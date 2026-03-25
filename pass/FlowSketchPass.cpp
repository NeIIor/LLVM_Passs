#include "flowsketch_rt.h"

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

static constexpr char kMagic[] = "FLOWSKETCH_TAB_V1";
static constexpr char kStaticPath[] = "log/static.flow.txt";

static constexpr llvm::StringLiteral kCfgColor = "#e53935";
static constexpr llvm::StringLiteral kDfgColor = "#1e1e1e";
static constexpr llvm::StringLiteral kConstEdgeColor = "#2e7d32";
static constexpr llvm::StringLiteral kCallColor = "#1e88e5";
static constexpr llvm::StringLiteral kInstrFill = "#90caf9";
static constexpr llvm::StringLiteral kConstFill = "#a5d6a7";
static constexpr llvm::StringLiteral kExtFnFill = "#fff59d";

static bool isRuntimeHook(const llvm::Function *fn) {
  if (!fn)
    return false;
  const llvm::StringRef n = fn->getName();
  return n == "__flowsketch_fn_enter" || n == "__flowsketch_bb_enter" ||
         n == "__flowsketch_call_site" || n == "__flowsketch_val_i64";
}

static bool shouldSkipCall(const llvm::CallBase *cb) {
  if (llvm::isa<llvm::IntrinsicInst>(cb))
    return true;
  const llvm::Function *callee = cb->getCalledFunction();
  return callee && isRuntimeHook(callee);
}

static std::string bbLabel(const llvm::BasicBlock &bb, size_t bi) {
  if (bb.hasName())
    return bb.getName().str();
  return "bb_" + std::to_string(bi);
}

static std::string instrId(size_t fi, size_t bi, size_t ii) {
  return "F" + std::to_string(fi) + "B" + std::to_string(bi) + "I" +
         std::to_string(ii);
}

static std::string constId(size_t fi, size_t bi, size_t ii, unsigned op) {
  return "F" + std::to_string(fi) + "B" + std::to_string(bi) + "I" +
         std::to_string(ii) + "_op" + std::to_string(op);
}

static std::string extFnId(const llvm::Function &f) {
  return std::string("EXT:") + f.getName().str();
}

static void appendOperandText(const llvm::Value *v, std::string &out) {
  llvm::raw_string_ostream os(out);
  v->getType()->print(os);
  os << " ";
  if (const auto *ci = llvm::dyn_cast<llvm::ConstantInt>(v))
    ci->getValue().print(os, false);
  else
    v->printAsOperand(os, false);
  os.flush();
}

static size_t functionIndexInModule(llvm::Module &M, const llvm::Function *target) {
  size_t idx = 0;
  for (const llvm::Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (&F == target)
      return idx;
    ++idx;
  }
  return static_cast<size_t>(-1);
}

static llvm::Value *coerceScalarToI64(llvm::IRBuilder<> &ir, llvm::Value *v) {
  llvm::Type *t = v->getType();
  llvm::LLVMContext &ctx = v->getContext();
  llvm::Type *i64 = llvm::Type::getInt64Ty(ctx);
  if (t->isVoidTy() || t->isAggregateType() || t->isVectorTy())
    return nullptr;
  if (t->isPointerTy())
    return ir.CreatePtrToInt(v, i64);
  if (t->isIntegerTy()) {
    const unsigned bw = t->getIntegerBitWidth();
    if (bw < 64)
      return ir.CreateZExt(v, i64);
    if (bw == 64)
      return v;
    return ir.CreateTrunc(v, i64);
  }
  if (t->isFloatingPointTy()) {
    const unsigned w = t->getPrimitiveSizeInBits();
    llvm::Type *it = llvm::Type::getIntNTy(ctx, w);
    llvm::Value *b = ir.CreateBitCast(v, it);
    if (w < 64)
      return ir.CreateZExt(b, i64);
    if (w == 64)
      return b;
    return ir.CreateTrunc(b, i64);
  }
  return nullptr;
}

struct FlowSketchPass : llvm::PassInfoMixin<FlowSketchPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &) {
    emitStaticGraph(M);
    instrument(M);
    return llvm::PreservedAnalyses::none();
  }

