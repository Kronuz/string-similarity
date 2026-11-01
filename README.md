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

Time per call against [rapidfuzz-cpp](https://github.com/rapidfuzz/rapidfuzz-cpp)
v3.3.4, best of two runs, `icase=false`; "edits" are random substitutions, insertions,
and deletions.

| Workload | Apple M4 Pro | rapidfuzz-cpp | Intel Core i9-9980HK | rapidfuzz-cpp |
| --- | ---: | ---: | ---: | ---: |
| Levenshtein, random words | **35 ns** | 76 ns | **64 ns** | 96 ns |
| Levenshtein, 3000 chars, 26 letters | **135 us** | 237 us | **247 us** | 323 us |
| Levenshtein, 3000 chars, 1% edits | **13.9 us** | 223 us | **25.9 us** | 306 us |
| Jaro, random words | **32 ns** | 67 ns | **57 ns** | 95 ns |
| Jaro, 300 chars, 3 edits | **1.1 us** | 1.3 us | **2.2 us** | 3.3 us |
| Jaro, 300 chars, random bytes of 128 values | 2.6 us | **2.3 us** | **4.3 us** | 5.5 us |

Apple clang 17, `-O3`, macOS 15:

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
