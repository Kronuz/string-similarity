# Architecture

The internals of `string-similarity`: how one CRTP base gives every metric a shared
surface, what each metric computes, and the one subtlety in how the base
dispatches to subclasses. For usage see `README.md`; for the repo map and
invariants see `AGENTS.md`.

## Shape

A base header plus one header per metric:

```
  basic_string_metric.h   StringMetric<Impl> (the CRTP base) + the Counter helper
  levenshtein.h           edit distance
  jaro.h / jaro_winkler.h Jaro and its prefix-boosted variant
  lcsubstr.h              longest common substring
  lcsubsequence.h         longest common subsequence
  jaccard.h               Jaccard index over character sets
  sorensen_dice.h         Sørensen-Dice over character bigrams
  phonetic_metric.h        PhoneticMetric<Encoder, Metric>: phonetic wrapper
```

Everything is templates and inline methods, so there is no compiled translation
unit. The headers carry their original Xapiand names.

## The CRTP base

`StringMetric<Impl>` holds the two pieces of shared state: an `_icase` flag and an
optional bound string `_str`. It provides the public `distance()` and
`similarity()` overloads (a two-argument form, and a one-argument form that
compares against `_str`), plus `name()` and `description()`. Each public call does
the base-case checks once (identical strings short-circuit to `0` / `1`, an empty
operand to `1` / `0`), then forwards to the subclass through a `static_cast<const
Impl*>(this)`, calling `_distance` / `_similarity` / `_name` / `_description`.
`_icase` case-folds both operands with `strings::upper` before the subclass sees
them, so each metric implements only the math, on already-normalized input.
`Levenshtein`, `Jaro`, `Jaro_Winkler` and `Sorensen_Dice` define `_icase_distance` /
`_icase_similarity`; the base detects them with `requires` and lets those kernels fold
case on the fly instead. `icase=true` means "fold both operands, then compare": for a
bound metric the stored string is folded once and only the comparand is folded per call.

The subclasses are befriended (`friend class StringMetric<Impl>;`) so the base can
reach their private `_distance` / `_similarity`. Each metric defines those two
plus their one-argument forms, and `_name` / `_description`.

## What each metric computes

- **Levenshtein** computes the edit distance with Myers' bit-parallel algorithm
  for unit costs (`bitparallel.h`) and a weighted two-column DP for other costs,
  and returns it *normalized*: `raw / (maxCost * max(len1, len2))`. With unit costs that is
  `raw / max(len1, len2)`, so the test recovers the textbook integer distance by
  multiplying back through the longer length.
- **Jaro** strips the common prefix of the two strings (it matches itself position by
  position and holds no transposition, so the rest is matched on its own with the
  window bound of the full lengths), then matches characters within a sliding window in
  one pass over the second string with bit masks, counts transpositions, and combines
  them into the standard Jaro formula. A remainder of up to 64 characters on both sides
  is matched in registers (a 256-entry thread-local table of masks, zero between
  calls; the window mask moves up one bit per character, the matched positions of both
  strings are words). A longer one keeps the mask rows of the first string in a direct
  table (256 rows of up to 64 words, `DirectRows`, zero between calls, so a lookup is
  an offset) or, above 64 words, in the scratch `Peq`; each character of the second
  string starts its scan at the first word of its row that can still hold a candidate
  (a per-call cursor, one store per character), which keeps the scan at about one word
  per character. **Jaro_Winkler**
  is its own metric over `Jaro::jaro` and adds the shared-prefix boost (up to four
  characters, scaled by `p`, applied only above a boost threshold `bt`).
- **LCSubstr** and **LCSubsequence** each run a two-row DP (contiguous run length
  for the former, classic LCS for the latter) and divide by `max(len1, len2)` to
  get a similarity in `[0, 1]`.
- **Jaccard** builds a `std::set<char>` of each string and returns
  `|intersection| / |union|`, counting the intersection with the `Counter` helper, a
  minimal output-iterator sink for `std::set_intersection` that just tallies `push_back`
  calls instead of materializing the intersection.
- **Sorensen_Dice** works over the strings' 2-character substrings (bigrams) as
  multisets: `2 * |shared| / (|b1| + |b2|)`. A bigram is the 16 bits of its two bytes;
  a per-thread table of 65536 counts (zero between calls, 256 KiB of zero pages) takes
  the bigrams of the shorter string, the longer one consumes them (each hit is one
  shared bigram) and the shorter one clears them again: three passes, no sorting, no
  allocation. The metric keeps no state beyond its bound string, so a copy is a plain
  copy. A string of one character has no bigram: two equal ones score 1 (identical),
  anything else 0. The counts are 32 bits: a pair whose shorter string has 4 Gi
  characters or more throws `std::length_error`.
- **PhoneticMetric<Encoder, Metric>** derives from the inner `Metric` and holds a
  phonetic `Encoder`. Its `_distance` / `_similarity` encode both operands and
  defer to the inner metric over the codes; its constructor stores the encoded
  form as the bound `_str`. `name()` reports the encoder's name.

## Scratch memory

