# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
basic_string_metric.h        CRTP base StringMetric<Impl> + the Counter helper. The shared surface. Header.
bitparallel.h                Internal bit-parallel kernels (Myers/Hyyro edit distance, banded and several columns per pass), mask tables (`PeqWord` for one word, `Peq` for more and for Jaro), on-the-fly case folding.
levenshtein.h                Edit distance, normalized by the longer length.
jaro.h                       Jaro similarity.
jaro_winkler.h               Jaro with a shared-prefix boost; uses `Jaro::jaro`.
lcsubstr.h                   Longest common substring.
lcsubsequence.h              Longest common subsequence.
jaccard.h                    Jaccard index over character sets.
sorensen_dice.h              Sørensen-Dice over character bigrams.
phonetic_metric.h             PhoneticMetric<Encoder, Metric>: encode phonetically, score the codes.
test/test.cc                 Runnable smoke test: asserts known values for every metric.
test/correctness.cc          Every metric against the textbook oracles in test/oracle.h.
test/differential.cc         Bit-for-bit comparison of Levenshtein/Jaro/Jaro_Winkler against test/legacy_metrics.h (frozen pre-bit-parallel code; never edit it).
test/levenshtein_long.cc     The banded Levenshtein kernel at lengths 63..10000 against a textbook DP, test/frozen_levenshtein.h (the previous full-table kernel; never edit it) and a direct drive of the band.
test/rapidfuzz_check.cc      Jaro/Jaro_Winkler against rapidfuzz-cpp (bench option only).
bench/bench.cc               Benchmark vs legacy and rapidfuzz-cpp (optional CMake target; comparator fetched only there).
examples/demo.cc             A runnable tour (not a test).
CMakeLists.txt               INTERFACE library `string_similarity` (+ alias); FetchContent strings + soundex; CTest tests `string_similarity`, `_differential`, `_correctness`, `_levenshtein`.
LICENSE                      MIT, Copyright (c) 2026 Germán Méndez Bravo and (c) 2015-2020 Dubalu LLC.
CHANGELOG.md                 Behavior and performance changes, dated.
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
(FetchContent, `GIT_TAG main`). The smoke test prints
`all string-metrics tests passed (N checks)` and exits 0. CTest runs four
targets: `string_similarity` (`test/test.cc`), `string_similarity_differential`,
`string_similarity_correctness` and `string_similarity_levenshtein`. The tests and
demo are only added when this repo is the top-level project (CMakeLists.txt), so
consumers vendoring it via `FetchContent` won't build them.

## Dependencies

Two siblings in the same family, both linked `INTERFACE`:

