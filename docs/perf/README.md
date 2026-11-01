# string-similarity benchmarks

`bench/bench.cc` times each metric against [rapidfuzz-cpp](https://github.com/rapidfuzz/rapidfuzz-cpp)
v3.3.4 on Apple M4 Pro and Intel Core i9-9980HK (macOS 15), Apple clang 17, `-O3`.

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
byte values on Apple M4 Pro (12% slower for `Jaro`, 19% for `Jaro_Winkler`; both are
ahead on the i9, and every other row is ahead on both machines). Rapidfuzz has no
Sørensen-Dice; on Apple M4 Pro the Dice kernel takes 19 ns for two random words and 46 ns
for 20-40 characters.

[banded-levenshtein.md](banded-levenshtein.md) has the method and the numbers of the
long-string Levenshtein kernel; [ARCHITECTURE.md](../../ARCHITECTURE.md) describes the
kernels.
