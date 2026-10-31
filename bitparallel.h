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
#include <string>                                 // for std::string
#include <string_view>                            // for std::string_view
#include <utility>                                // for std::swap

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
 * Scratch memory is thread-local and grown on demand, so steady-state calls
 * perform no heap allocation. The character-to-row index is kept zeroed between
 * calls by clearing only the entries a call touched.
 */
namespace strsim_detail {

using word_t = std::uint64_t;
inline constexpr std::size_t kWordBits = 64;

// Per-thread growable word scratch.
struct Scratch {
	word_t* data = nullptr;
	std::size_t cap = 0;

	Scratch() = default;
	Scratch(const Scratch&) = delete;
	Scratch& operator=(const Scratch&) = delete;
	~Scratch() { delete[] data; }

	word_t* ensure(std::size_t n) {
		if (n > cap) {
			delete[] data;
			cap = n < 128 ? 128 : n + n / 2;
			data = new word_t[cap];
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
			_rows = _t->rows.ensure((distinct + 1) * _nb + extra);
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


// Unit-cost Levenshtein distance (insert, delete, substitute all cost 1) by
// Myers' bit-vector algorithm (single word) and Hyyrö's block form (longer).
inline std::size_t levenshtein_unit(std::string_view sa, std::string_view sb) {
	auto a = reinterpret_cast<const unsigned char*>(sa.data());
	auto b = reinterpret_cast<const unsigned char*>(sb.data());
	std::size_t n = sa.size(), m = sb.size();

	// Common prefix and suffix never change the distance.
	while (n && m && *a == *b) {
		++a; ++b; --n; --m;
	}
	while (n && m && a[n - 1] == b[m - 1]) {
		--n; --m;
	}
	if (n == 0) return m;
	if (m == 0) return n;

	// The shorter string is the pattern (fewer words per text character).
	if (m > n) {
		std::swap(a, b);
		std::swap(n, m);
	}
	// a: text (n), b: pattern (m <= n)

	const std::size_t nb = (m + kWordBits - 1) / kWordBits;
	Peq<0> peq(b, m, 2 * nb);

	if (nb == 1) {
		const word_t last = word_t(1) << (m - 1);
		word_t Pv = ~word_t(0), Mv = 0;
		std::size_t score = m;
		for (std::size_t j = 0; j < n; ++j) {
			const word_t Eq = peq.row(a[j])[0];
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

	word_t* Pv = peq.extra();
	word_t* Mv = Pv + nb;
	for (std::size_t w = 0; w < nb; ++w) {
		Pv[w] = ~word_t(0);
		Mv[w] = 0;
	}
	const word_t last = word_t(1) << ((m - 1) % kWordBits);
	std::size_t score = m;
	for (std::size_t j = 0; j < n; ++j) {
		const word_t* row = peq.row(a[j]);
		// Horizontal delta entering the block: D[0][j] = j, so +1 at the top.
		word_t hp = 1, hn = 0;
		for (std::size_t w = 0; w + 1 < nb; ++w) {
			const word_t Eq = row[w];
			const word_t pv = Pv[w], mv = Mv[w];
			const word_t Xv = Eq | mv;
			const word_t Eh = Eq | hn;
			const word_t Xh = (((Eh & pv) + pv) ^ pv) | Eh;
			const word_t Ph = mv | ~(Xh | pv);
			const word_t Mh = pv & Xh;
			const word_t Ps = (Ph << 1) | hp;
			const word_t Ms = (Mh << 1) | hn;
			hp = Ph >> 63;
			hn = Mh >> 63;
			Pv[w] = Ms | ~(Xv | Ps);
			Mv[w] = Ps & Xv;
		}
		{
			const std::size_t w = nb - 1;
			const word_t Eq = row[w];
			const word_t pv = Pv[w], mv = Mv[w];
			const word_t Xv = Eq | mv;
			const word_t Eh = Eq | hn;
			const word_t Xh = (((Eh & pv) + pv) ^ pv) | Eh;
			const word_t Ph = mv | ~(Xh | pv);
			const word_t Mh = pv & Xh;
			score += (Ph & last) != 0;
			score -= (Mh & last) != 0;
			const word_t Ps = (Ph << 1) | hp;
			const word_t Ms = (Mh << 1) | hn;
			Pv[w] = Ms | ~(Xv | Ps);
			Mv[w] = Ps & Xv;
		}
	}
	return score;
}

} // namespace strsim_detail
