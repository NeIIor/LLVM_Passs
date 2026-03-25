# Examples from [llvm_course](https://github.com/lisitsynSA/llvm_course/tree/main/LLVM_Pass/c_examples)

Small programs used in **lisitsynSA/llvm_course** for LLVM pass tutorials (names, dump, uses, CFG instrumentation). Copied here so you can feed them directly into FlowSketch.

| File        | Idea |
|-------------|------|
| `hello.c`   | Branch inside a helper; `printf`. |
| `calc.c`    | Nested static calls; one `printf`. |
| `fact.c`    | Recursion + `atoi`; **needs one argv** (e.g. `5`). |

Run (from repo root):

```sh
sh scripts/run_demo.sh examples/llvm_course/hello.c
sh scripts/run_demo.sh examples/llvm_course/calc.c
```

For `fact.c`, build with the script then pass an argument to the binary (the script runs it once with no args by default, which hits the usage path):

```sh
sh scripts/run_demo.sh examples/llvm_course/fact.c
./build/demo_prof 6
./build/flowsketch-merge
dot -Tsvg log/flowsketch.dot -o log/flowsketch.svg
```

Upstream: [c_examples on GitHub](https://github.com/lisitsynSA/llvm_course/tree/main/LLVM_Pass/c_examples).

## Pre-generated graphs

After running the three examples locally, SVGs are kept in the repo under:

- [`docs/course_samples/hello.svg`](../../docs/course_samples/hello.svg)  
- [`docs/course_samples/calc.svg`](../../docs/course_samples/calc.svg)  
- [`docs/course_samples/fact.svg`](../../docs/course_samples/fact.svg) (run with argument `6`)

Regenerate from project root:

```sh
sh scripts/run_demo.sh examples/llvm_course/hello.c && cp log/flowsketch.svg docs/course_samples/hello.svg
sh scripts/run_demo.sh examples/llvm_course/calc.c && cp log/flowsketch.svg docs/course_samples/calc.svg
sh scripts/run_demo.sh examples/llvm_course/fact.c
rm -f log/dynamic.flow.log && ./build/demo_prof 6
./build/flowsketch-merge && dot -Tsvg log/flowsketch.dot -o docs/course_samples/fact.svg
```
