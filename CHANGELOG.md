# Changelog

All notable changes to this library. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The library has no
release tags yet, so entries are dated.

## 2026-10-31

### Fixed

- **`Jaro_Winkler` computes Jaro-Winkler.** It returned the plain Jaro score (its
  CRTP base dispatched to `Jaro`) and `name()` said so. It is its own
  `StringMetric` (no longer a subclass of `Jaro`): similarity
  `jd + l * p * (1 - jd)` when `jd > bt`, with `l` the common prefix (at most 4).
  `p` (0.1) and `bt` (0.7) keep their meaning and bounds; both constructors
  validate them.
- **`Jaro` is the standard algorithm.** One pass over the second string; each
  character takes the first unused equal character of the first within
  `floor(max(|a|, |b|) / 2) - 1` positions (0 at minimum); transpositions are
  `floor(out-of-order matches / 2)`. Before, two independent greedy passes were
  combined, transpositions were `t / 2.0`, and two one-character strings equal
  only after case folding scored 0. Results differ for strings with repeated
  characters and odd out-of-order counts. Oracle: a brute-force reference in the
  tests and rapidfuzz-cpp (agreement to 1e-12 on a million random pairs).
- **`Levenshtein` with an insert/delete cost other than 1** seeded its left
  column with `i + 1` instead of `(i + 1) * cost`, giving wrong distances. Unit
  costs were unaffected. Oracle: the textbook full-matrix DP.
- **`PhoneticMetric`** encodes both operands (and the comparand of the
  bound-string form) before the inner metric runs. Its two-argument and
  one-argument calls compared the raw strings. Oracle: encode with the encoder,
  then compare the codes.
- **`Levenshtein` with both costs 0** returns distance 0 (similarity 1) instead
  of 0 / 0 = NaN: every edit is free.
- **`Sorensen_Dice`** counts bigrams as a multiset, the standard definition
  `2 * |shared| / (|b1| + |b2|)`. It used distinct bigrams, so repeated bigrams
  ("aaaa") were undercounted.

The other metrics were checked against brute-force oracles and match them.

## 2026-10-30

### Added

- `test/differential.cc` compares `Levenshtein`, `Jaro` and `Jaro_Winkler` bit for
  bit against a frozen copy of the earlier scalar code, `test/legacy_metrics.h`,
  and names the input classes where the results differ (the fixes above).
- `bench/bench.cc` times the library against the frozen legacy code and
  [rapidfuzz-cpp](https://github.com/rapidfuzz/rapidfuzz-cpp).

### Changed

- **Bit-parallel kernels for `Levenshtein` and `Jaro`** (and so `Jaro_Winkler`).
  Results are bit for bit what the earlier scalar code returned, except for the
  fixes listed under 2026-10-31.
  - Levenshtein with default costs trims the common prefix and suffix, then runs
    Myers' bit-vector algorithm (Hyyrö's block form past 64 characters). Other
    costs run the weighted dynamic program on the stack instead of the heap.
  - Jaro finds the first unused equal character with a mask operation
    (`peq[c] & ~used & window`, lowest set bit) instead of a scan.
  - No allocation per call: masks for strings up to 64 characters live on the
    stack, longer ones in reused thread-local scratch.
  - With `icase`, the kernels fold case on the fly into a stack buffer (heap past
    512 characters) instead of the base class building two upper-cased
    `std::string`s.

  Apple Silicon, ns per call (`bench/bench.cc`; rapidfuzz computes the standard
  Jaro; the "before" Jaro rows time the old, non-standard algorithm):

  | workload | before | after | rapidfuzz |
  |---|---|---|---|
  | Levenshtein, random words | 90-95 | 39 | 67-75 |
  | Levenshtein, 300 chars | 110 us | 2.3 us | 3.2 us |
  | Levenshtein, 3000 chars | 11.3 ms | 250 us | 230-250 us |
  | Jaro, random words | 117 | 77 | 66 |
  | Jaro, 3000 chars | 3.9 ms | 26 us | 22 us |
