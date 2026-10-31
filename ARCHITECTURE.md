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
`Levenshtein` and `Jaro` define `_icase_distance` / `_icase_similarity`; the base
detects them with `requires` and lets those kernels fold case on the fly instead.

The subclasses are befriended (`friend class StringMetric<Impl>;`) so the base can
reach their private `_distance` / `_similarity`. Each metric defines those two
plus their one-argument forms, and `_name` / `_description`.

## What each metric computes

- **Levenshtein** computes the edit distance with Myers' bit-parallel algorithm
  for unit costs (`bitparallel.h`) and a weighted two-column DP for other costs,
  and returns it *normalized*: `raw / (maxCost * max(len1, len2))`. With unit costs that is
  `raw / max(len1, len2)`, so the test recovers the textbook integer distance by
  multiplying back through the longer length.
- **Jaro** matches characters within a sliding window in one pass over the
  second string, using bit masks, counts transpositions, and combines them into
  the standard Jaro formula. **Jaro_Winkler**
  is its own metric over `Jaro::jaro` and adds the shared-prefix boost (up to four
  characters, scaled by `p`, applied only above a boost threshold `bt`).
- **LCSubstr** and **LCSubsequence** each run a two-row DP (contiguous run length
  for the former, classic LCS for the latter) and divide by `max(len1, len2)` to
  get a similarity in `[0, 1]`.
- **Jaccard** builds a `std::set<char>` of each string and returns
  `|intersection| / |union|`. **Sorensen_Dice** works over the strings'
  2-character substrings (bigrams) as multisets: `2 * |shared| / (|b1| + |b2|)`. Both count the
  intersection with the `Counter` helper, a minimal output-iterator sink for
  `std::set_intersection` that just tallies `push_back` calls instead of
  materializing the intersection.
- **PhoneticMetric<Encoder, Metric>** derives from the inner `Metric` and holds a
  phonetic `Encoder`. Its `_distance` / `_similarity` encode both operands and
  defer to the inner metric over the codes; its constructor stores the encoded
  form as the bound `_str`. `name()` reports the encoder's name.

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
