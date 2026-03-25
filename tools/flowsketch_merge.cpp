#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

static constexpr char kMagic[] = "FLOWSKETCH_TAB_V1";

struct Node {
  std::string id;
  std::string kind;
  std::string text;
  std::string fill;
};

struct Edge {
  std::string from;
  std::string to;
  std::string kind;
  std::string color;
};

struct Block {
  size_t fi = 0;
  size_t bi = 0;
  std::string label;
  std::vector<std::string> node_ids;
  uint64_t visits = 0;
};

struct Func {
  size_t fi = 0;
  std::string mangled;
  std::string name;
  std::vector<Block> blocks;
  uint64_t visits = 0;
};

struct ModuleGraph {
  std::string module_id;
  std::vector<Func> funcs;
  std::unordered_map<std::string, Node> nodes;
  std::vector<Edge> edges;
  std::unordered_map<std::string, uint64_t> instrCallCount;
  std::unordered_map<std::string, uint64_t> instrLastBits;
};

static Func *findFunc(ModuleGraph &g, size_t fi);
static Block *findBlock(Func &f, size_t bi);

static std::string instrNodeId(uint64_t fi, uint64_t bi, uint64_t ii) {
  return "F" + std::to_string(fi) + "B" + std::to_string(bi) + "I" +
         std::to_string(ii);
}

static std::vector<std::string> splitWs(const std::string &line) {
  std::vector<std::string> w;
  std::istringstream iss(line);
  std::string t;
  while (iss >> t)
    w.push_back(t);
  return w;
}

static std::string formatTraceBits(uint64_t bits) {
  const int64_t asSigned = static_cast<int64_t>(bits);
  char buf[120];
  std::snprintf(buf, sizeof buf,
                "\\n@ trace: %" PRIu64 " (0x%" PRIx64 ")  i64:%" PRId64, bits,
                bits, asSigned);
  return std::string(buf);
}

static std::vector<std::string> split(const std::string &line) {
  std::vector<std::string> p;
  std::string cur;
  for (char c : line) {
    if (c == '\t') {
      p.push_back(cur);
      cur.clear();
    } else
      cur.push_back(c);
  }
  p.push_back(cur);
  return p;
}

/* Gentle heat: keep clusters pale so red CFG edges stay readable (no full red fill). */
static void heatTintCluster(std::string &hex, uint64_t v, uint64_t vmax) {
  if (hex.empty() || hex[0] != '#' || hex.size() != 7)
    return;
  if (vmax == 0 || v == 0)
    return;
  const double rhot =
      std::min(1.0, static_cast<double>(v) / static_cast<double>(vmax));
  const double t = 0.42 * rhot;
  auto parse = [&](int o) -> int {
    return std::stoi(hex.substr(1 + o * 2, 2), nullptr, 16);
  };
  const int r0 = parse(0), g0 = parse(1), b0 = parse(2);
  const int tr = 0xff, tg = 0xf5, tb = 0xe6;
  const int r = static_cast<int>(r0 * (1 - t) + tr * t);
  const int g = static_cast<int>(g0 * (1 - t) + tg * t);
  const int b = static_cast<int>(b0 * (1 - t) + tb * t);
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%02X%02X%02X", r, g, b);
  hex = buf;
}

