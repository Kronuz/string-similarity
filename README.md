# string-similarity

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/string-similarity/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/string-similarity/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

String similarity metrics for C++20 behind one interface: Levenshtein, Jaro and
Jaro-Winkler, longest common substring and subsequence, Jaccard, Sørensen-Dice,
and a phonetic wrapper, each scoring in `[0, 1]`. For fuzzy matching,
suggestions, and deduplication.

![string-similarity: a pair of strings through any metric to a score between 0 and 1](docs/string-similarity.svg)

## ✨ Highlights

- **Seven metrics, one interface.** Each has `similarity()` and `distance()` in
  `[0, 1]`, plus `name()` and `description()`.
- **Compare many against one.** Bind one side in the constructor and score others
  against it.
- **Sounds-alike matching.** `PhoneticMetric<SoundexEnglish, Levenshtein>`
  compares Soundex codes, so "Robert" and "Rupert" match.
- **Case-insensitive by default**, with a flag to turn it off.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer; it fetches
[strings](https://github.com/Kronuz/strings) and
[soundex](https://github.com/Kronuz/soundex). CI builds it on Linux (x86_64,
arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(string_similarity
  GIT_REPOSITORY https://github.com/Kronuz/string-similarity.git
  GIT_TAG        main)
FetchContent_MakeAvailable(string_similarity)

target_link_libraries(my_app PRIVATE string_similarity::string_similarity)
```

## 🚀 Quick start

```cpp
#include "levenshtein.h"
#include "jaro_winkler.h"
#include "phonetic_metric.h"
#include "english_soundex.h"

#include <cstdio>
#include <string>

int main() {
	Levenshtein lev;
	std::printf("%.3f\n", lev.similarity(std::string("kitten"), std::string("sitting")));   // 1 - 3/7

	Jaro_Winkler jw;
	std::printf("%.3f\n", jw.similarity(std::string("MARTHA"), std::string("MARHTA")));

	PhoneticMetric<SoundexEnglish, Levenshtein> sounds_like("Robert");
	std::printf("%.3f\n", sounds_like.similarity(std::string("Rupert")));      // same Soundex code
}
```

[`examples/demo.cc`](examples/demo.cc) prints every metric for several pairs.

## 🎯 When to use it

- ✅ Ranking candidate spellings, matching names, or flagging near-duplicates.
- ❌ Searching a large corpus for close matches: these compare one pair at a
  time; use an index (n-grams, BK-trees) to find candidates first.
- ❌ Unicode-aware comparison: metrics and case folding work byte by byte.

## 📚 Documentation

- [docs/API.md](docs/API.md): each metric and how to call it.
- [ARCHITECTURE.md](ARCHITECTURE.md): the CRTP base and how each metric
  normalizes.
- [AGENTS.md](AGENTS.md): repository map, invariants, and traps.
- [CHANGELOG.md](CHANGELOG.md): what changed, and when.

## 🛠️ Building and testing

```sh
cmake -B build && cmake --build build && ctest --test-dir build
./build/string_similarity_demo
```

The tests check every metric against textbook oracles, the frozen earlier
implementations (bit for bit, except in the input classes the changelog lists) and
[rapidfuzz-cpp](https://github.com/rapidfuzz/rapidfuzz-cpp), which the configure step
fetches for the cross-check; `-DSTRING_SIMILARITY_RAPIDFUZZ_CHECK=OFF` skips it.

## ⚡ Performance

`Levenshtein`, `Jaro` (and so `Jaro_Winkler`) and `Sorensen_Dice` use bit-parallel or
table-driven kernels.

- **Levenshtein, default costs (1, 1).** The common prefix and suffix are
  trimmed, then Myers' bit-vector algorithm advances 64 cells of the table per
  word operation: one word up to 64 characters, Hyyrö's block form beyond.
  Past one word the kernel advances two text columns per pass over the blocks
  (three on arm64), so the carry chains of the columns overlap. For more than 448
  characters (8 words or more) it runs inside an Ukkonen band first: an
  alignment of cost at most `k` stays within `k` diagonals of the
  corner-to-corner diagonal, so only the blocks that band touches are computed.
  The public API has no distance bound, so the first band is 63 wider than the
  length difference; when the distance is larger, the first result (an upper
  bound) sizes the second and final run. Results are exact either way. Other
  costs use the weighted dynamic program, kept on the stack.
- **Jaro.** The common prefix is stripped, then one pass over the second string.
  The first unused equal character of the first string inside the window is a
  mask operation (`row[c] & ~used & window`, lowest set bit). Up to 64 characters
  the masks and the used set are single words; beyond that each character of the
  second string starts at the first word of its row that can still hold a
  candidate, so the scan stays at about one word per character.
- **Sørensen-Dice.** The bigrams of the shorter string are counted in a per-thread
  table of 65536 counts, the longer string consumes them, and the shorter one
  clears them again: no sorting and no allocation.
- **No allocation per call.** Strings of up to 64 characters build their character
  masks inline on the stack or in a small thread-local table; longer ones use
  thread-local scratch that is reused and capped at 1 MiB per thread (a request
  above the cap takes a block for that call alone).
- **Case folding on the fly.** With `icase`, the metric folds into a stack buffer
  (heap past 512 characters) inside the kernel.

Times per call, best of two runs of `bench/bench.cc`, `icase=false`. Rapidfuzz
computes the standard Jaro and Levenshtein; "ratio" is string-similarity over
rapidfuzz, so below 1 is faster. "Edits" are random substitutions, insertions and
deletions. The Jaro rows with edits or random bytes run over hundreds of distinct
pairs; the rows marked "one pair" repeat a single pair, which a branch predictor
learns, so they flatter both libraries.

| workload | Apple M4 Pro | | | Intel i9-9980HK | | |
|---|---|---|---|---|---|---|
| | string-similarity | [rapidfuzz-cpp](https://github.com/rapidfuzz/rapidfuzz-cpp) | ratio | string-similarity | rapidfuzz-cpp | ratio |
| Levenshtein, random words | 35 ns | 76 ns | 0.46 | 64 ns | 96 ns | 0.66 |
| Levenshtein, near words | 30 ns | 50 ns | 0.59 | 49 ns | 79 ns | 0.63 |
| Levenshtein, 300 chars, 4 letters | 2.2 us | 3.2 us | 0.68 | 4.1 us | 6.3 us | 0.65 |
| Levenshtein, 3000 chars, 4 letters | 93 us | 246 us | 0.38 | 173 us | 330 us | 0.52 |
| Levenshtein, 3000 chars, 26 letters | 135 us | 237 us | 0.57 | 247 us | 323 us | 0.77 |
| Levenshtein, 3000 chars, 1% edits | 13.9 us | 223 us | 0.06 | 25.9 us | 306 us | 0.08 |
| Levenshtein, 3000 chars, 10% edits | 36.6 us | 239 us | 0.15 | 68.8 us | 319 us | 0.22 |
| Levenshtein, 10000 chars, 26 letters | 1.34 ms | 2.64 ms | 0.51 | 2.43 ms | 3.27 ms | 0.74 |
| Levenshtein, 10000 chars, 1% edits | 91 us | 2.60 ms | 0.03 | 167 us | 3.21 ms | 0.05 |
| Jaro, random words | 32 ns | 67 ns | 0.48 | 57 ns | 95 ns | 0.60 |
| Jaro_Winkler, near words | 34 ns | 63 ns | 0.54 | 66 ns | 94 ns | 0.70 |
| Jaro, 20-40 chars, 2 edits | 61 ns | 105 ns | 0.58 | 135 ns | 148 ns | 0.91 |
| Jaro, 64 chars, 1 edit | 85 ns | 136 ns | 0.63 | 163 ns | 186 ns | 0.87 |
| Jaro, 65 chars, 1 edit | 94 ns | 141 ns | 0.67 | 173 ns | 201 ns | 0.86 |
| Jaro, 129 chars, 1 edit | 295 ns | 378 ns | 0.78 | 568 ns | 876 ns | 0.65 |
| Jaro, 300 chars, 3 edits | 1.1 us | 1.3 us | 0.83 | 2.2 us | 3.3 us | 0.67 |
| Jaro, 300 chars, 4 letters (one pair) | 1.2 us | 1.6 us | 0.75 | 2.4 us | 4.2 us | 0.58 |
| Jaro, 300 chars, random bytes of 128 values | 2.6 us | 2.3 us | **1.12** | 4.3 us | 5.5 us | 0.79 |
| Jaro, 3000 chars, 4 letters (one pair) | 11.7 us | 23.3 us | 0.50 | 22.8 us | 68.0 us | 0.34 |

Rapidfuzz is ahead in one workload: Jaro on 300 unrelated characters drawn from 128
byte values on Apple M4 (12% slower for `Jaro`, 19% for `Jaro_Winkler`; both are
ahead on the i9, and every other row is ahead on both machines). Rapidfuzz has no
Sørensen-Dice; on Apple M4 the Dice kernel takes 19 ns for two random words (it
took 372 ns) and 46 ns for 20-40 characters (it took 2.2 us), and
[CHANGELOG.md](CHANGELOG.md) has the before and after numbers.

```sh
cmake -B build -DSTRING_SIMILARITY_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target string_similarity_bench && ./build/string_similarity_bench
```

The bench target alone fetches rapidfuzz-cpp. Numbers move by a few percent with
code layout and machine load; compare two builds in one run, as the A/A rerun row
of the bench does. [CHANGELOG.md](CHANGELOG.md) has the changes to results and
speed, with before and after numbers.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) first. CI must
pass on every platform.

## 📄 License

MIT, Copyright (c) 2015-2018 Dubalu LLC. See [LICENSE](LICENSE). Extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).
