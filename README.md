# string-similarity

[![CI](https://github.com/Kronuz/string-similarity/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/string-similarity/actions/workflows/ci.yml)

A small, header-only C++20 suite of string-similarity metrics: edit distance,
Jaro and Jaro-Winkler, longest common substring and subsequence, Jaccard,
Sørensen-Dice, and a Soundex-backed wrapper. Extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).

## What it is

A CRTP base, `StringMetric<Impl>`, and a family of subclasses that each
implement one metric. Every metric exposes the same surface: a `distance()` and
a `similarity()` in `[0, 1]`, plus a `name()` and `description()`. You either
pass both strings to a call, or construct the metric with one fixed string and
compare others against it:

```cpp
#include "levenshtein.h"

Levenshtein lev;                                   // icase by default
lev.distance("kitten", "sitting");                 // normalized edit distance
lev.similarity("kitten", "sitting");               // 1 - distance

Levenshtein fixed("kitten");                        // bind one side
fixed.similarity("sitting");
```

The metrics:

- **Levenshtein** — edit distance, normalized by the longer string.
- **Jaro** / **Jaro_Winkler** — character-transposition similarity; Jaro-Winkler
  adds a shared-prefix boost.
- **LCSubstr** / **LCSubsequence** — longest common contiguous run, and longest
  common (not necessarily contiguous) subsequence, over the longer length.
- **Jaccard** — over the two strings' character sets.
- **Sorensen_Dice** — over their character bigrams.
- **PhoneticMetric<Encoder, Metric>** — encodes both inputs with a phonetic
  encoder (from the [soundex](https://github.com/Kronuz/soundex) library), then
  runs the inner metric over the codes, so homophones score as similar.

It is header-only: the metrics are templates and inline methods, so there is
nothing to compile or link beyond the headers. It requires C++20.

## Install

Two dependencies, both siblings in the same family and both pulled in by CMake:

- [strings](https://github.com/Kronuz/strings) — `basic_string_metric.h` uses
  `strings::upper` for case folding.
- [soundex](https://github.com/Kronuz/soundex) — `phonetic_metric.h` is
  parameterized over a phonetic encoder (e.g. `SoundexEnglish` from
  `english_soundex.h`).

With CMake `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
  string_similarity
  GIT_REPOSITORY https://github.com/Kronuz/string-similarity.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(string_similarity)

target_link_libraries(your_target PRIVATE string_similarity::string_similarity)
```

The target requests `cxx_std_20` and links `strings` and `phonetic` `INTERFACE`,
so `"strings.hh"` and the soundex headers resolve on your include path with no
extra wiring. Then:

```cpp
#include "levenshtein.h"
#include "phonetic_metric.h"
#include "english_soundex.h"   // a concrete encoder, from the phonetic library
```

The headers keep their original filenames, so a codebase that already
`#include`s them just needs this repo on its include path.

## Usage

```cpp
#include "jaro_winkler.h"
#include "jaccard.h"
#include "sorensen_dice.h"
#include "phonetic_metric.h"
#include "english_soundex.h"

Jaro jaro;
jaro.similarity("MARTHA", "MARHTA");               // ~0.944

Jaccard jac;
jac.similarity("night", "nacht");                  // 3/7, over char sets

Sorensen_Dice sd;
sd.similarity("night", "nacht");                   // 0.25, over char bigrams

// Phonetic: encode with English Soundex, score the codes with Levenshtein.
PhoneticMetric<SoundexEnglish, Levenshtein> phon("Robert");
phon.similarity("Rupert");                          // homophones share a code
```

Every metric defaults to `icase=true`, folding both sides to upper case before
comparing. Pass `false` to compare case-sensitively. `distance()` returns `0` for
identical strings and `1` when either side is empty; `similarity()` is the
mirror, `1` and `0`.

## Build & test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

The first configure fetches `strings` and `phonetic` over the network
(FetchContent, `GIT_TAG main`). The test asserts known values for each metric:
Levenshtein("kitten","sitting") recovers the classic edit distance 3, Jaro of
MARTHA/MARHTA is ~0.944, the LCS metrics, Jaccard, and Sørensen-Dice hit their
hand-computed fractions, and English Soundex collapses Robert/Rupert to one code.
It prints `all string-similarity tests passed` and exits 0.

## Examples

[`examples/demo.cc`](examples/demo.cc) is a runnable tour. A top-level CMake build
produces it next to the test:

```sh
cmake -B build && cmake --build build && ./build/string_similarity_demo
```

It prints every metric's similarity for a handful of word pairs.

## Provenance

Extracted from [Xapiand](https://github.com/Kronuz/Xapiand). The metric
algorithms were copied verbatim; the one decoupling change was removing the
persistence layer. Upstream, each metric carried `serialise()` / `unserialise()`
methods (and pulled in `length.h` and a GPL Xapian `serialise-double.h`) purely
to persist a metric's configured parameters into a Xapiand index. Those have no
bearing on the math, so they were dropped, leaving the pure metrics. See
[ARCHITECTURE.md](ARCHITECTURE.md) for the design and [AGENTS.md](AGENTS.md) for
the repo map and invariants.

## License

MIT, Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE).