- **[strings](https://github.com/Kronuz/strings)**: `basic_string_metric.h`
  includes `"strings.hh"` for `strings::upper` (the case-folding step). Part of
  the public surface, so it must ride onto the consumer's include path.
- **[soundex](https://github.com/Kronuz/soundex)**: `phonetic_metric.h` is a
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

- **No persistence.** The library omits Xapiand's `serialise()` / `unserialise()`
  methods and their includes (`length.h`, the GPL
  `xapian/common/serialise-double.h`); the metrics are pure algorithms. Don't
  reintroduce them here. A Xapiand that uses this library keeps its own serialise
  adapter on top. Do not
  call `_soundex.serialise()` either; the soundex library has no serialise.
- **The CRTP base owns the base cases and case folding.** Identical strings
  short-circuit to `0` / `1`, an empty operand to `1` / `0`, and `_icase` folds
  both operands with `strings::upper` before any subclass `_distance` /
  `_similarity` runs. A subclass kernel sees normalized, non-trivial input; keep it
  that way rather than re-checking inside a metric.
- **Subclasses are befriended.** Each metric declares
  `friend class StringMetric<Impl>;` (Jaro_Winkler is its own `StringMetric<Jaro_Winkler>` and calls the static `Jaro::jaro`) so the
  base can call its private kernels. Keep the friend declaration when adding a
  metric.
- **Optimized kernels fold case themselves.** `Levenshtein` and `Jaro` define `_icase_distance` / `_icase_similarity`; the base calls those (detected with `requires`) instead of upper-casing copies. Output must stay identical to `test/legacy_metrics.h` except for the input classes listed at the top of `test/differential.cc` (Levenshtein insert/delete cost != 1 or both costs 0, Jaro matching, Jaro_Winkler), where the legacy result is a known bug (see `CHANGELOG.md`); everywhere else the test enforces equality. `test/correctness.cc` checks every metric against the textbook oracles in `test/oracle.h`; `test/rapidfuzz_check.cc` (bench option) checks Jaro/Jaro_Winkler against rapidfuzz.
- **The banded kernel's result is exact only within its bound.** `levenshtein_band(..., k, exact)` returns the distance when it is at most `k` (or when `exact` is set) and an upper bound above `k` otherwise; `levenshtein_unit` relies on that to retry with `k = result`. Pessimistic starts (a block entering below the band begins with every vertical delta +1, the first active block's incoming delta is +1) are what keep the table an over-estimate; do not "improve" them to anything below the true value. Do not index blocks outside `[first, last]`, and keep `score` tied to the last active block.
- **`levenshtein_unit`'s one-word path stays small.** The multi-word path is `STRSIM_NOINLINE` (`levenshtein_blocks`): inlined, it made a 16-32 character near-identical word about 5% slower on arm64. `kColumns` is 3 on arm64 and 2 elsewhere (measured: on M4 two is 7-8% slower than one at 288-384 characters, three is faster everywhere; on x86-64 three and four are slower than two); initialize per-column arrays element by element (a loop became a `memset_pattern16` call per group).
- **The dispatch is fixed at the StringMetric<...> layer.** A derived
  metric must not rely on the base's CRTP dispatch reaching its code: `Jaro_Winkler`
  names itself in `StringMetric<...>`, and `PhoneticMetric` (derived from its inner
  `Metric`) declares its own `distance` / `similarity` that encode, then call the inner metric.

## How to extend

- **Add a metric.** Derive `class Foo : public StringMetric<Foo>`, befriend the
  base, implement the two-argument and one-argument `_distance` / `_similarity` and
  `_name` / `_description`. Add the standard headers it needs at the top (the
  family lists each include with a `// for ...` note). No serialise.
- **Always extend the tests.** Add a case to `test/test.cc` asserting a
  hand-computed value for any new metric, with a tolerance for the floating-point
  ones, and an oracle comparison to `test/correctness.cc`.

## Traps

- **Levenshtein returns a *normalized* distance**, not the raw integer. It is
  `raw / (maxCost * max(len1, len2))`. To assert the textbook distance, multiply
  back through the longer length (the test does this for kitten/sitting == 3).
- **`Counter` is a sink, not a container.** Jaccard and Sørensen-Dice feed it to
  `std::set_intersection` to tally the intersection size without materializing it.
  It needs `<algorithm>` (for `std::set_intersection`) and `<iterator>` (for
  `std::back_inserter`); both are listed explicitly rather than leaned on
  transitively.
- **`PhoneticMetric` hides the base `distance` / `similarity`.** Its own
  overloads encode the operands; the bound-string constructor stores the encoded
  form. A call that reaches `StringMetric<Metric>` directly compares raw strings.
  `test/correctness.cc` checks both forms against encode-then-compare.

## Standalone vs. Xapiand

This is a standalone extraction from
[Xapiand](https://github.com/Kronuz/Xapiand). It differs from upstream in three
ways: it has no persistence layer (`serialise()` / `unserialise()` and the
`length.h` / GPL `serialise-double.h` includes), it resolves its local includes
(`strings.hh` and the soundex encoder headers) against the standalone `strings`
and `soundex` libraries through CMake, and it carries the kernels and fixes in
`CHANGELOG.md`. Keep it reconcilable with upstream as a plain edit.
