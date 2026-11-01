# Levenshtein: a banded, multi-column kernel for long strings

Measured 2026-10-31 at `string-similarity:b987cf8`. The column-count rows were measured 2026-11-01 at `string-similarity:206f76b`, which has the same kernel with the per-thread mask scratch capped.

## Change

`Levenshtein` with the default costs (1, 1) computes the exact edit distance with bit-parallel kernels. The common prefix and suffix are trimmed first, and the shorter string becomes the pattern.

- A pattern of up to 64 characters is one word (Myers' bit-vector algorithm). Its character masks are built inline on the stack (`PeqWord`, a 256-byte index and at most 64 rows) and nothing else is allocated.
- A pattern of more than 64 characters uses Hyyrö's block form. Each pass over the blocks advances `kColumns` text columns together (three on arm64, two elsewhere), so the carry chains of the columns overlap.
- A pattern of 8 or more words (449 characters) first runs inside an Ukkonen band. An alignment of cost at most `k` stays on the diagonals `j - i` in `[-s, (n - m) + s]` with `s = (k - (n - m)) / 2`, so only the blocks that band touches are computed. The first band is `STRSIM_BAND_SLACK` (63) wider than the length difference, because the public API has no distance bound. A result of at most `k` is exact. A larger one is an upper bound that sizes the second, final run. The band is skipped when the first run would touch more than half of the pattern's rows (a text much longer than the pattern), where it only repeats work.

Results are exactly what the frozen quadratic program and the previous block kernel return (`test/frozen_levenshtein.h`). Other costs use the weighted dynamic program.

## Method

- Bench: `bench/bench.cc`, target `string_similarity_bench` (`-DSTRING_SIMILARITY_BUILD_BENCH=ON`; the bench target alone fetches rapidfuzz-cpp v3.3.4), rows "long Levenshtein": the previous kernel ("before"), the library ("after") and `rapidfuzz::levenshtein_distance`, best of 5 per row. "Edits" are random substitutions, insertions and deletions; "4 letters" and "26 letters" name the alphabet.
- Mid-length rows (130 to 700 characters): a second harness with the same three arms, best of 7 calibrated runs per row.
- Words and mid-length strings (4 to 256 characters): a harness that times 4096 random pairs per length and three kinds of pair (random, one edit apart, random with `icase=true`), best of 9, built once against the previous headers and once against the library and run alternately in the same hold; the tables give the median of three rounds.
- Machines: Apple M4 Pro and Intel Core i9-9980HK (macOS 15, x86_64). Apple clang 17, `-O3`, with the machine to itself.
- Identity: every bench run first checks the library against the frozen implementations (0 mismatches).

## Results

Long strings, time per call. "before" is the previous block kernel, "rapidfuzz" is rapidfuzz-cpp.

| workload | M4 Pro before | M4 Pro after | M4 Pro rapidfuzz | i9 before | i9 after | i9 rapidfuzz |
|---|---:|---:|---:|---:|---:|---:|
| 300 chars, 4 letters | 2.35 us | 2.22 us | 3.23 us | 4.36 us | 4.06 us | 6.10 us |
| 1000 chars, 4 letters | 26.5 us | 15.1 us | 28.8 us | 38.4 us | 28.0 us | 43.6 us |
| 3000 chars, 4 letters | 254 us | 95.0 us | 249 us | 319 us | 172 us | 318 us |
| 3000 chars, 26 letters | 254 us | 132 us | 236 us | 322 us | 239 us | 307 us |
| 3000 chars, 1% substitutions | 252 us | 13.6 us | 236 us | 320 us | 24.8 us | 301 us |
| 3000 chars, 1% edits | 234 us | 14.2 us | 224 us | 312 us | 25.0 us | 292 us |
| 3000 chars, 5% edits, 4 letters | 253 us | 29.4 us | 238 us | 321 us | 51.9 us | 309 us |
| 3000 chars, 10% edits, 4 letters | 254 us | 36.4 us | 237 us | 320 us | 67.3 us | 309 us |
| 3000 vs 3100 chars, 4 letters | 262 us | 103 us | 260 us | 333 us | 187 us | 334 us |
| 10000 chars, 26 letters | 2.96 ms | 1.34 ms | 2.62 ms | 3.63 ms | 2.38 ms | 3.15 ms |
| 10000 chars, 1% edits | 2.86 ms | 90.2 us | 2.58 ms | 3.52 ms | 165 us | 3.16 ms |

Random strings gain 1.1x to 2.7x (the columns overlap) and strings within a few percent of each other 4.8x to 32x (the band). The library is ahead of rapidfuzz on every row on both machines. On random strings of 1000 characters and more it takes 0.38x to 0.56x of rapidfuzz's time on the M4 Pro and 0.54x to 0.78x on the i9.

Mid-length strings (130 to 700 characters), before over after:

| workload | M4 Pro | i9 |
|---|---:|---:|
| 130 chars, random, 4 letters | 1.07x | 1.11x |
| 400 chars, random, 4 letters | 1.10x | 1.09x |
| 400 chars, 1% edits, 26 letters (7 words, no band) | 1.01x | 1.10x |
| 500 chars, random, 4 letters | 1.08x | 1.04x |
| 500 chars, 1% edits, 26 letters (8 words, band) | 2.53x | 2.53x |
| 700 chars, random, 4 letters | 1.39x | 1.21x |
| 700 chars, 10% edits, 4 letters | 3.59x | 3.32x |

Words of up to 256 characters, random pairs over 26 letters, before and after in ns per call (median of three rounds). The M4 Pro:

| length | 4 | 8 | 12 | 16 | 32 | 64 | 65 | 128 | 129 | 256 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| before | 23.4 | 36.5 | 55.9 | 71.2 | 150.6 | 283.2 | 386.9 | 695.6 | 1004.1 | 1850.2 |
| after | 16.9 | 27.1 | 43.1 | 60.2 | 140.4 | 258.3 | 386.0 | 684.7 | 861.1 | 1722.1 |

The i9:

| length | 4 | 8 | 12 | 16 | 32 | 64 | 65 | 128 | 129 | 256 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| before | 39.8 | 60.4 | 87.1 | 117.1 | 231.6 | 388.2 | 694.1 | 1194.8 | 1589.2 | 3547.9 |
| after | 43.5 | 52.9 | 77.8 | 108.0 | 220.4 | 368.5 | 682.4 | 1185.0 | 1491.9 | 3261.7 |

Inline masks gain 1.18x to 1.38x on random pairs of up to 16 characters on the M4 Pro (1.17x to 1.28x on near-identical pairs, 1.10x to 1.23x with `icase=true`) and 1.08x to 1.14x on the i9 (1.04x to 1.09x and 1.07x to 1.14x). The i9 row at 4 characters reads 0.91x in two rounds (39.8 and 39.9 ns before, 43.5 and 43.6 after) and 1.21x in the third (39.7 and 32.8), which is inside the noise of that host.

### Columns per pass

`bench/bench.cc` built once per value of `STRSIM_COLUMNS` (`-DCMAKE_CXX_FLAGS=-DSTRSIM_COLUMNS=N`), rows "long Levenshtein", "after" column. The first column of each machine is the time with one column per pass; the others are the speedup over it (above 1 is faster). Each figure is the mean of two runs of the bench, one binary per value, all eight runs in one hold per machine. The two runs of a value differ by up to 3.4% on the M4 Pro and 8.7% on the i9, where low-priority build and test jobs of the same author ran at the same time.

| workload | M4 Pro, 1 column | 2 | 3 | 4 | i9, 1 column | 2 | 3 | 4 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 300 chars, 4 letters | 2.4 us | 0.94x | 1.06x | 1.05x | 5.1 us | 1.14x | 1.08x | 1.05x |
| 1000 chars, 4 letters | 17.9 us | 1.18x | 1.19x | 1.16x | 38.0 us | 1.24x | 1.18x | 1.15x |
| 3000 chars, 4 letters | 129.1 us | 1.36x | 1.38x | 1.30x | 223.9 us | 1.18x | 1.07x | 1.05x |
| 3000 chars, 26 letters | 191.1 us | 1.44x | 1.44x | 1.37x | 305.2 us | 1.16x | 1.04x | 1.02x |
| 3000 chars, 1% substitutions | 16.6 us | 1.14x | 1.22x | 1.31x | 36.4 us | 1.31x | 1.24x | 1.32x |
| 3000 chars, 1% edits | 16.6 us | 1.12x | 1.18x | 1.24x | 36.8 us | 1.31x | 1.29x | 1.32x |
| 3000 chars, 5% edits, 4 letters | 35.6 us | 1.11x | 1.20x | 1.21x | 76.2 us | 1.31x | 1.20x | 1.28x |
| 3000 chars, 10% edits, 4 letters | 43.0 us | 1.13x | 1.16x | 1.16x | 94.8 us | 1.27x | 1.16x | 1.21x |
| 3000 vs 3100 chars, 4 letters | 139.7 us | 1.38x | 1.35x | 1.30x | 235.2 us | 1.14x | 1.02x | 1.03x |
| 10000 chars, 26 letters | 2.11 ms | 1.59x | 1.58x | 1.50x | 2.86 ms | 1.09x | 0.99x | 0.96x |
| 10000 chars, 1% edits | 111.8 us | 1.15x | 1.23x | 1.26x | 239.5 us | 1.32x | 1.26x | 1.33x |

On the M4 Pro, two columns are 6% slower than one at 300 characters (an earlier sweep over 288 to 384 characters measured 7% to 8%), three are faster than one on every row and as fast as two on the random rows, and four lose 1% to 6% to three on the random rows and gain up to 7% on the banded rows. On the i9, two columns are the best or tied on every row; three and four fall 5% to 12% behind them on the random rows, and three read 0.99x of one column at 10000 characters. Three columns on arm64 (31 general-purpose registers) and two elsewhere (16) are the choices that never lose to one column. `kColumns` is set in `bitparallel.h` and `STRSIM_COLUMNS` overrides it.

## Proof

- `test/levenshtein_long.cc` (CTest `string_similarity_levenshtein`) checks the kernel against a textbook dynamic program that is itself checked against `test/oracle.h`, and against the frozen previous kernel, at lengths 63, 64, 65, 127, 128, 129, 1000, 3000 and 10000 on random, near-identical and off-diagonal pairs (a prefix of one inside the other, a run of up to 300 characters deleted from the middle), and drives the band directly: every distance bound and every first-band width, comparing the exact result when it is at most the bound and the upper-bound property above it.
- `test/differential.cc` and `test/correctness.cc` compare `Levenshtein` with the frozen implementation and the oracles on short and weighted inputs.
- `tools/mutants.py` breaks the kernel in 24 ways (the carry of the bit-vector step, the edge deltas, the score row, the band's edges and bound, the retry bound, the trimming, the mask table) and requires `string_similarity_levenshtein`, `differential` or `correctness` to fail on each. Mutants whose only effect is to move work between the band and the full table are equivalent and are not listed.

## Decision and open items

- The band starts at 8 words. Below that the whole table is run: the 400-character rows gain 1.0x to 1.1x, the 500-character rows with a few edits gain 2.5x.
- A pattern of two or more words is out of line (`levenshtein_blocks`, `STRSIM_NOINLINE`). Inlined into the one-word path, it makes a 16 to 32 character near-identical word about 5% slower on arm64.
- `STRSIM_BAND_SLACK` and `STRSIM_BAND_MIN_BLOCKS` change speed, never a result.
- Random 4-letter strings of 300 to 700 characters gain 1.04x to 1.4x; the whole table is needed there, so the band has nothing to save. The public API has no distance bound; a thresholded call could start with a narrower band. That is not measured.
