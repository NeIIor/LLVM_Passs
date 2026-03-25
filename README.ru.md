# FlowSketch — CFG и DFG из LLVM IR

**Языки:** [English](README.md)

**FlowSketch** — учебный минимальный конвейер: плагин LLVM (**New PM**) `FlowSketchPass` сериализует граф в простой текстовый формат и вставляет лёгкие вызовы профилировщика; утилита `flowsketch-merge` объединяет статический дамп с логом запуска и строит **Graphviz DOT**.

## Зачем это нужно

- **CFG** — как исполняются инструкции и блоки: порядок внутри базового блока, переходы `br`/`switch`, межпроцедурные рёбра **вызовов** (синие).
- **DFG** — по каким SSA-значениям «течёт» вычисление: по связям **пользователей** инструкций (тёмные рёбра между инструкциями).
- **Константы** — операнды, которые не являются инструкциями, выводятся отдельными **зелёными** узлами и соединяются с потребителем.
- **Динамика** — после прогона инструментированной программы счётчики входов в функции и блоки подкрашивают вложенные кластеры (чем чаще заход, тем «теплее» оттенок).

## Где здесь LLVM

LLVM даёт программу в виде **SSA**: `Module` → `Function` → `BasicBlock` → `Instruction`. Проход обходит этот IR, вешает **стабильные синтетические id** вида `F{функция}B{блок}I{индекс}` (а не адреса в памяти) и записывает рёбра CFG/DFG/вызовов. IR — эталон для потока управления (терминаторы, разбиение на блоки) и потока данных (`Instruction::uses()`, операнды).

**Как устроен этот проект:**

- таб-разделённый `log/static.flow.txt` с заголовком версии;
- иерархические id для инструкций;
- `flowsketch-merge` строит Graphviz DOT из статики и динамики;
- хуки времени выполнения в `runtime/flowsketch_rt.c` (`__flowsketch_*`).

## Пример графа (CFG + DFG + динамика)

Картинка лежит в репозитории: `docs/flowsketch_sample.svg` — построена из [`examples/advanced.c`](examples/advanced.c) стандартным скриптом. На синих рёбрах вызовов — `calls:N`, у части узлов `call` — строка **trace** с битовым снимком возврата, у кластеров — **FN visits** / **runs:**.

![Пример графа FlowSketch](docs/flowsketch_sample.svg)

**Обновление скриншота:** после правок прогона выполните `sh scripts/run_demo.sh` и скопируйте `log/flowsketch.svg` в `docs/flowsketch_sample.svg`.

## Сборка

Нужно: `clang`, `opt` (LLVM 14 или совместимый), `cmake`, `g++`, по желанию `graphviz`.

Подготовка заголовков (без root):

```sh
cd "/путь/к/LLVM_Pass"
apt-get download llvm-14-dev
dpkg-deb -x llvm-14-dev_*.deb .llvm-hdrs
```

### CMake

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Плагин: `build/libFlowSketchPass.so`. Либо вручную:

```sh
sh scripts/build_plugin.sh build/libFlowSketchPass.so
```

## Быстрый демо

```sh
sh scripts/run_demo.sh                      # по умолчанию examples/advanced.c (clang + opt)
sh scripts/run_demo.sh examples/demo.c      # короткий пример
sh scripts/run_demo.sh examples/llvm_course/hello.c  # примеры из llvm_course
sh scripts/run_demo_clang.sh                # то же через clang -fpass-plugin (без opt)
dot -Tsvg log/flowsketch.dot -o log/flowsketch.svg
```

### Примеры из [llvm_course / LLVM_Pass](https://github.com/lisitsynSA/llvm_course/tree/main/LLVM_Pass)

