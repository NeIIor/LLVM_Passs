#include "flowsketch_rt.h"

#include <inttypes.h>
#include <stdio.h>

static FILE *g_log;
static int g_tried_open;

static void open_log(void) {
  if (g_tried_open)
    return;
  g_tried_open = 1;
  g_log = fopen("log/dynamic.flow.log", "w");
  if (g_log)
    setbuf(g_log, NULL);
}

void __flowsketch_fn_enter(uint64_t func_index) {
  open_log();
  if (g_log) {
    fprintf(g_log, "FN %" PRIu64 "\n", func_index);
    return;
  }
  fprintf(stderr, "FN %" PRIu64 "\n", func_index);
}

void __flowsketch_bb_enter(uint64_t func_index, uint64_t bb_index) {
  open_log();
  if (g_log) {
    fprintf(g_log, "BB %" PRIu64 " %" PRIu64 "\n", func_index, bb_index);
    return;
  }
  fprintf(stderr, "BB %" PRIu64 " %" PRIu64 "\n", func_index, bb_index);
}

void __flowsketch_call_site(uint64_t func_index, uint64_t bb_index,
                            uint64_t instr_index) {
  open_log();
  if (g_log) {
    fprintf(g_log, "CALL %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", func_index,
            bb_index, instr_index);
    return;
  }
  fprintf(stderr, "CALL %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", func_index,
          bb_index, instr_index);
}

void __flowsketch_val_i64(uint64_t func_index, uint64_t bb_index,
                          uint64_t instr_index, uint64_t bits) {
  open_log();
  if (g_log) {
    fprintf(g_log, "VAL %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
            func_index, bb_index, instr_index, bits);
    return;
  }
  fprintf(stderr, "VAL %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
          func_index, bb_index, instr_index, bits);
}
