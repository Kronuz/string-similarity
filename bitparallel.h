/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2019 Dubalu LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include <bit>                                    // for std::countr_zero
#include <cstddef>                                // for std::size_t
#include <cstdint>                                // for std::uint64_t
#include <cstring>                                // for std::memset
#include <memory>                                 // for std::unique_ptr
#include <string>                                 // for std::string
#include <string_view>                            // for std::string_view
#include <type_traits>                            // for std::integral_constant
#include <utility>                                // for std::swap, std::index_sequence

#include "strings.hh"                             // for chars::toupper


/*
 * Internal helpers shared by the bit-parallel kernels (Levenshtein, Jaro).
 * Not part of the public API.
 *
 * Everything works on bytes (the library's character type is char). A pattern
 * is turned into per-character bit masks ("Peq": bit i of mask[c] is set when
 * pattern[i] == c); the kernels then advance 64 dynamic-programming cells (or 64
 * match candidates) per machine word operation.
 *
 * Scratch memory is thread-local and grown on demand up to a cap, so steady-state
 * calls perform no heap allocation and a thread never keeps more than the cap; a
 * call that needs more takes a block of its own and frees it when it returns. The
 * character-to-row index is kept zeroed between calls by clearing only the entries
 * a call touched.
 */
namespace strsim_detail {

using word_t = std::uint64_t;
inline constexpr std::size_t kWordBits = 64;

// The most word scratch a thread keeps between calls: 1 MiB per slot (the library
// uses one slot). The default fits the masks of strings of about 300000 letters
// (26 symbols) or 32000 bytes of all 256 values.
#ifndef STRSIM_MAX_SCRATCH_WORDS
#define STRSIM_MAX_SCRATCH_WORDS (std::size_t(1) << 17)
#endif
inline constexpr std::size_t kMaxScratchWords = STRSIM_MAX_SCRATCH_WORDS;

// Per-thread growable word scratch, never larger than kMaxScratchWords.
struct Scratch {
	word_t* data = nullptr;
	std::size_t cap = 0;

	Scratch() = default;
	Scratch(const Scratch&) = delete;
	Scratch& operator=(const Scratch&) = delete;
	~Scratch() { delete[] data; }

	// At least n words, valid until the next call; null when n is above the cap.
	word_t* ensure(std::size_t n) {
		if (n > kMaxScratchWords) {
			return nullptr;
		}
		if (n > cap) {
			std::size_t want = n < 128 ? 128 : n + n / 2;
			if (want > kMaxScratchWords) {
				want = kMaxScratchWords;
			}
			word_t* block = new word_t[want];
			delete[] data;
			data = block;
			cap = want;
		}
		return data;
	}
};

// Character -> mask row table for one pattern. Patterns of up to 64 characters
// (one word) live entirely on the stack. Longer ones use thread-local scratch,
// which `Slot` selects (two independent tables, so Jaro can hold both strings at
// once); the thread-local index is kept zeroed by clearing only what a call touched.
template <int Slot>
class Peq {
	struct Tls {
		std::uint16_t idx[256] = {};
		Scratch rows;
	};

	static Tls& tls() {
		static thread_local Tls t;
		return t;
	}

	// Small mode storage (never initialized unless used).
	std::uint16_t _sidx[256];
	word_t _srows[kWordBits + 1];
	word_t _sextra[1];

	Tls* _t = nullptr;
	std::unique_ptr<word_t[]> _big;  // the one-off block of a request above the cap
	std::uint16_t* _idx;
	const unsigned char* _p;
	std::size_t _n;
	std::size_t _nb;
	word_t* _rows;
	word_t* _extra;

public:
	Peq(const unsigned char* p, std::size_t n, std::size_t extra = 0)
		: _p(p), _n(n), _nb((n + kWordBits - 1) / kWordBits)
	{
		std::size_t count = 0;
		if (_nb <= 1) {
			std::memset(_sidx, 0, sizeof _sidx);
			_idx = _sidx;
			_rows = _srows;
			_srows[0] = 0;
			for (std::size_t i = 0; i < n; ++i) {
				auto& r = _idx[p[i]];
				if (r == 0) {
					r = static_cast<std::uint16_t>(++count);
					_srows[count] = 0;
				}
				_srows[r] |= word_t(1) << i;
			}
			_extra = _sextra;
		} else {
			_t = &tls();
			_idx = _t->idx;
			// Rows: one zero row plus at most min(n, 256) distinct characters.
			const std::size_t distinct = n < 256 ? n : 256;
			const std::size_t words = (distinct + 1) * _nb + extra;
			_rows = _t->rows.ensure(words);
			if (!_rows) {
				// Above the thread's cap: a block for this call alone.
				_big.reset(new word_t[words]);
				_rows = _big.get();
			}
			for (std::size_t w = 0; w < _nb; ++w) {
				_rows[w] = 0;
			}
			for (std::size_t i = 0; i < n; ++i) {
				auto& r = _idx[p[i]];
				if (r == 0) {
					r = static_cast<std::uint16_t>(++count);
					word_t* row = _rows + count * _nb;
					for (std::size_t w = 0; w < _nb; ++w) {
						row[w] = 0;
					}
				}
				_rows[std::size_t(r) * _nb + i / kWordBits] |= word_t(1) << (i % kWordBits);
			}
			_extra = _rows + (distinct + 1) * _nb;
		}
	}