private:
  void instrument(llvm::Module &M) {
    llvm::LLVMContext &ctx = M.getContext();
    llvm::Type *voidTy = llvm::Type::getVoidTy(ctx);
    llvm::Type *i64 = llvm::Type::getInt64Ty(ctx);

    llvm::FunctionCallee enterFn = M.getOrInsertFunction(
        "__flowsketch_fn_enter", llvm::FunctionType::get(voidTy, {i64}, false));
    llvm::FunctionCallee bbFn = M.getOrInsertFunction(
        "__flowsketch_bb_enter",
        llvm::FunctionType::get(voidTy, {i64, i64}, false));
    llvm::FunctionCallee callFn = M.getOrInsertFunction(
        "__flowsketch_call_site",
        llvm::FunctionType::get(voidTy, {i64, i64, i64}, false));
    llvm::FunctionCallee valFn = M.getOrInsertFunction(
        "__flowsketch_val_i64",
        llvm::FunctionType::get(voidTy, {i64, i64, i64, i64}, false));

    size_t funcIndex = 0;
    for (llvm::Function &F : M) {
      if (F.isDeclaration())
        continue;

      llvm::IRBuilder<> ir(ctx);
      size_t bbIndex = 0;
      for (llvm::BasicBlock &B : F) {
        std::vector<std::pair<llvm::CallBase *, size_t>> sites;
        size_t ii = 0;
        for (llvm::Instruction &I : B) {
          if (auto *cb = llvm::dyn_cast<llvm::CallBase>(&I)) {
            if (!shouldSkipCall(cb))
              sites.push_back({cb, ii});
          }
          ++ii;
        }

        ir.SetInsertPoint(&*B.getFirstInsertionPt());
        if (bbIndex == 0)
          ir.CreateCall(enterFn, {ir.getInt64(funcIndex)});
        ir.CreateCall(bbFn, {ir.getInt64(funcIndex), ir.getInt64(bbIndex)});

        for (auto it = sites.rbegin(); it != sites.rend(); ++it) {
          llvm::CallBase *cb = it->first;
          ir.SetInsertPoint(cb);
          ir.CreateCall(callFn, {ir.getInt64(funcIndex), ir.getInt64(bbIndex),
                                 ir.getInt64(it->second)});
        }

        for (const auto &pr : sites) {
          llvm::CallBase *cb = pr.first;
          const size_t ii = pr.second;
          auto *ci = llvm::dyn_cast<llvm::CallInst>(cb);
          if (!ci || ci->getType()->isVoidTy())
            continue;
          if (ci->getType()->isAggregateType() || ci->getType()->isVectorTy())
            continue;

          llvm::BasicBlock::iterator loc = ci->getIterator();
          ++loc;
          if (loc != B.end())
            ir.SetInsertPoint(&*loc);
          else
            ir.SetInsertPoint(B.getTerminator());

          llvm::Value *bits = coerceScalarToI64(ir, ci);
          if (bits)
            ir.CreateCall(valFn, {ir.getInt64(funcIndex), ir.getInt64(bbIndex),
                                  ir.getInt64(ii), bits});
        }

        ++bbIndex;
      }
      ++funcIndex;
    }
  }

  void emitStaticGraph(llvm::Module &M) {
    llvm::sys::fs::create_directories(llvm::Twine("log"), /* IgnoreExisting */ true);

    std::ofstream out(kStaticPath);
    if (!out)
      return;

    out << kMagic << '\n';
    out << "M\t" << M.getModuleIdentifier() << '\n';

    std::unordered_map<const llvm::Function *, std::string> extNodes;
    for (llvm::Function &F : M) {
      if (F.isDeclaration()) {
        const std::string id = extFnId(F);
        extNodes[&F] = id;
        out << "N\t" << id << "\textfn\t"
            << llvm::demangle(F.getName().str()) << '\t' << kExtFnFill.str()
            << '\n';
      }
    }

    bool wroteIndirectStub = false;
    auto ensureIndirect = [&](std::ofstream &os) {
      if (wroteIndirectStub)
        return;
      wroteIndirectStub = true;
      os << "N\tEXT:?indirect\textfn\tindirect callsite\t" << kExtFnFill.str()
         << '\n';
    };

    size_t funcIndex = 0;
    for (llvm::Function &F : M) {
      if (F.isDeclaration())
        continue;

      out << "F\t" << funcIndex << '\t' << F.getName().str() << '\t'
          << llvm::demangle(F.getName().str()) << '\n';

      std::unordered_map<const llvm::Instruction *, std::string> idOf;
      size_t bbIndex = 0;
      for (llvm::BasicBlock &B : F) {
        out << "B\t" << funcIndex << '\t' << bbIndex << '\t'
            << bbLabel(B, bbIndex) << '\n';

        size_t localI = 0;
        for (llvm::Instruction &I : B) {
          const std::string nid = instrId(funcIndex, bbIndex, localI);
          idOf[&I] = nid;
          out << "N\t" << nid << "\tinstr\t" << I.getOpcodeName() << '\t'
              << kInstrFill.str() << '\n';

          unsigned opi = 0;
          for (llvm::Use &u : I.operands()) {
            llvm::Value *v = u.get();
            if (llvm::isa<llvm::Instruction>(v)) {
              ++opi;
              continue;
            }
            const std::string cid = constId(funcIndex, bbIndex, localI, opi);
            std::string label;
            appendOperandText(v, label);
            out << "N\t" << cid << "\tconst\t" << label << '\t'
                << kConstFill.str() << '\n';
            out << "E\t" << cid << '\t' << nid << "\tuse_const\t"
                << kConstEdgeColor.str() << '\n';
            ++opi;
          }
          ++localI;
        }
        ++bbIndex;
      }

      bbIndex = 0;
      for (llvm::BasicBlock &B : F) {
        llvm::Instruction *prev = nullptr;
        for (llvm::Instruction &I : B) {
          const std::string cur = idOf[&I];

          if (prev) {
            out << "E\t" << idOf[prev] << '\t' << cur << "\tcfg\t"
                << kCfgColor.str() << '\n';
          }

          for (llvm::Use &u : I.uses()) {
            auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());
            if (!user)
              continue;
            auto it = idOf.find(user);
            if (it != idOf.end())
              out << "E\t" << cur << '\t' << it->second << "\tdfg\t"
                  << kDfgColor.str() << '\n';
          }

          if (auto *br = llvm::dyn_cast<llvm::BranchInst>(&I)) {
            for (unsigned s = 0; s < br->getNumSuccessors(); ++s) {
              llvm::Instruction *head = &*br->getSuccessor(s)->begin();
              auto hit = idOf.find(head);
              if (hit != idOf.end())
                out << "E\t" << cur << '\t' << hit->second << "\tcfg\t"
                    << kCfgColor.str() << '\n';
            }
          } else if (auto *sw = llvm::dyn_cast<llvm::SwitchInst>(&I)) {
            for (unsigned si = 0; si < sw->getNumSuccessors(); ++si) {
              llvm::BasicBlock *suc = sw->getSuccessor(si);
              llvm::Instruction *head = &*suc->begin();
              auto hit = idOf.find(head);
              if (hit != idOf.end())
                out << "E\t" << cur << '\t' << hit->second << "\tcfg\t"
                    << kCfgColor.str() << '\n';
            }
          } else if (auto *cb = llvm::dyn_cast<llvm::CallBase>(&I)) {
            llvm::Function *callee = cb->getCalledFunction();
            if (callee) {
              if (callee->isDeclaration()) {
                auto extIt = extNodes.find(callee);
                const std::string ext =
                    extIt != extNodes.end() ? extIt->second : extFnId(*callee);
                out << "E\t" << cur << '\t' << ext << "\tcall\t"
                    << kCallColor.str() << '\n';
              } else {
                llvm::Instruction *head = &callee->getEntryBlock().front();
                std::unordered_map<const llvm::Instruction *, std::string> calleeIds;
                size_t fiCallee = functionIndexInModule(M, callee);
                size_t cBi = 0;
                for (llvm::BasicBlock &BBc : *callee) {
                  size_t cIi = 0;
                  for (llvm::Instruction &j : BBc) {
                    calleeIds[&j] = instrId(fiCallee, cBi, cIi);
                    ++cIi;
                  }
                  ++cBi;
                }
                auto hit = calleeIds.find(head);
                if (hit != calleeIds.end())
                  out << "E\t" << cur << '\t' << hit->second << "\tcall\t"
                      << kCallColor.str() << '\n';
              }
            } else {
              ensureIndirect(out);
              out << "E\t" << cur << "\tEXT:?indirect\tcall\t"
                  << kCallColor.str() << '\n';
            }
          }

          prev = &I;
        }
        ++bbIndex;
      }

      ++funcIndex;
    }
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "FlowSketchPass", "0.1.0",
          [](llvm::PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](llvm::StringRef name, llvm::ModulePassManager &pm,
                   llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (name == "flowsketch") {
                    pm.addPass(FlowSketchPass{});
                    return true;
                  }
                  return false;
                });
            PB.registerOptimizerLastEPCallback(
                [](llvm::ModulePassManager &pm, llvm::OptimizationLevel) {
                  pm.addPass(FlowSketchPass{});
                  return true;
                });
          }};
}
