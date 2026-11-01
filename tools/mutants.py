#!/usr/bin/env python3
"""Mutation check for the metrics.

For each mutant: copy the headers with one deliberate bug into a scratch directory, build
the tests named for it against that copy and require that one of them FAILS (does not
build, exits non-zero, or does not finish in time). A mutant that survives means the tests
would not catch that bug.

usage: tools/mutants.py BUILD_DIR [-j N] [--quick] [--list] [only-name ...]

BUILD_DIR is a build directory of this repository configured with
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON; the compile flags and the include paths of the
dependencies come from it. The scratch directory is $MUT_DIR or a temporary one. CXX
overrides the compiler; MUTANT_FLAGS adds compile flags; MUTANT_JOBS sets -j. --quick passes
the "quick" argument to the tests that take one.

Two mutants make a heap buffer too small, which only AddressSanitizer sees. They are skipped
unless MUTANT_FLAGS contains -fsanitize=address. Changes that move work between the fast and
the slow path without changing a result (the size at which a buffer moves to the heap, the
length at which the band is skipped, the common prefix and suffix that are trimmed, the word a
scan starts from) are equivalent mutants and are not listed.
"""
import concurrent.futures
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# name -> (source, arguments of the quick run)
TESTS = {
    "levenshtein": ("test/levenshtein_long.cc", ["quick"]),
    "differential": ("test/differential.cc", ["quick"]),
    "correctness": ("test/correctness.cc", []),
    "scratch": ("test/scratch.cc", []),
    "copy": ("test/copy_move.cc", []),
    "subclass": ("test/subclass_compat.cc", []),
    "smoke": ("test/test.cc", []),
}

BASE = "basic_string_metric.h"
BIT = "bitparallel.h"
LEV = "levenshtein.h"
JARO = "jaro.h"
JW = "jaro_winkler.h"
DICE = "sorensen_dice.h"
PHON = "phonetic_metric.h"

K = ("levenshtein", "differential", "correctness")      # the Levenshtein kernel
J = ("correctness", "differential", "scratch")          # Jaro and Jaro-Winkler
D = ("correctness", "differential")                     # the other metrics

