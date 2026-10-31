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

## 🛠️ Building and testing

```sh
cmake -B build && cmake --build build && ctest --test-dir build
./build/string_similarity_demo
```

## ⚡ Performance notes

`Levenshtein` and `Jaro` (and so `Jaro_Winkler`) use bit-parallel kernels; results
are bit-for-bit what the earlier scalar code returned, except for the corrections
listed under "Behavior changes" (`test/differential.cc` compares against a frozen
copy, `test/legacy_metrics.h`, and states exactly which input classes differ).

- **Levenshtein, default costs (1, 1).** Common prefix and suffix are trimmed, then
  Myers' bit-vector algorithm advances 64 cells of the table per word operation
  (one word up to 64 characters, Hyyrö's block form beyond). Other costs use the
  weighted dynamic program, on the stack instead of the heap.
- **Jaro.** One pass over the second string; the window search for the first
  unused equal character in the first is a mask operation
  (`peq[c] & ~used & window`, lowest set bit) instead of a scan.
- **No allocation per call.** Strings of up to 64 characters keep their character
  masks on the stack; longer ones use thread-local scratch that is reused.
- **Case folding on the fly.** With `icase` the metric folds into a stack buffer
  (heap past 512 characters) inside the kernel rather than building two upper-cased
  `std::string`s in the base class.

`bench/bench.cc` compares the current code, the frozen legacy code and
[rapidfuzz-cpp](https://github.com/rapidfuzz/rapidfuzz-cpp) (fetched only by the
bench target): `cmake -B build -DSTRING_SIMILARITY_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
&& cmake --build build --target string_similarity_bench && ./build/string_similarity_bench`.
Indicative numbers (Apple Silicon, ns per call; rapidfuzz computes the standard
Jaro; the "before" Jaro rows time the old, non-standard algorithm):

| workload | before | now | rapidfuzz |
|---|---|---|---|
| Levenshtein, random words | 90-95 | 39 | 67-75 |
| Levenshtein, 300 chars | 110 us | 2.3 us | 3.2 us |
| Levenshtein, 3000 chars | 11.3 ms | 250 us | 230-250 us |
| Jaro, random words | 117 | 77 | 66 |
| Jaro, 3000 chars | 3.9 ms | 26 us | 22 us |

## ⚠️ Behavior changes

Three bugs were corrected; results change for these inputs and only these.

- **`Jaro_Winkler` computes Jaro-Winkler.** It used to return the plain Jaro score
  (its CRTP base dispatched to `Jaro`) and `name()` said so. It is now its own
  `StringMetric` (no longer a subclass of `Jaro`): similarity `jd + l * p * (1 - jd)`
  when `jd > bt`, with `l` the common prefix (at most 4). `p` (0.1) and `bt` (0.7)
  keep their meaning and bounds; both constructors validate them.
- **`Jaro` is the standard algorithm.** One pass over the second string; each
  character takes the first unused equal character of the first within
  `floor(max(|a|, |b|) / 2) - 1` positions (0 at minimum); transpositions are
  `floor(out-of-order matches / 2)`. Before, two independent greedy passes were
  combined, transpositions were `t / 2.0`, and two one-character strings equal only
  after case folding scored 0. Results differ for strings with repeated characters
  and odd out-of-order counts. Oracle: a brute-force reference in the tests and
  rapidfuzz-cpp (agreement to 1e-12 on a million random pairs).
- **`Levenshtein` with an insert/delete cost other than 1** seeded its left column
  with `i + 1` instead of `(i + 1) * cost`, giving wrong distances. Unit costs were
  unaffected. Oracle: the textbook full-matrix DP.

Other metrics were checked against brute-force oracles and match them. Known and
unchanged: `Sorensen_Dice` uses the sets of distinct bigrams (not multisets);
`PhoneticMetric`'s two-argument calls run the inner metric on the raw strings;
`Levenshtein` with both costs 0 divides 0 by 0.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) first. CI must
pass on every platform.

## 📄 License

MIT, Copyright (c) 2015-2018 Dubalu LLC. See [LICENSE](LICENSE). Extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).
