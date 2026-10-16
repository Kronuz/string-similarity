# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
basic_string_metric.h        CRTP base StringMetric<Impl> + the Counter helper. The shared surface. Header.
levenshtein.h                Edit distance, normalized by the longer length.
jaro.h                       Jaro similarity.
jaro_winkler.h               Jaro with a shared-prefix boost; derives from Jaro.
lcsubstr.h                   Longest common substring.
lcsubsequence.h              Longest common subsequence.
jaccard.h                    Jaccard index over character sets.
sorensen_dice.h              Sørensen-Dice over character bigrams.
phonetic_metric.h             PhoneticMetric<Encoder, Metric>: encode phonetically, score the codes.
test/test.cc                 Runnable smoke test: asserts known values for every metric.
examples/demo.cc             A runnable tour (not a test).
CMakeLists.txt               INTERFACE library `string_similarity` (+ alias); FetchContent strings + soundex; CTest test `string_similarity`.
LICENSE                      MIT, Copyright (c) 2015-2019 Dubalu LLC.
README.md                    What it is, install, quick start.
docs/API.md                  The metrics and their usage.
docs/string-similarity.d2    The README diagram's source (docs/string-similarity.svg is rendered from it).
ARCHITECTURE.md              The CRTP base, what each metric computes, the dispatch subtlety.
```

This is header-only. There is nothing to compile or link beyond the headers; the
CMake target is a pure `INTERFACE` library that puts the source dir on the
consumer's include path and links the two dependencies.

## Build and run the test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

The first configure fetches `strings` and `soundex` over the network
(FetchContent, `GIT_TAG main`). Expected output ends with
`all string-similarity tests passed`, exit 0. The test target is
`string_similarity_test`; the registered CTest name is `string_similarity`. The test and
demo are only added when this repo is the top-level project (CMakeLists.txt), so
consumers vendoring it via `FetchContent` won't build them.

## Dependencies

Two siblings in the same family, both linked `INTERFACE`:

- **[strings](https://github.com/Kronuz/strings)** — `basic_string_metric.h`
  includes `"strings.hh"` for `strings::upper` (the case-folding step). Part of
  the public surface, so it must ride onto the consumer's include path.
- **[soundex](https://github.com/Kronuz/soundex)** — `phonetic_metric.h` is a
  template over a phonetic encoder; the concrete encoders (`SoundexEnglish` in
  `english_soundex.h`, etc.) live there. Consumers of the soundex wrapper include
  one of those headers, so soundex is linked `INTERFACE` too.

`soundex` itself depends on `strings`. We `FetchContent_Declare(strings ...)`
before `soundex` so that declaration wins (FetchContent dedups by the declared
name and keeps the first), and both resolve to a single copy of `strings`. We
track both at `GIT_TAG main`, like the rest of the family.

## Conventions

- **C++20.** The target requests `cxx_std_20` `INTERFACE` to stay uniform with the
  sibling libraries. Don't drop below it.
- **Filenames are stable.** The headers keep their original Xapiand names so a
  consumer that already `#include`s them just needs this repo on the include path.
  Don't rename them. (The repo is `string-similarity`; the CMake target/alias use the
  underscore form `string_similarity` because `-` is not valid in a CMake target.)
- Tabs for indentation, double quotes in code, no em dashes in prose.
- MIT-licensed; keep the copyright header (Copyright (c) 2015-2019 Dubalu LLC) on
  source files.

## Load-bearing invariants

- **No persistence.** On extraction the `serialise()` / `unserialise()` methods
  and their includes (`length.h`, the GPL `xapian/common/serialise-double.h`) were
  removed; the metrics are pure algorithms now. Don't reintroduce them here — if
  Xapiand ever de-vendors this, it keeps its own serialise adapter on top. Do not
  call `_soundex.serialise()` either; the soundex library has no serialise.
- **The CRTP base owns the base cases and case folding.** Identical strings
  short-circuit to `0` / `1`, an empty operand to `1` / `0`, and `_icase` folds
  both operands with `strings::upper` before any subclass `_distance` /
  `_similarity` runs. A subclass kernel sees normalized, non-trivial input; keep it
  that way rather than re-checking inside a metric.
- **Subclasses are befriended.** Each metric declares
  `friend class StringMetric<Impl>;` (or `friend Jaro;` for Jaro-Winkler) so the
  base can call its private kernels. Keep the friend declaration when adding a
  metric.
- **The dispatch is fixed at the StringMetric<...> layer.** For `Jaro_Winkler`
  and `PhoneticMetric`, the base's CRTP `Impl` is the parent (`Jaro`, the inner
  `Metric`), so the public two-argument call reaches the parent's kernel, not the
  derived one. This is intentional, preserved-from-upstream behavior. Don't "fix"
  it by re-templating the base; tests assert the values the API actually returns.

## How to extend

- **Add a metric.** Derive `class Foo : public StringMetric<Foo>`, befriend the
  base, implement the two-argument and one-argument `_distance` / `_similarity` and
  `_name` / `_description`. Add the standard headers it needs at the top (the
  family lists each include with a `// for ...` note). No serialise.
- **Always extend the smoke test.** `test/test.cc` is the only executable check.
  Add a case asserting a hand-computed value for any new metric, with a tolerance
  for the floating-point ones.

## Traps

- **Levenshtein returns a *normalized* distance**, not the raw integer. It is
  `raw / (maxCost * max(len1, len2))`. To assert the textbook distance, multiply
  back through the longer length (the test does this for kitten/sitting == 3).
- **`Counter` is a sink, not a container.** Jaccard and Sørensen-Dice feed it to
  `std::set_intersection` to tally the intersection size without materializing it.
  It needs `<algorithm>` (for `std::set_intersection`) and `<iterator>` (for
  `std::back_inserter`); both are listed explicitly rather than leaned on
  transitively.
- **The phonetic encoding is reached through the bound-string constructor**, not
  the two-argument `similarity()` (see the dispatch note above). The test verifies
  the homophone collapse through the encoder directly so it actually exercises the
  phonetic path.

## Standalone vs. Xapiand

This is a standalone extraction from
[Xapiand](https://github.com/Kronuz/Xapiand). The metric algorithms were copied
verbatim; the decoupling change was removing the persistence layer
(`serialise()` / `unserialise()` and the `length.h` / GPL `serialise-double.h`
includes) and resolving the remaining local includes (`strings.hh` and the
soundex encoder headers) against the standalone `strings` and `soundex`
libraries through CMake. Keep it reconcilable with upstream as a plain edit.
