#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logging hooks inserted by the LLVM pass (names are stable for llvm.name metadata). */
void __flowsketch_fn_enter(uint64_t func_index);
void __flowsketch_bb_enter(uint64_t func_index, uint64_t bb_index);
void __flowsketch_call_site(uint64_t func_index, uint64_t bb_index,
                            uint64_t instr_index_in_block);
void __flowsketch_val_i64(uint64_t func_index, uint64_t bb_index,
                          uint64_t instr_index_in_block, uint64_t bits);

#ifdef __cplusplus
}
#endif