В [`examples/llvm_course/`](examples/llvm_course/) лежат те же исходники, что и в каталоге **c_examples** курса **lisitsynSA/llvm_course** ([файлы на GitHub](https://github.com/lisitsynSA/llvm_course/tree/main/LLVM_Pass/c_examples)): `hello.c`, `calc.c`, `fact.c`. Мы **прогнали их через FlowSketch** и положили в репозиторий готовые картинки:

- [`docs/course_samples/hello.svg`](docs/course_samples/hello.svg)  
- [`docs/course_samples/calc.svg`](docs/course_samples/calc.svg)  
- [`docs/course_samples/fact.svg`](docs/course_samples/fact.svg) (запуск с аргументом `6`)

Как воспроизвести — в [`examples/llvm_course/README.md`](examples/llvm_course/README.md). Для `fact.c` после сборки нужен числовой аргумент у `./build/demo_prof`.

Смотрите также `log/flowsketch.svg` после своего прогона.

## Как читать граф

| Элемент | Смысл |
|--------|--------|
| Вложенные кластеры | Модуль → функция → базовый блок |
| Голубые прямоугольники | Инструкции (имя опкода LLVM) |
| Зелёные эллипсы | Константы / не-инструкционные операнды |
| Жёлтые овалы | Внешние функции (только декларация в модуле) |
| **Красные** стрелки | CFG (порядок в блоке, ветвления) |
| **Чёрные** стрелки | DFG (от производителя к потребителю SSA) |
| **Синие** стрелки | Вызовы (к входу тела или к внешнему узлу) |
| **Зелёные** стрелки | Значение-константа → инструкция |
| Подписи `runs:` / `FN visits:` | Сколько раз зафиксирован вход (0, если бинарь не запускали) |

### Как анализировать осмысленно

1. Войдите в кластер **функции** — это отдельная «карта» поведения одной сущности.
2. Внутри блока двигайтесь по **красным** рёбрам сверху вниз — это реальный порядок инструкций до терминатора.
3. Для любой инструкции пройдите по **чёрным** рёбрам назад — увидите цепочку SSA-определений (аддитивные цепочки, `phi`, и т.д.).
4. **Синие** рёбра показывают межпроцедурный контроль: куда уходит `call`.
5. Сравнивайте окраску до/после изменения кода или входных данных — горячие блоки должны соответствовать реальному сценарию запуска.

### Доп. динамика (после прогона)

- **Синие рёбра вызовов:** в логе строки `CALL fi bi ii` дают **сколько раз** исполнялась конкретная инструкция `call` (по её индексу `ii` в базовом блоке); на ребро в DOT добавляется подпись `calls:N`.
- **Значения:** после скалярного `call` с непустым результатом пишется `VAL fi bi ii bits` (сырое `i64`: целые через `zext`, `float`/`double` через `bitcast` в биты). На узел инструкции добавляется строка `@ trace: …`.

## Файлы

- `pass/FlowSketchPass.cpp` — проход LLVM.
- `runtime/flowsketch_rt.c` — запись лога времени выполнения.
- `tools/flowsketch_merge.cpp` — слияние и DOT.
- `log/static.flow.txt` — перезаписывается при каждом `opt`.
- `log/dynamic.flow.log` — создаётся при запуске программы (режим перезаписи).
- `scripts/run_demo_clang.sh` — тот же демо-конвейер через `clang -fpass-plugin` (без отдельного `opt`).

## См. также

В репозитории [llvm_course, каталог **LLVM_Pass**](https://github.com/lisitsynSA/llvm_course/tree/main/LLVM_Pass) — пошаговые примеры: регистрация прохода, дамп функций/блоков/инструкций, uses, правка IR, инструментирование в стиле CFG (`Pass6_cfg.cpp`), «плохая» оптимизация. Удобно сравнить с workflow `clang -fpass-plugin` и отдельным `log.c` ([README курса](https://github.com/lisitsynSA/llvm_course/blob/main/LLVM_Pass/README.md)).

### Сборка через `clang -fpass-plugin` (по желанию)

Как в курсе: объектное с программой собирать **с** плагином, `flowsketch_rt.c` — **без** плагина (иначе заинструментируется рантайм).

```sh
clang -fpass-plugin=build/libFlowSketchPass.so -O0 -g -Iinclude \
  -c examples/advanced.c -o build/advanced_inst.o
clang -O0 -g -Iinclude -c runtime/flowsketch_rt.c -o build/rt.o
clang build/advanced_inst.o build/rt.o -o build/demo_clang
```

Или одной командой: `sh scripts/run_demo_clang.sh` (плюс merge и `dot`). Версия **Clang** должна быть совместима по major с **LLVM**, с которым собран `.so`; на macOS иногда нужны флаги линковки (см. README курса).

## Лицензия

Учебный / справочный пример — используйте со ссылкой на авторов при публикации производных работ.