# name: (file, old text, new text, tests). `old` occurs once in the file.
MUTANTS = {
    # --- Levenshtein: the bit-vector step
    "carry_hp": (BIT, "hp = Ph >> 63;", "hp = Ph >> 62;", K),
    "carry_hn": (BIT, "hn = Mh >> 63;", "hn = Ph >> 63;", K),
    "step_eh_without_hn": (BIT, "const word_t Eh = Eq | hn;\n\tconst word_t Xh = (((Eh & Pv) + Pv) ^ Pv) | Eh;\n\tconst word_t Ph = Mv | ~(Xh | Pv);\n\tconst word_t Mh = Pv & Xh;\n\tconst word_t Ps",
                            "const word_t Eh = Eq;\n\tconst word_t Xh = (((Eh & Pv) + Pv) ^ Pv) | Eh;\n\tconst word_t Ph = Mv | ~(Xh | Pv);\n\tconst word_t Mh = Pv & Xh;\n\tconst word_t Ps", K),
    "end_step_no_decrement": (BIT, "\tscore -= (Mh & end_bit) != 0;\n", "", K),
    "end_step_no_increment": (BIT, "\tscore += (Ph & end_bit) != 0;\n", "", K),
    "top_edge_zero": (BIT, "word_t hp[U] = {((void)Is, word_t(1))...};", "word_t hp[U] = {((void)Is, word_t(0))...};", K),
    "full_init_zero": (BIT, "\t\tPv[w] = ~word_t(0);", "\t\tPv[w] = 0;", K),
    "full_end_bit": (BIT, "const word_t end_bit = word_t(1) << ((m - 1) % kWordBits);\n\tstd::size_t score = m;",
                     "const word_t end_bit = word_t(1) << (m % kWordBits);\n\tstd::size_t score = m;", K),
    "full_score_start": (BIT, "\tstd::size_t score = m;\n\tstd::size_t j = 0;", "\tstd::size_t score = m + 1;\n\tstd::size_t j = 0;", K),
    "one_word_end_bit": (BIT, "const word_t last = word_t(1) << (m - 1);", "const word_t last = word_t(1) << m;", K),
    "one_word_top_carry": (BIT, "Ph = (Ph << 1) | 1;", "Ph = (Ph << 1);", K),
    # --- Levenshtein: the band
    "narrow_band": (BIT, "(k - delta) / 2 : 0;", "(k - delta) / 2 - ((k - delta) / 2 > 0) : 0;", K),
    "band_init_zero": (BIT, "\t\t\tPv[active] = ~word_t(0);", "\t\t\tPv[active] = 0;", K),
    "score_rows": (BIT, "score += active + 1 < nb ? kWordBits : m - active * kWordBits;", "score += kWordBits;", K),
    "exact_always": (BIT, "if (first != 0 || (active == 0 && last + 1 != nb)) {", "if (false) {", K),
    "bottom_short": (BIT, "const std::size_t bot = j + cols + s < m ? j + cols + s : m;", "const std::size_t bot = j + s < m ? j + s : m;", K),
    "top_cut": (BIT, "const std::size_t top = j + 1 > delta + s ? j + 1 - (delta + s) : 1;", "const std::size_t top = j + cols > delta + s ? j + cols - (delta + s) : 1;", K),
    "retry_half": (BIT, "return levenshtein_band(a, n, m, peq, r, exact);", "return levenshtein_band(a, n, m, peq, r / 2, exact);", K),
    "accept_wrong": (BIT, "if (exact || r <= k0) {", "if (exact || r <= k0 + 8) {", K),
    "score_bit_top": (BIT, "const word_t score_bit = last + 1 == nb ? end_bit : top_bit;", "const word_t score_bit = top_bit;", K),
    "delta_off": (BIT, "const std::size_t delta = n - m;\n\tstd::size_t s", "const std::size_t delta = 0;\n\tstd::size_t s", K),
    # --- Levenshtein: trimming and masks
    "pattern_is_longer": (BIT, "\tif (m > n) {\n\t\tstd::swap(a, b);", "\tif (m < n) {\n\t\tstd::swap(a, b);", K),
    "peq_rows_capacity": (BIT, "const std::size_t words = (distinct + 1) * _nb + extra;", "const std::size_t words = distinct * _nb + extra;", K),
    "peq_index_kept": (BIT, "\t\t\t\t_idx[_p[i]] = 0;", "\t\t\t\t(void)0;", K),
    "direct_rows_kept": (BIT, "_t[std::size_t(_p[i]) * _nb + i / kWordBits] = 0;", "(void)0;", J),
    "direct_row_offset": (BIT, "return _t + std::size_t(c) * _nb;", "return _t + std::size_t(c) * _nb + (c == 'a');", J),
    "common_prefix_order": (BIT, "std::endian::native == std::endian::little ? std::countr_zero(diff) : std::countl_zero(diff)",
                            "std::endian::native == std::endian::little ? std::countl_zero(diff) : std::countr_zero(diff)", J),
    # --- scratch cap
    "scratch_cap_off": (BIT, "\t\tif (n > kMaxScratchWords) {\n\t\t\treturn nullptr;", "\t\tif (n > 2 * kMaxScratchWords) {\n\t\t\treturn nullptr;", ("scratch",)),
    "scratch_grow_uncapped": (BIT, "\t\t\tif (want > kMaxScratchWords) {\n\t\t\t\twant = kMaxScratchWords;\n\t\t\t}\n", "", ("scratch",)),
    # --- Levenshtein: costs
    "weighted_seed": (LEV, "col[0] = (i + 1) * _ins_del_cost;", "col[0] = i + 1;", D),
    "weighted_first_row": (LEV, "prev_col[i] = i * _ins_del_cost;", "prev_col[i] = i;", D),
    "weighted_substitution": (LEV, "prev_col[j] + (str1[i] == str2[j] ? 0 : _subst_cost)", "prev_col[j] + (str1[i] == str2[j] ? 0 : _ins_del_cost)", D),
    "zero_cost_dropped": (LEV, "\t\tif (_maxCost == 0) {\n\t\t\treturn 0.0;  // every edit is free: any two strings are at distance 0\n\t\t}\n", "", D),
    "unit_costs_test": (LEV, "if (_subst_cost == 1 && _ins_del_cost == 1) {", "if (_subst_cost == 1) {", D),
    "normalize_by_shorter": (LEV, "(_maxCost * std::max(len1, len2))", "(_maxCost * std::min(len1, len2))", D),
    # --- Jaro: matching
    "jaro_no_start_mask": (JARO, "\t\t\t\t\tif (w == start / W) {\n\t\t\t\t\t\tm &= ~strsim_detail::word_t(0) << (start % W);\n\t\t\t\t\t}\n", "", J),
    "jaro_no_end_mask": (JARO, "\t\t\t\t\tif (w == lastw && end % W != 0) {\n\t\t\t\t\t\tm &= (strsim_detail::word_t(1) << (end % W)) - 1;\n\t\t\t\t\t}\n", "", J),
    "jaro_cursor_past": (JARO, "cursor[uc] = static_cast<std::uint32_t>(std::min(w, lastw));", "cursor[uc] = static_cast<std::uint32_t>(w);", J),
    "jaro_last_word": (JARO, "const size_t lastw = (end - 1) / W;", "const size_t lastw = end / W;", J),
    "jaro_window_grows": (JARO, "window = (window << 1) | 1;", "window = (window << 1);", J),
    "jaro_first_window": (JARO, ": (word_t(1) << (bound + 1)) - 1;", ": (word_t(1) << bound) - 1;", J),
    "jaro_end_limit": (JARO, "const size_t end = std::min(r2, r1 + bound);", "const size_t end = std::min(r2, r1 + bound - 1);", J),
    "jaro_word_limit": (JARO, "if (r1 <= 64 && r2 <= 64) {", "if (r1 <= 64 && r2 <= 65) {", J),
    "jaro_bound": (JARO, "const size_t bound = half ? half - 1 : 0;", "const size_t bound = half;", J),
    "jaro_prefix_uncounted": (JARO, "size_t m = prefix, t = 0;", "size_t m = 0, t = 0;", J),
    "jaro_word_transpositions": (JARO, "t = out_of_order / 2;\n\t\treturn static_cast<size_t>(std::popcount(used));", "t = out_of_order;\n\t\treturn static_cast<size_t>(std::popcount(used));", J),
    "jaro_block_transpositions": (JARO, "t = out_of_order / 2;\n\t\treturn m;", "t = out_of_order;\n\t\treturn m;", J),
    "jaro_ignores_transpositions": (JARO, "((double)(m - t) / m)", "((double)m / m)", J),
    # --- Jaro-Winkler
    "winkler_prefix_5": (JW, "MAX_PREFIX_LEN{4}", "MAX_PREFIX_LEN{5}", J),
    "winkler_boost_at_threshold": (JW, "if (jd <= _bt) {", "if (jd < _bt) {", J),
    "winkler_no_boost": (JW, "return jd + (len_common_prefix(str1, str2) * _p * (1.0 - jd));", "return jd;", J),
    "winkler_fixed_p": (JW, "(len_common_prefix(str1, str2) * _p * (1.0 - jd))", "(len_common_prefix(str1, str2) * 0.1 * (1.0 - jd))", J),
    # --- Sorensen-Dice
    "dice_counts_not_consumed": (DICE, "c -= hit;", "c -= 0;", D),
    "dice_table_not_cleared": (DICE, "\t\tt[bigram_key(a.data() + i)] = 0;", "\t\t(void)0;", D),
    "dice_denominator": (DICE, "((str1.size() - 1) + (str2.size() - 1))", "(str1.size() + str2.size())", D),
    "dice_one_character": (DICE, "return !str1.empty() && str1 == str2 ? 1.0 : 0.0;", "return 0.0;", D),
    "dice_key_shift": (DICE, "(unsigned(static_cast<unsigned char>(p[0])) << 8)", "(unsigned(static_cast<unsigned char>(p[0])) << 4)", D),
    # --- the other metrics
    "jaccard_union": ("jaccard.h", "return c.count / (double)(set_str1.size() + set_str2.size() - c.count);", "return c.count / (double)(set_str1.size() + set_str2.size());", D),
    "lcsubstr_run": ("lcsubstr.h", "v2[j] = v1[j - 1] + 1;", "v2[j] = v1[j - 1] + 2;", D),
    "lcsubsequence_max": ("lcsubsequence.h", "v2[j] = std::max(v2[j - 1], v1[j]);", "v2[j] = std::min(v2[j - 1], v1[j]);", D),
    # --- the base class and the phonetic wrapper
    "bound_string_not_folded": (BASE, "_str(_icase ? strings::upper(std::forward<T>(str)) : std::forward<T>(str)) { }", "_str(std::forward<T>(str)) { }", D + ("subclass",)),
    "phonetic_not_encoded": (PHON, "return Metric::distance(_soundex.encode(str1), _soundex.encode(str2));", "return Metric::distance(str1, str2);", D),
    "phonetic_bound_not_encoded": (PHON, "\t\tthis->_str = _soundex.encode();\n", "", D + ("subclass",)),
    # --- what a subclass reads
    "winkler_factor_private": (JW, "protected:\n\t// protected (not private) so a consumer can subclass to re-add the\n\t// serialise()/unserialise() persistence this library deliberately omits.\n\tdouble _p;  // Scaling factor.\n\tdouble _bt; // Boost threshold.\n\nprivate:",
                               "private:\n\tdouble _p;  // Scaling factor.\n\tdouble _bt; // Boost threshold.\n", ("subclass",)),
    "winkler_default_p": (JW, "Jaro_Winkler(T&& str, bool icase=true, double p=0.1, double bt=0.7)", "Jaro_Winkler(T&& str, bool icase=true, double p=0.2, double bt=0.7)", ("subclass",)),
    "winkler_default_p_unbound": (JW, "Jaro_Winkler(bool icase=true, double p=0.1, double bt=0.7)", "Jaro_Winkler(bool icase=true, double p=0.2, double bt=0.7)", ("subclass",)),
    "winkler_default_threshold": (JW, "Jaro_Winkler(T&& str, bool icase=true, double p=0.1, double bt=0.7)", "Jaro_Winkler(T&& str, bool icase=true, double p=0.1, double bt=0.8)", ("subclass",)),
    "levenshtein_costs_private": (LEV, "protected:\n\t// protected (not private) so a consumer can subclass to re-add the\n\t// serialise()/unserialise() persistence this library deliberately omits.\n\tsize_t _subst_cost;\n\tsize_t _ins_del_cost;\n\tsize_t _maxCost;\n\nprivate:",
                                   "private:\n\tsize_t _subst_cost;\n\tsize_t _ins_del_cost;\n\tsize_t _maxCost;\n", ("subclass",)),
    "levenshtein_max_cost": (LEV, "\t\t  _maxCost(std::max(_subst_cost, _ins_del_cost)) { }\n\n\ttemplate", "\t\t  _maxCost(_subst_cost) { }\n\n\ttemplate", ("subclass",) + D),
}

