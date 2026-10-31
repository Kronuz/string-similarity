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
the base-case checks once — identical strings short-circuit to `0` / `1`, an empty
operand to `1` / `0` — then forwards to the subclass through a `static_cast<const
Impl*>(this)`, calling `_distance` / `_similarity` / `_name` / `_description`.
`_icase` case-folds both operands with `strings::upper` before the subclass sees
them, so each metric implements only the math, on already-normalized input.

The subclasses are befriended (`friend class StringMetric<Impl>;`) so the base can
reach their private `_distance` / `_similarity`. Each metric defines those two
plus their one-argument forms, and `_name` / `_description`.

## What each metric computes

- **Levenshtein** fills the usual two-row DP table and returns the edit distance
  *normalized*: `raw / (maxCost * max(len1, len2))`. With unit costs that is
  `raw / max(len1, len2)`, so the test recovers the textbook integer distance by
  multiplying back through the longer length.
- **Jaro** counts matching characters within a sliding window and their
  transpositions, combining them into the standard Jaro formula. **Jaro_Winkler**
  is its own metric over `Jaro::jaro` and adds the shared-prefix boost (up to four
  characters, scaled by `p`, applied only above a boost threshold `bt`).
- **LCSubstr** and **LCSubsequence** each run a two-row DP — contiguous run length
  for the former, classic LCS for the latter — and divide by `max(len1, len2)` to
  get a similarity in `[0, 1]`.
- **Jaccard** builds a `std::set<char>` of each string and returns
  `|intersection| / |union|`. **Sorensen_Dice** does the same over the strings'
  2-character substrings (bigrams): `2 * |shared| / (|b1| + |b2|)`. Both count the
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
inner `Metric`, so the public two-argument call runs the inner metric on the raw
strings, and the phonetic encoding is exercised through the bound-string
constructor path. This is upstream Xapiand behavior, preserved; the test verifies
the phonetic collapse through the encoder directly.

## Why this shape

The suite exists to score how alike two short strings are, by several different
notions of "alike," behind one uniform interface. The CRTP base removes the
boilerplate (base cases, case folding, the bound-string overloads) so each metric
is just its kernel. It is character-oriented: the set- and bigram-based metrics
operate on bytes, and the phonetic wrapper is the path to a sound-based, rather
than spelling-based, comparison.

## Standalone vs. Xapiand

Upstream, the base and each subclass also carried `serialise()` / `unserialise()`
methods to persist a metric's configured parameters (the icase flag, the bound
string, Levenshtein's costs, Jaro-Winkler's `p` / `bt`) into a Xapiand index.
Those dragged in `length.h` (Xapiand's varint serializer) and a GPL-licensed
Xapian `serialise-double.h`. None of it touches the math, so on extraction the
persistence methods and both includes were removed, leaving the pure metrics. The
only remaining local includes are `strings.hh` (for `strings::upper`) and, for the
soundex wrapper, the phonetic encoder headers — both resolved through CMake
against their standalone sibling libraries. Any change here should stay
reconcilable with upstream as a plain edit.
