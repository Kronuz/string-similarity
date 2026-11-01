# string-similarity benchmarks

`bench/bench.cc` times each metric against [rapidfuzz-cpp](https://github.com/rapidfuzz/rapidfuzz-cpp)
v3.3.4 on Apple M4 Pro (macOS 26) and Intel Core i9-9980HK (macOS 15), Apple clang 17, `-O3`.

```sh
cmake -B build -DSTRING_SIMILARITY_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target string_similarity_bench
./build/string_similarity_bench
```

The bench target alone fetches rapidfuzz-cpp. Numbers move by a few percent with code
layout and machine load; compare two builds in one run, as the A/A rerun row of the
bench does.

Times per call, best of two runs of `bench/bench.cc`, `icase=false`. Rapidfuzz
computes the standard Jaro and Levenshtein; "ratio" is string-similarity over
rapidfuzz, so below 1 is faster. "Edits" are random substitutions, insertions and
deletions. The Jaro rows with edits or random bytes run over hundreds of distinct
pairs; the rows marked "one pair" repeat a single pair, which a branch predictor
learns, so they flatter both libraries.

| Workload | Apple M4 Pro: string-similarity | rapidfuzz-cpp | ratio | Intel Core i9-9980HK: string-similarity | rapidfuzz-cpp | ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Levenshtein, random words | **33 ns** | 74 ns | 0.45 | **64 ns** | 93 ns | 0.69 |
| Levenshtein, near words | **30 ns** | 49 ns | 0.61 | **51 ns** | 82 ns | 0.62 |
| Levenshtein, 300 chars, 4 letters | **2.2 us** | 3.2 us | 0.69 | **4.3 us** | 6.4 us | 0.67 |
| Levenshtein, 3000 chars, 4 letters | **95.4 us** | 252.8 us | 0.38 | **177.9 us** | 339.8 us | 0.52 |
| Levenshtein, 3000 chars, 26 letters | **133.1 us** | 238.3 us | 0.56 | **244.6 us** | 319.3 us | 0.77 |
| Levenshtein, 3000 chars, 1% edits | **14.0 us** | 226.0 us | 0.06 | **26.4 us** | 306.2 us | 0.09 |
| Levenshtein, 3000 chars, 10% edits | **37.1 us** | 241.0 us | 0.15 | **69.7 us** | 318.1 us | 0.22 |
| Levenshtein, 10000 chars, 26 letters | **1.35 ms** | 2.64 ms | 0.51 | **2.45 ms** | 3.27 ms | 0.75 |
| Levenshtein, 10000 chars, 1% edits | **91.6 us** | 2.62 ms | 0.04 | **169.8 us** | 3.27 ms | 0.05 |
| Jaro, random words | **31 ns** | 67 ns | 0.46 | **57 ns** | 94 ns | 0.61 |
| Jaro_Winkler, near words | **32 ns** | 64 ns | 0.51 | **65 ns** | 97 ns | 0.67 |
| Jaro, 20-40 chars, 2 edits | **60 ns** | 102 ns | 0.59 | **127 ns** | 146 ns | 0.87 |
| Jaro, 64 chars, 1 edit | **88 ns** | 134 ns | 0.65 | **149 ns** | 183 ns | 0.81 |
| Jaro, 65 chars, 1 edit | **90 ns** | 138 ns | 0.65 | **158 ns** | 198 ns | 0.80 |
| Jaro, 129 chars, 1 edit | **291 ns** | 373 ns | 0.78 | **541 ns** | 828 ns | 0.65 |
| Jaro, 300 chars, 3 edits | **1.1 us** | 1.3 us | 0.82 | **2.4 us** | 3.4 us | 0.70 |
| Jaro, 300 chars, 4 letters (one pair) | **1.2 us** | 1.6 us | 0.77 | **2.5 us** | 4.1 us | 0.60 |
| Jaro, 300 chars, random bytes of 128 values | 2.6 us | **2.4 us** | 1.10 | **4.6 us** | 5.4 us | 0.84 |
| Jaro, 3000 chars, 4 letters (one pair) | **11.9 us** | 21.8 us | 0.55 | **23.2 us** | 69.2 us | 0.34 |

Rapidfuzz is ahead in one workload: Jaro on 300 unrelated characters drawn from 128
byte values on Apple M4 Pro (10% slower for `Jaro`, 6% for `Jaro_Winkler`; both are
ahead on the i9, and every other row is ahead on both machines). Rapidfuzz has no
Sørensen-Dice; on Apple M4 Pro the Dice kernel takes 19 ns for two random words and 45 ns
for 20-40 characters.

[banded-levenshtein.md](banded-levenshtein.md) has the method and the numbers of the
long-string Levenshtein kernel; [ARCHITECTURE.md](../../ARCHITECTURE.md) describes the
kernels.