static bool loadStatic(const std::string &path, ModuleGraph &g) {
  std::ifstream in(path);
  if (!in)
    return false;
  std::string line;
  if (!std::getline(in, line) || line != kMagic)
    return false;

  std::unordered_map<size_t, Func *> fmap;

  while (std::getline(in, line)) {
    if (line.empty())
      continue;
    const auto p = split(line);
    if (p.empty())
      continue;
    if (p[0] == "M" && p.size() >= 2) {
      g.module_id = p[1];
    } else if (p[0] == "F" && p.size() >= 4) {
      Func fn;
      fn.fi = std::stoull(p[1]);
      fn.mangled = p[2];
      fn.name = p[3];
      g.funcs.push_back(std::move(fn));
      fmap[g.funcs.back().fi] = &g.funcs.back();
    } else if (p[0] == "B" && p.size() >= 4) {
      const size_t fi = std::stoull(p[1]);
      Block bb;
      bb.fi = fi;
      bb.bi = std::stoull(p[2]);
      bb.label = p[3];
      auto it = fmap.find(fi);
      if (it != fmap.end())
        it->second->blocks.push_back(std::move(bb));
    } else if (p[0] == "N" && p.size() >= 5) {
      Node n;
      n.id = p[1];
      n.kind = p[2];
      n.text = p[3];
      n.fill = p[4];
      g.nodes[n.id] = std::move(n);
    } else if (p[0] == "E" && p.size() >= 5) {
      Edge e;
      e.from = p[1];
      e.to = p[2];
      e.kind = p[3];
      e.color = p[4];
      g.edges.push_back(std::move(e));
    }
  }

  for (Func &fn : g.funcs) {
    std::sort(fn.blocks.begin(), fn.blocks.end(),
              [](const Block &a, const Block &b) { return a.bi < b.bi; });
  }

  auto attachByPrefix = [&](const std::string &id) {
    if (!g.nodes.count(id))
      return;
    size_t fi = 0, bi = 0;
    if (std::sscanf(id.c_str(), "F%zuB%zu", &fi, &bi) < 2)
      return;
    if (Func *fn = findFunc(g, fi))
      if (Block *bb = findBlock(*fn, bi)) {
        if (std::find(bb->node_ids.begin(), bb->node_ids.end(), id) ==
            bb->node_ids.end())
          bb->node_ids.push_back(id);
      }
  };

  for (const auto &kv : g.nodes)
    attachByPrefix(kv.first);

  return true;
}

static Func *findFunc(ModuleGraph &g, size_t fi) {
  for (Func &f : g.funcs)
    if (f.fi == fi)
      return &f;
  return nullptr;
}

static Block *findBlock(Func &f, size_t bi) {
  for (Block &b : f.blocks)
    if (b.bi == bi)
      return &b;
  return nullptr;
}

static bool loadDynamic(const std::string &path, ModuleGraph &g) {
  std::ifstream in(path);
  if (!in)
    return true;
  std::string line;
  while (std::getline(in, line)) {
    const auto w = splitWs(line);
    if (w.empty())
      continue;
    if (w[0] == "FN" && w.size() >= 2) {
      const uint64_t fi = std::stoull(w[1]);
      if (Func *f = findFunc(g, static_cast<size_t>(fi)))
        ++f->visits;
    } else if (w[0] == "BB" && w.size() >= 3) {
      const uint64_t fi = std::stoull(w[1]);
      const uint64_t bi = std::stoull(w[2]);
      if (Func *f = findFunc(g, static_cast<size_t>(fi)))
        if (Block *bb = findBlock(*f, static_cast<size_t>(bi)))
          ++bb->visits;
    } else if (w[0] == "CALL" && w.size() >= 3) {
      const uint64_t fi = std::stoull(w[1]);
      const uint64_t bi = std::stoull(w[2]);
      if (w.size() >= 4) {
        const uint64_t ii = std::stoull(w[3]);
        ++g.instrCallCount[instrNodeId(fi, bi, ii)];
      } else {
        if (Func *f = findFunc(g, static_cast<size_t>(fi)))
          if (Block *bb = findBlock(*f, static_cast<size_t>(bi)))
            ++bb->visits;
      }
    } else if (w[0] == "VAL" && w.size() >= 5) {
      const uint64_t fi = std::stoull(w[1]);
      const uint64_t bi = std::stoull(w[2]);
      const uint64_t ii = std::stoull(w[3]);
      const uint64_t bits = std::stoull(w[4]);
      g.instrLastBits[instrNodeId(fi, bi, ii)] = bits;
    }
  }
  return true;
}