	Peq(const Peq&) = delete;
	Peq& operator=(const Peq&) = delete;

	~Peq() {
		if (_t) {
			for (std::size_t i = 0; i < _n; ++i) {
				_idx[_p[i]] = 0;
			}
		}
	}

	std::size_t blocks() const noexcept { return _nb; }

	// The _nb mask words for character c.
	const word_t* row(unsigned char c) const noexcept {
		return _rows + std::size_t(_idx[c]) * _nb;
	}

	// The `extra` scratch words requested at construction (one word for small patterns).
	word_t* extra() const noexcept { return _extra; }

	// The words of scratch this thread keeps between calls (for tests).
	static std::size_t retained_words() noexcept { return tls().rows.cap; }
};


// The character masks of a pattern of at most one word, all on the stack and built
// inline: bit i of mask(c) is set when pattern[i] == c. Distinct characters are
// numbered from 1 in first-seen order (at most 64, so a byte holds the number) and
// row 0 is the empty mask; only the 256 byte index is cleared.
class PeqWord {
	std::uint8_t _idx[256];
	word_t _rows[kWordBits + 1];

public:
	PeqWord(const unsigned char* p, std::size_t n) {
		std::memset(_idx, 0, sizeof _idx);
		_rows[0] = 0;
		std::size_t count = 0;
		for (std::size_t i = 0; i < n; ++i) {
			auto& r = _idx[p[i]];
			if (r == 0) {
				r = static_cast<std::uint8_t>(++count);
				_rows[count] = 0;
			}
			_rows[r] |= word_t(1) << i;
		}
	}

	PeqWord(const PeqWord&) = delete;
	PeqWord& operator=(const PeqWord&) = delete;

	word_t mask(unsigned char c) const noexcept { return _rows[_idx[c]]; }
};


// Case folding on the fly: fold into a caller-sized stack buffer when it fits,
// into a heap string otherwise.
class Folded {
	static constexpr std::size_t kStack = 512;
	char _buf[kStack];
	std::string _heap;
	std::string_view _view;

public:
	Folded(std::string_view s, bool fold) {
		if (!fold) {
			_view = s;
		} else if (s.size() <= kStack) {
			for (std::size_t i = 0; i < s.size(); ++i) {
				_buf[i] = chars::toupper(s[i]);
			}
			_view = std::string_view(_buf, s.size());
		} else {
			_heap.resize(s.size());
			for (std::size_t i = 0; i < s.size(); ++i) {
				_heap[i] = chars::toupper(s[i]);
			}
			_view = _heap;
		}
	}
	Folded(const Folded&) = delete;
	Folded& operator=(const Folded&) = delete;