The mask tables of strings longer than 64 characters (`Peq`) live in thread-local
scratch that grows on demand up to `kMaxScratchWords` (1 MiB; `STRSIM_MAX_SCRATCH_WORDS`
overrides it at compile time): a thread never keeps more. A call whose masks need more
(about 300000 letters of 26 symbols, 32000 bytes of all 256 values) takes a block of its
own and frees it on return; results are the same. Two fixed tables sit beside the
scratch, each allocated from zero pages when a thread first uses it, so only the pages
a call touches become resident: the Sørensen-Dice count table (256 KiB) and the Jaro
rows for a first string of up to 64 words (`DirectRows`, 128 KiB). The word-sized Jaro
mask table is 2 KiB of thread-local storage.

## The Levenshtein kernel

`levenshtein_unit` in `bitparallel.h` computes the unit-cost distance. It trims the
common prefix and suffix, makes the shorter string the pattern, and picks one of
three kernels by the pattern's length `m` (in 64-bit words, `nb`):

- **`m <= 64`**: Myers' bit-vector algorithm, one word, one text character per step.
  The pattern's character masks are built inline on the stack (`PeqWord`: a 256 byte
  index of at most 64 row numbers and 65 mask words; only the index is cleared).
- **`nb` below `kBandMinBlocks` (8)**: Hyyrö's block form over the whole table
  (`levenshtein_full`).
- **`nb >= 8`** (more than 448 characters): the same blocks inside an Ukkonen band
  (`levenshtein_band`), run up to twice, unless the length difference makes the band
  touch more than half of the pattern's rows anyway (then the whole table).

*Columns per pass.* A text column's blocks form a carry chain (the horizontal delta
leaving block `w` enters block `w + 1`), so a one-column pass runs at the chain's
latency. `lev_pass` advances `kColumns` columns per visit of each block: the
columns are independent chains, the block's vertical deltas are loaded and stored
once, and the processor overlaps them (a block costs 1.0 ns instead of 1.85 on
Apple M4, 1.9 instead of 2.3 on an Intel i9). `kColumns` is 3 on arm64 (31
registers; two columns are 7-8% slower than one at 288-384 characters on Apple M4, three
are faster at every length on Apple M1 and M4) and 2 elsewhere (x86-64's 16 registers:
three and four are slower; the rows are in [docs/perf/banded-levenshtein.md](docs/perf/banded-levenshtein.md)).

*The band.* Any alignment of cost at most `k` stays on the diagonals
`j - i` in `[-s, (n - m) + s]` with `s = (k - (n - m)) / 2`: touching diagonal `d`
costs at least `|d| + |d - (n - m)|` insertions and deletions. For each group of
columns the kernel advances only the blocks of 64 rows that band touches.
A block entering below the band starts from "every vertical delta is +1" (a column
that grows by one per row) and the first active block's incoming horizontal delta is
+1; both are over-estimates, so the computed table is never below the true one, and
it equals the true one on every cell of an alignment inside the band. A result at
most `k` is therefore the exact distance, and a larger one is an upper bound.
`levenshtein_band` also reports `exact` when the band covered the whole table.

*No bound in the API.* The first run uses `k0 = (n - m) + 63`. When its result `r`
is not within `k0`, `r` (an upper bound of the distance) is the `k` of the second
run, which is therefore exact. Near-identical strings finish in the first run
(cost proportional to the band, not the table); unrelated ones pay one narrow run
(about 5% of the table at 3000 characters) and then a band of `r`.

*Score tracking.* The distance is the value at the pattern's last row of the last
column. The kernel keeps the value at the last row of the last active block, adds
the horizontal delta at that row every column, and adds the block's row count when a
new block activates.

`test/levenshtein_long.cc` checks the kernel against a textbook dynamic program and
the frozen previous kernel (`test/frozen_levenshtein.h`), drives the band with every
bound, and runs every first-band width; its mutants (narrower band, wrong start
value, wrong score rows, early accept) each fail it.

## The dispatch subtlety

The CRTP `Impl` is fixed at the layer that names `StringMetric<...>`, and the base
calls that class's kernel. A metric that derives from another metric therefore
runs the parent's kernel unless it names itself: `Jaro_Winkler` is its own
`StringMetric<Jaro_Winkler>` for this reason. `PhoneticMetric` derives from its
inner `Metric`, so it declares its own public `distance` / `similarity`, which hide
the base ones and encode the operands before calling the inner metric.

## Why this shape

The suite exists to score how alike two short strings are, by several different
notions of "alike," behind one uniform interface. The CRTP base removes the
boilerplate (base cases, case folding, the bound-string overloads) so each metric
is just its kernel. It is character-oriented: the set- and bigram-based metrics
operate on bytes, and the phonetic wrapper is the path to a sound-based, rather
than spelling-based, comparison.

## Standalone vs. Xapiand

Upstream, the base and each subclass also carry `serialise()` / `unserialise()`
methods to persist a metric's configured parameters (the icase flag, the bound
string, Levenshtein's costs, Jaro-Winkler's `p` / `bt`) into a Xapiand index.
Those need `length.h` (Xapiand's varint serializer) and a GPL-licensed Xapian
`serialise-double.h`. None of it touches the math, so this library omits the
persistence methods and both includes and keeps the pure metrics. Its only local
includes are `strings.hh` (for `strings::upper`) and, for the soundex wrapper,
the phonetic encoder headers, both resolved through CMake against their
standalone sibling libraries. Any change here should stay
reconcilable with upstream as a plain edit.
