# string-similarity

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/string-similarity/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/string-similarity/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

String similarity metrics for C++20: Levenshtein, Jaro-Winkler, LCS, Jaccard,
Sørensen-Dice, and Soundex, scored in [0, 1]. They share one interface, and
Levenshtein, Jaro, and Sørensen-Dice run on bit-parallel or table-driven kernels. Reach
for it for fuzzy matching, spelling suggestions, and deduplication.

## ✨ Highlights

- **Seven metrics, one interface.** `Levenshtein`, `Jaro`, `Jaro_Winkler`,
  `LCSubstr`, `LCSubsequence`, `Jaccard`, and `Sorensen_Dice` each have `similarity()`
  and `distance()` in `[0, 1]`, plus `name()` and `description()`.
- **Bit-parallel Levenshtein.** Myers' bit-vector algorithm, Hyyrö's block form past
  64 characters, and an Ukkonen band past 448; results are exact.
- **Reused scratch.** Strings of up to 64 characters build their masks on the stack or
  in a small thread-local table; longer ones reuse thread-local scratch capped at 1 MiB
  per thread.
- **Compare many against one.** Bind one side in the constructor and score others
  against it.
- **Sounds-alike matching.** `PhoneticMetric<SoundexEnglish, Levenshtein>` compares
  Soundex codes, so "Robert" and "Rupert" match.
- **Case-insensitive by default**, with a flag to turn it off.

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
	std::printf("%.3f\n", lev.similarity(std::string("kitten"), std::string("sitting")));   // 0.571 (1 - 3/7)

	Jaro_Winkler jw;
	std::printf("%.3f\n", jw.similarity(std::string("MARTHA"), std::string("MARHTA")));     // 0.961

	PhoneticMetric<SoundexEnglish, Levenshtein> sounds_like("Robert");
	std::printf("%.3f\n", sounds_like.similarity(std::string("Rupert")));   // 1.000, same Soundex code
}
```

[`examples/demo.cc`](examples/demo.cc) prints every metric for several pairs.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer. It brings
[strings](https://github.com/Kronuz/strings) and
[soundex](https://github.com/Kronuz/soundex) through FetchContent. CI builds and tests
it with GCC and Clang on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD,
plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(string_similarity
  GIT_REPOSITORY https://github.com/Kronuz/string-similarity.git
  GIT_TAG        03f679e0a072d3fe39082e4676d5602d252e1b63)  # or main, which tracks the tip
FetchContent_MakeAvailable(string_similarity)

target_link_libraries(my_app PRIVATE string_similarity::string_similarity)
```

Then include the metric's header, for example `#include "levenshtein.h"`.

## 🎯 When to use it

Ranking candidate spellings, matching names, or flagging near-duplicates, one pair at a
time.

Not for:

- **Searching a large corpus for close matches.** These compare one pair at a time; use
  an index (n-grams, BK-trees) to find candidates first.
- **Unicode-aware comparison.** Metrics and case folding work byte by byte.

## ⚡ Performance

Time per call to compare one pair of strings: the best of two runs of `bench/bench.cc` with
`icase=false`. *string-similarity* is `Levenshtein`, `Jaro` and `Jaro_Winkler` from this library;
*rapidfuzz-cpp* is `levenshtein_normalized_distance`, `jaro_similarity` and
`jaro_winkler_similarity` from [rapidfuzz-cpp v3.3.4](https://github.com/rapidfuzz/rapidfuzz-cpp/tree/v3.3.4),
the library most C++ code uses for these metrics. Before it times a row, the bench checks that both
return the same value on every pair (to 1e-12). *Random words* are 3 to 12 random lower-case
letters; *3000 chars, 26 letters* is one random string of 3,000 letters against another;
*1% edits* is a string against a copy with 1% random substitutions, insertions and deletions;
*random bytes of 128 values* are bytes 0x80 to 0xFF. Bold marks the faster of each pair.

| Workload | Apple M4 Pro: string-similarity | rapidfuzz-cpp | ratio | Intel Core i9-9980HK: string-similarity | rapidfuzz-cpp | ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Levenshtein, random words | **33 ns** | 74 ns | 0.45 | **64 ns** | 93 ns | 0.69 |
| Levenshtein, 3000 chars, 26 letters | **133.1 us** | 238.3 us | 0.56 | **244.6 us** | 319.3 us | 0.77 |
| Levenshtein, 3000 chars, 1% edits | **14.0 us** | 226.0 us | 0.06 | **26.4 us** | 306.2 us | 0.09 |
| Jaro, random words | **31 ns** | 67 ns | 0.46 | **57 ns** | 94 ns | 0.61 |
| Jaro, 300 chars, 3 edits | **1.1 us** | 1.3 us | 0.82 | **2.4 us** | 3.4 us | 0.70 |
| Jaro, 300 chars, random bytes of 128 values | 2.6 us | **2.4 us** | 1.10 | **4.6 us** | 5.4 us | 0.84 |

Apple clang 17, `-O3`; the M4 Pro runs macOS 26 and the i9 macOS 15:

```sh
cmake -B build -DSTRING_SIMILARITY_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target string_similarity_bench
./build/string_similarity_bench
```

[docs/perf/](docs/perf/README.md) has all 19 workloads and the banded Levenshtein
measurements.

## 📚 Documentation

- [docs/API.md](docs/API.md): each metric and how to call it.
- [ARCHITECTURE.md](ARCHITECTURE.md): the CRTP base, how each metric normalizes, and
  the kernels.
- [docs/perf/](docs/perf/README.md): the benchmark tables and method.
- [CHANGELOG.md](CHANGELOG.md): changes to results and speed.
- [AGENTS.md](AGENTS.md): repository map, invariants, and traps.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#string-similarity).

## 🛠️ Building and testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build
./build/string_similarity_demo
```

The tests check every metric against textbook oracles, frozen reference
implementations, and [rapidfuzz-cpp](https://github.com/rapidfuzz/rapidfuzz-cpp), which
the configure step fetches for the cross-check; `-DSTRING_SIMILARITY_RAPIDFUZZ_CHECK=OFF`
skips it. `-DSTRING_SIMILARITY_BUILD_BENCH=ON` builds the benchmark.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) first, and the
[catalog's AGENTS.md](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md) for
the family's rules. CI must pass on every platform.

## 📄 License

MIT; see [LICENSE](LICENSE). Extracted from [Xapiand](https://github.com/Kronuz/Xapiand).