# Mutants that must not build: a member a subclass reads is no longer accessible.
DOES_NOT_BUILD = {"winkler_factor_private", "levenshtein_costs_private"}

# Mutants that write one element past a heap or stack buffer: only AddressSanitizer sees
# them. They run when MUTANT_FLAGS contains -fsanitize=address and are skipped otherwise.
ASAN_ONLY = {"peq_rows_capacity", "scratch_cap_off"}


def flags_for(build_dir, source):
    """The compile flags the build uses for `source`, without its input and output."""
    path = os.path.join(build_dir, "compile_commands.json")
    if not os.path.exists(path):
        sys.exit(f"{path} not found: configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
    want = os.path.join(root, source)
    for entry in json.load(open(path)):
        if os.path.abspath(entry["file"]) != want:
            continue
        args = shlex.split(entry["command"])
        out, skip = [], False
        for a in args[1:]:
            if skip:
                skip = False
            elif a == "-o":
                skip = True
            elif a == "-c" or os.path.abspath(a) == want:
                pass
            else:
                out.append(a)
        return args[0], out
    sys.exit(f"{source} is not in {path}: build the tests first")


def run(cmd, timeout, **kw):
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout, **kw)
        return p.returncode, p.stdout
    except subprocess.TimeoutExpired:
        return None, ""