static void writeDot(const std::string &path, ModuleGraph &g) {
  uint64_t maxVis = 0;
  for (const Func &f : g.funcs) {
    maxVis = std::max(maxVis, f.visits);
    for (const Block &b : f.blocks)
      maxVis = std::max(maxVis, b.visits);
  }

  std::ofstream out(path);
  out << "digraph FlowSketch {\n";
  out << "  graph [rankdir=TB, fontname=\"Helvetica\"];\n";
  out << "  node [fontname=\"Helvetica\", style=filled, shape=box];\n";
  out << "  edge [fontname=\"Helvetica\"];\n";

  out << "  subgraph cluster_module {\n";
  out << "    label = \"module: " << g.module_id << "\";\n";
  out << "    style=filled;\n    fillcolor=\"#fafafa\";\n";

  for (Func &f : g.funcs) {
    std::string ffill = "#e8eef7";
    heatTintCluster(ffill, f.visits, maxVis);
    out << "    subgraph cluster_fn_" << f.fi << " {\n";
    out << "      label = \"" << f.name << "\\nFN visits: " << f.visits << "\";\n";
    out << "      style=filled;\n      fillcolor=\"" << ffill << "\";\n";

    for (Block &b : f.blocks) {
      std::string bfill = "#f0f4f8";
      heatTintCluster(bfill, b.visits, maxVis);
      out << "      subgraph cluster_bb_" << f.fi << "_" << b.bi << " {\n";
      out << "        label = \"" << b.label << " (B" << b.bi << ")  runs: "
          << b.visits << "\";\n";
      out << "        style=filled;\n        fillcolor=\"" << bfill << "\";\n";
      for (const std::string &nid : b.node_ids) {
        auto it = g.nodes.find(nid);
        if (it == g.nodes.end())
          continue;
        const Node &n = it->second;
        const char *shape =
            n.kind == "const" ? "ellipse" : (n.kind == "extfn" ? "oval" : "box");
        out << "        \"" << n.id << "\" [label=\"" << n.text;
        auto tr = g.instrLastBits.find(nid);
        if (tr != g.instrLastBits.end())
          out << formatTraceBits(tr->second);
        out << "\", fillcolor=\"" << n.fill << "\", shape=" << shape << "];\n";
      }
      out << "      }\n";
    }
    out << "    }\n";
  }

  for (const auto &kv : g.nodes) {
    bool placed = false;
    for (const Func &f : g.funcs) {
      for (const Block &b : f.blocks) {
        if (std::find(b.node_ids.begin(), b.node_ids.end(), kv.first) !=
            b.node_ids.end()) {
          placed = true;
          break;
        }
        if (placed)
          break;
      }
      if (placed)
        break;
    }
    if (placed)
      continue;
    const Node &n = kv.second;
    const char *shape =
        n.kind == "const" ? "ellipse" : (n.kind == "extfn" ? "oval" : "box");
    out << "    \"" << n.id << "\" [label=\"" << n.text;
    auto tr = g.instrLastBits.find(kv.first);
    if (tr != g.instrLastBits.end())
      out << formatTraceBits(tr->second);
    out << "\", fillcolor=\"" << n.fill << "\", shape=" << shape << "];\n";
  }

  out << "  }\n";

  for (const Edge &e : g.edges) {
    out << "  \"" << e.from << "\" -> \"" << e.to << "\" [";
    if (e.kind == "cfg") {
      out << "color=\"#5d0f0f\", penwidth=2.2";
    } else if (e.kind == "call") {
      out << "color=\"" << e.color << "\", penwidth=1.75";
      auto ct = g.instrCallCount.find(e.from);
      if (ct != g.instrCallCount.end() && ct->second > 0)
        out << ", label=\"calls:" << ct->second
            << "\", fontcolor=\"#0d47a1\", fontsize=11";
    } else {
      out << "color=\"" << e.color << "\"";
      if (e.kind == "dfg")
        out << ", penwidth=1.2";
    }
    out << "];\n";
  }

  out << "}\n";
}

} // namespace

int main(int argc, char **argv) {
  const char *staticPath = "log/static.flow.txt";
  const char *dynamicPath = "log/dynamic.flow.log";
  const char *dotPath = "log/flowsketch.dot";
  if (argc >= 2)
    staticPath = argv[1];
  if (argc >= 3)
    dynamicPath = argv[2];
  if (argc >= 4)
    dotPath = argv[3];

  ModuleGraph g;
  if (!loadStatic(staticPath, g)) {
    std::cerr << "Failed to read " << staticPath << std::endl;
    return 1;
  }
  loadDynamic(dynamicPath, g);
  writeDot(dotPath, g);
  std::cout << "Wrote " << dotPath << std::endl;
  return 0;
}