	std::string_view view() const noexcept { return _view; }
};


// Columns of the table advanced together per pass over the blocks of a pattern
// longer than one word. The cells of one column depend on each other through a
// short carry chain from block to block; advancing several columns per visit gives
// the processor independent chains to overlap, so it runs at the throughput of the
// operations rather than at the latency of the chain. Three columns fit AArch64's 31
// registers and are the fastest on Apple M1 and M4 (two is 8% slower than the single
// column at 300-450 characters on M4); x86-64's 16 registers make two the fastest there.
#ifndef STRSIM_COLUMNS
#if defined(__aarch64__) || defined(_M_ARM64)
#define STRSIM_COLUMNS 3
#else
#define STRSIM_COLUMNS 2
#endif
#endif
inline constexpr std::size_t kColumns = STRSIM_COLUMNS;

// A pattern of at least this many words first runs inside a narrow band around
// the diagonals (see levenshtein_band), kBandSlack wider than the length difference.
#ifndef STRSIM_BAND_MIN_BLOCKS
#define STRSIM_BAND_MIN_BLOCKS 8
#endif
#ifndef STRSIM_BAND_SLACK
#define STRSIM_BAND_SLACK 63
#endif
inline constexpr std::size_t kBandMinBlocks = STRSIM_BAND_MIN_BLOCKS;
inline constexpr std::size_t kBandSlack = STRSIM_BAND_SLACK;

#if defined(__GNUC__) || defined(__clang__)
#define STRSIM_INLINE inline __attribute__((always_inline))
#define STRSIM_NOINLINE __attribute__((noinline))
#else
#define STRSIM_INLINE inline
#define STRSIM_NOINLINE
#endif

// One block (64 rows) of Hyyro's recurrence over one column. (Pv, Mv) are the
// block's vertical deltas and advance to the next column; (hp, hn) is the horizontal
// delta entering the block's top row and becomes the one leaving its bottom row.
STRSIM_INLINE void lev_step(word_t Eq, word_t& Pv, word_t& Mv, word_t& hp, word_t& hn) {
	const word_t Xv = Eq | Mv;
	const word_t Eh = Eq | hn;
	const word_t Xh = (((Eh & Pv) + Pv) ^ Pv) | Eh;
	const word_t Ph = Mv | ~(Xh | Pv);
	const word_t Mh = Pv & Xh;
	const word_t Ps = (Ph << 1) | hp;
	const word_t Ms = (Mh << 1) | hn;
	hp = Ph >> 63;
	hn = Mh >> 63;
	Pv = Ms | ~(Xv | Ps);
	Mv = Ps & Xv;
}

// The same for the block that holds the tracked score row: also adds the horizontal
// delta at bit `end_bit` (the row whose value is `score`) to `score`.
STRSIM_INLINE void lev_step_end(word_t Eq, word_t& Pv, word_t& Mv, word_t hp, word_t hn, word_t end_bit, std::size_t& score) {
	const word_t Xv = Eq | Mv;
	const word_t Eh = Eq | hn;
	const word_t Xh = (((Eh & Pv) + Pv) ^ Pv) | Eh;
	const word_t Ph = Mv | ~(Xh | Pv);
	const word_t Mh = Pv & Xh;
	score += (Ph & end_bit) != 0;
	score -= (Mh & end_bit) != 0;
	const word_t Ps = (Ph << 1) | hp;
	const word_t Ms = (Mh << 1) | hn;
	Pv = Ms | ~(Xv | Ps);
	Mv = Ps & Xv;
}

template <std::size_t U, std::size_t... Is>
STRSIM_INLINE void lev_columns(const word_t* const (&r)[U], std::size_t w, word_t& Pv, word_t& Mv, word_t (&hp)[U], word_t (&hn)[U], std::index_sequence<Is...>) {
	(lev_step(r[Is][w], Pv, Mv, hp[Is], hn[Is]), ...);
}

template <std::size_t U, std::size_t... Is>
STRSIM_INLINE void lev_columns_end(const word_t* const (&r)[U], std::size_t w, word_t& Pv, word_t& Mv, const word_t (&hp)[U], const word_t (&hn)[U], word_t end_bit, std::size_t& score, std::index_sequence<Is...>) {
	(lev_step_end(r[Is][w], Pv, Mv, hp[Is], hn[Is], end_bit, score), ...);
}

// Advance blocks first..last over the U text columns at `a`. The delta entering the
// first block is +1 (the true top edge when first is 0; an over-estimate otherwise).
// `score` follows the horizontal delta at bit `end_bit` of block `last`.
template <std::size_t U, std::size_t... Is>
STRSIM_INLINE void lev_pass_impl(const unsigned char* a, const Peq<0>& peq, word_t* Pv, word_t* Mv, std::size_t first, std::size_t last, word_t end_bit, std::size_t& score, std::index_sequence<Is...> seq) {
	// Initialized element by element: a loop here becomes a call to memset_pattern16.
	const word_t* const r[U] = {peq.row(a[Is])...};
	word_t hp[U] = {((void)Is, word_t(1))...};
	word_t hn[U] = {((void)Is, word_t(0))...};
	for (std::size_t w = first; w < last; ++w) {
		word_t P = Pv[w], M = Mv[w];
		lev_columns<U>(r, w, P, M, hp, hn, seq);
		Pv[w] = P;
		Mv[w] = M;
	}
	word_t P = Pv[last], M = Mv[last];
	lev_columns_end<U>(r, last, P, M, hp, hn, end_bit, score, seq);
	Pv[last] = P;
	Mv[last] = M;
}

template <std::size_t U>
STRSIM_INLINE void lev_pass(const unsigned char* a, const Peq<0>& peq, word_t* Pv, word_t* Mv, std::size_t first, std::size_t last, word_t end_bit, std::size_t& score) {
	lev_pass_impl<U>(a, peq, Pv, Mv, first, last, end_bit, score, std::make_index_sequence<U>{});
}

// Edit distance of text a[0..n) and the pattern held by `peq` (m rows, n >= m, at
// least one word, two scratch words per block) over the whole table: every block in
// every column, so the result is exact.
inline std::size_t levenshtein_full(const unsigned char* a, std::size_t n, std::size_t m, const Peq<0>& peq) {
	const std::size_t nb = peq.blocks();
	word_t* Pv = peq.extra();
	word_t* Mv = Pv + nb;
	for (std::size_t w = 0; w < nb; ++w) {
		Pv[w] = ~word_t(0);
		Mv[w] = 0;
	}
	const word_t end_bit = word_t(1) << ((m - 1) % kWordBits);
	std::size_t score = m;
	std::size_t j = 0;
	for (; j + kColumns <= n; j += kColumns) {
		lev_pass<kColumns>(a + j, peq, Pv, Mv, 0, nb - 1, end_bit, score);
	}
	for (; j < n; ++j) {
		lev_pass<1>(a + j, peq, Pv, Mv, 0, nb - 1, end_bit, score);
	}
	return score;
}

// Edit distance of the same, computed inside a band of the table.
//
// Any alignment of cost at most k stays on the diagonals j - i in [-s, (n - m) + s],
// where s = (k - (n - m)) / 2: an alignment that touches diagonal d needs at least
// |d| + |d - (n - m)| insertions and deletions. The kernel advances only the blocks
// of 64 rows that the band touches in each group of columns. A block that enters the
// band below is started from "every vertical delta is +1" (a column that grows by one
// per row), a block that leaves above hands the next block a +1 edge delta, and both
// are over-estimates: the table it computes is never below the true one, and it equals the true one
// on every cell of an alignment inside the band. So
//   * a result <= k is the exact distance,
//   * a result > k only says "more than k" and is itself an upper bound of the
//     distance (the retry in levenshtein_unit uses it as the next k),
// and `exact` is set when the band covered the whole table (the result is then exact
// whatever its value). k >= n + m covers it all.
inline std::size_t levenshtein_band(const unsigned char* a, std::size_t n, std::size_t m, const Peq<0>& peq, std::size_t k, bool& exact) {
	const std::size_t nb = peq.blocks();
	word_t* Pv = peq.extra();
	word_t* Mv = Pv + nb;
	const std::size_t delta = n - m;
	std::size_t s = k > delta ? (k - delta) / 2 : 0;
	if (s > m) s = m;
	const word_t top_bit = word_t(1) << 63;
	const word_t end_bit = word_t(1) << ((m - 1) % kWordBits);

	exact = true;
	std::size_t active = 0;  // blocks started so far
	std::size_t score = 0;   // the value at the last row of block (active - 1)
	std::size_t j = 0;       // columns done
	while (j < n) {
		const std::size_t cols = j + kColumns <= n ? kColumns : 1;
		// Rows (1-based) the band reaches in the columns j + 1 .. j + cols.
		const std::size_t top = j + 1 > delta + s ? j + 1 - (delta + s) : 1;
		const std::size_t bot = j + cols + s < m ? j + cols + s : m;
		const std::size_t first = (top - 1) / kWordBits;
		const std::size_t last = (bot - 1) / kWordBits;
		if (first != 0 || (active == 0 && last + 1 != nb)) {
			exact = false;
		}
		for (; active <= last; ++active) {
			// Column 0 (or a new block below the band): every delta is +1.
			Pv[active] = ~word_t(0);
			Mv[active] = 0;
			score += active + 1 < nb ? kWordBits : m - active * kWordBits;
		}
		const word_t score_bit = last + 1 == nb ? end_bit : top_bit;
		if (cols == kColumns) {
			lev_pass<kColumns>(a + j, peq, Pv, Mv, first, last, score_bit, score);
		} else {
			lev_pass<1>(a + j, peq, Pv, Mv, first, last, score_bit, score);
		}
		j += cols;
	}
	return score;
}

// Trim the common prefix and suffix (they never change the distance) and make the
// shorter string the pattern b (fewer words per text character).
inline void levenshtein_prepare(const unsigned char*& a, const unsigned char*& b, std::size_t& n, std::size_t& m) {
	while (n && m && *a == *b) {
		++a; ++b; --n; --m;
	}
	while (n && m && a[n - 1] == b[m - 1]) {
		--n; --m;
	}
	if (m > n) {
		std::swap(a, b);
		std::swap(n, m);
	}
}

// A pattern of two or more words (m > 64, n >= m). Short ones take the whole table. A
// long one first runs inside a narrow band, which is exact when the distance is within
// the band's bound k0 = (n - m) + slack; otherwise it returns an upper bound that sizes
// the second, final run. The band is skipped when the first run would touch more than
// about half of the pattern's rows anyway (a large length difference): there it only
// repeats the work. Out of line: this is big, and the one-word path should stay small
// (its prologue and the call it is inlined into are part of a short word's cost).
STRSIM_NOINLINE inline std::size_t levenshtein_blocks(const unsigned char* a, std::size_t n, const unsigned char* b, std::size_t m, std::size_t slack) {
	const std::size_t nb = (m + kWordBits - 1) / kWordBits;
	Peq<0> peq(b, m, 2 * nb);
	const std::size_t delta = n - m;
	if (nb < kBandMinBlocks || delta + slack + 1 + 2 * kWordBits > m / 2) {
		return levenshtein_full(a, n, m, peq);
	}
	bool exact;
	const std::size_t k0 = delta + slack;
	const std::size_t r = levenshtein_band(a, n, m, peq, k0, exact);
	if (exact || r <= k0) {
		return r;
	}
	return levenshtein_band(a, n, m, peq, r, exact);
}

// Unit-cost Levenshtein distance (insert, delete, substitute all cost 1) by
// Myers' bit-vector algorithm (single word) and Hyyrö's block form (longer).
// `slack` is the width of the first band, beyond the length difference, for a long
// pattern; it only affects speed, never the result.
inline std::size_t levenshtein_unit(std::string_view sa, std::string_view sb, std::size_t slack = kBandSlack) {
	auto a = reinterpret_cast<const unsigned char*>(sa.data());
	auto b = reinterpret_cast<const unsigned char*>(sb.data());
	std::size_t n = sa.size(), m = sb.size();
	levenshtein_prepare(a, b, n, m);
	if (m == 0) return n;
	// a: text (n), b: pattern (m <= n)
	if (m > kWordBits) {
		return levenshtein_blocks(a, n, b, m, slack);
	}

	PeqWord peq(b, m);
	const word_t last = word_t(1) << (m - 1);
	word_t Pv = ~word_t(0), Mv = 0;
	std::size_t score = m;
	for (std::size_t j = 0; j < n; ++j) {
		const word_t Eq = peq.mask(a[j]);
		const word_t Xv = Eq | Mv;
		const word_t Xh = (((Eq & Pv) + Pv) ^ Pv) | Eq;
		word_t Ph = Mv | ~(Xh | Pv);
		word_t Mh = Pv & Xh;
		score += (Ph & last) != 0;
		score -= (Mh & last) != 0;
		Ph = (Ph << 1) | 1;
		Mh <<= 1;
		Pv = Mh | ~(Xv | Ph);
		Mv = Ph & Xv;
	}
	return score;
}

// One banded run with distance bound k, for tests: returns the exact distance when
// it is at most k and otherwise an upper bound above k; `exact` says whether the
// band covered the whole table. A pattern of one word has no band to speak of: its
// result is exact.
inline std::size_t levenshtein_unit_band(std::string_view sa, std::string_view sb, std::size_t k, bool& exact) {
	auto a = reinterpret_cast<const unsigned char*>(sa.data());
	auto b = reinterpret_cast<const unsigned char*>(sb.data());
	std::size_t n = sa.size(), m = sb.size();
	levenshtein_prepare(a, b, n, m);
	exact = true;
	if (m == 0) return n;
	if (m <= kWordBits) return levenshtein_unit(sa, sb);
	const std::size_t nb = (m + kWordBits - 1) / kWordBits;
	Peq<0> peq(b, m, 2 * nb);
	return levenshtein_band(a, n, m, peq, k, exact);
}

} // namespace strsim_detail