def check(name, spec, scratch, build_dir, cxx, quick):
    fname, old, new, tests = spec
    extra = shlex.split(os.environ.get("MUTANT_FLAGS", ""))
    if name in ASAN_ONLY and "-fsanitize=address" not in extra:
        return name, "skipped", "needs MUTANT_FLAGS=-fsanitize=address"
    text = open(os.path.join(root, fname), encoding="utf-8").read()
    if text.count(old) != 1:
        return name, "bad", f"the text to change occurs {text.count(old)} times in {fname}"
    d = os.path.join(scratch, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for h in os.listdir(root):
        if h.endswith(".h"):
            shutil.copy(os.path.join(root, h), d)
    open(os.path.join(d, fname), "w", encoding="utf-8").write(text.replace(old, new))
    for t in tests:
        source, quick_args = TESTS[t]
        compiler, flags = flags_for(build_dir, source)
        exe = os.path.join(d, t)
        rc, out = run([cxx or compiler, f"-I{d}", *flags, *extra, os.path.join(root, source), "-o", exe], 600)
        if rc != 0:
            if name in DOES_NOT_BUILD:
                return name, "killed", f"{t} does not build"
            return name, "bad", f"{t} does not build: " + " ".join(out.split("\n")[:3])
        rc, out = run([exe, *(quick_args if quick else [])], 300)
        if rc is None:
            return name, "killed", f"{t} does not finish"
        if rc != 0:
            fails = [l for l in out.split("\n") if l.startswith("FAIL") or "fail" in l.lower()]
            return name, "killed", f"{t}: " + (fails[0][:100] if fails else f"exit {rc}")
    if name in DOES_NOT_BUILD:
        return name, "bad", "builds, but must not"
    return name, "survived", ""


def main():
    args = sys.argv[1:]
    jobs, quick, only = int(os.environ.get("MUTANT_JOBS", "4")), False, []
    build_dir = None
    while args:
        a = args.pop(0)
        if a == "-j":
            jobs = int(args.pop(0))
        elif a == "--quick":
            quick = True
        elif a == "--list":
            for n, (f, _, _, tests) in MUTANTS.items():
                print(f"{n:32} {f:20} {', '.join(tests)}")
            return 0
        elif build_dir is None:
            build_dir = os.path.abspath(a)
        else:
            only.append(a)
    if build_dir is None:
        sys.exit(__doc__)
    unknown = [n for n in only if n not in MUTANTS]
    if unknown:
        sys.exit("unknown mutant: " + " ".join(unknown))
    names = only or list(MUTANTS)
    scratch = os.environ.get("MUT_DIR") or tempfile.mkdtemp(prefix="strsim-mut-")
    os.makedirs(scratch, exist_ok=True)
    cxx = os.environ.get("CXX")
    counts = {"killed": 0, "survived": 0, "bad": 0, "skipped": 0}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(check, n, MUTANTS[n], scratch, build_dir, cxx, quick) for n in names]
        for f in concurrent.futures.as_completed(futures):
            name, state, why = f.result()
            counts[state] += 1
            label = {"killed": "killed", "survived": "SURVIVED", "bad": "BAD MUTANT", "skipped": "skipped"}[state]
            print(f"{label}: {name}" + (f" ({why})" if why else ""), flush=True)
    if not os.environ.get("MUT_DIR"):
        shutil.rmtree(scratch, ignore_errors=True)
    print(f"{counts['killed']} killed, {counts['survived']} survived, {counts['bad']} bad, {counts['skipped']} skipped of {len(names)}")
    return 1 if counts["survived"] or counts["bad"] else 0


if __name__ == "__main__":
    sys.exit(main())
