# Changelog

All notable changes to this library. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The library has no
release tags yet, so entries are dated.

## 2026-10-31

### Added

- Tests: `test/copy_move.cc` (copies and moves of every bound metric, from non-const
  and const lvalues and rvalues, through vector regrowth), `test/scratch.cc` (the
  scratch cap, built with the default cap and with 256 words), `test/threads.cc`
  (eight threads against the oracles), `test/api_doc.cc` (the snippet of
  `docs/API.md`), `test/frozen_dice.h` (the previous Sørensen-Dice, for the
  differential test). `test/correctness.cc` checks that `icase=true` equals
  fold-then-compare for every metric, `PhoneticMetric` over every inner metric, and
  Jaro-Winkler at `jd == bt`.
- `test/rapidfuzz_check.cc` is a CTest test (`string_similarity_rapidfuzz_check`;
  `-DSTRING_SIMILARITY_RAPIDFUZZ_CHECK=OFF` skips it and the fetch of rapidfuzz-cpp). It
  also checks Levenshtein, case-sensitive and bound Jaro and Jaro-Winkler, and both
  argument orders.
- `STRSIM_MAX_SCRATCH_WORDS` (compile time) sets the per-thread scratch cap.
- `test/levenshtein_long.cc` (CTest `string_similarity_levenshtein`) checks the
  long-string Levenshtein kernel against a textbook dynamic program and the frozen
  previous kernel, `test/frozen_levenshtein.h`, at lengths 63, 64, 65, 127, 128, 129,
  1000, 3000 and 10000 on random, near-identical and off-diagonal inputs, and drives
  the band directly (every distance bound, every first-band width).
- `bench/bench.cc` has long Levenshtein rows against the previous kernel and
  rapidfuzz-cpp.

### Changed

- **`Sorensen_Dice` counts the bigrams of a pair in a per-thread table of 65536
  counts** (one per pair of bytes: the shorter string is counted into it, the longer
  one consumes the counts, the shorter one clears them) instead of sorting vectors of
  `string_view`s: no sorting and no allocation per call, also with `icase` (the case
  folding runs inside the kernel, like for the other metrics). The metric keeps no
  state beyond its bound string. Results are bit for bit the previous ones, except the
  class listed under Fixed.
- **The per-thread scratch is capped** at 1 MiB (`kMaxScratchWords`; a thread used to
  keep the largest request it had ever seen, per slot: two 1 MiB strings through
  `Jaro` kept about 100 MB per thread). A call whose masks need more takes a block of
  its own and frees it on return. Results are unchanged.
- **CI, ASan and UBSan job:** built with `-O1`, and the randomized tests run fewer
  repetitions under a sanitizer, over the same lengths and alphabets
  (`test/sanitizer.h`; the rapidfuzz cross-check runs 40,000 pairs there and a million
  in the other jobs). The job took 39m56s of its 60 minutes.
- Soundex and strings stay pinned by commit (strings `485f882`, soundex `4f192a7`).

- **Levenshtein, patterns longer than 64 characters: banded, several columns per pass.**
  Results are exactly what the previous kernel returned (checked against it and the
  dynamic program). Two text columns (three on arm64) advance per pass over the
  blocks, so their carry chains overlap (1.3-2.7x on random strings), and a pattern
  of 8 or more words runs inside an Ukkonen band first, sized by the length
  difference plus 63; a distance above the band's bound is found by a second run
  whose bound is the first run's upper bound. Near-identical strings cost the band,
  not the table. A band that would touch more than half of the pattern's rows (a
  much longer text) is skipped.
- **Levenshtein, words of up to 64 characters** build their character masks inline
  on the stack (`PeqWord`, a 256 byte index) instead of through the general `Peq`
  constructor: 1.2-1.4x faster on Apple M4 and 1.05-1.2x on an Intel i9 for words up
  to 16 characters, results unchanged.

  `Levenshtein(false).distance`, per call, `bench/bench.cc` on an otherwise idle
  machine ("before" is the previous kernel behind the same equality check and
  normalization; M4: Apple M4 Pro, i9: Intel i9-9980HK, both Apple clang 17):

  | workload | M4 before | M4 after | M4 rapidfuzz | i9 before | i9 after | i9 rapidfuzz |
  |---|---|---|---|---|---|---|
  | 300 chars, 4 letters | 2.3 us | 2.2 us | 3.2 us | 4.4 us | 4.1 us | 6.1 us |
  | 3000 chars, 4 letters | 254 us | 95 us | 249 us | 319 us | 172 us | 318 us |
  | 3000 chars, 26 letters | 254 us | 132 us | 236 us | 322 us | 239 us | 307 us |
  | 3000 chars, 1% substitutions | 252 us | 13.6 us | 236 us | 320 us | 24.8 us | 301 us |
  | 3000 chars, 10% edits | 254 us | 36 us | 237 us | 320 us | 67 us | 309 us |
  | 10000 chars, 1% edits | 2.86 ms | 90 us | 2.58 ms | 3.52 ms | 165 us | 3.16 ms |

### Fixed

- **`PhoneticMetric<Encoder, Sorensen_Dice>` in the bound form scored 0 for every pair
  whose codes differ.** The wrapper assigns the encoded string to the inner metric after
  building it, and `Sorensen_Dice` had computed its bigrams in its own constructor: it
  intersected the codes with an empty set. `Sorensen_Dice` keeps no bigrams now.
  Oracle: encode with the encoder, then the multiset Dice of the codes
  (`PhoneticMetric<SoundexEnglish, Sorensen_Dice>("Robert").similarity("Rubin")` is
  8/11, it was 0); the test covers all four inner metrics, both forms.
- **A copy or move of a bound `Sorensen_Dice` pointed into its source.** Its bigrams
  were `string_view`s into `_str`: a copy kept scoring against the source's string
  (a use after free once the source was gone, wrong scores once it was overwritten).
  Same root as the item above.
- **A non-const lvalue copy of a bound metric did not compile**
  (`Levenshtein b = a;`): the constructor that binds a string beat the copy
  constructor. The binding constructors of every metric (`Levenshtein`, `Jaro`,
  `Jaro_Winkler`, `Sorensen_Dice`, `Jaccard`, `LCSubsequence`, `LCSubstr`,
  `PhoneticMetric`) exclude their own type and its derived classes.
- **`Sorensen_Dice` with `icase=true` scored 0 for two one-character strings that are
  equal after folding** (`a` and `A`; the bound form compared its folded string with the
  raw argument). Oracle: `icase=true` means fold both operands, then compare
  case-sensitively, and identical strings score 1; the test checks that equivalence for
  every metric, two-argument and bound. Only this class changes; the differential
  test lists it.
- `docs/API.md`: the usage snippet compiles (`std::string` operands, the includes it
  needs); `test/api_doc.cc` builds it.
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
