cmake_minimum_required(VERSION 3.14)
project(string_similarity LANGUAGES CXX)

include(FetchContent)

# --- Dependency: strings ---
# basic_string_metric.h includes "strings.hh" for strings::upper (its
# case-folding step). It lives in the sibling strings library, pulled in here so
# the include resolves. It is pinned by commit (the commit soundex pins too), so a
# change in strings reaches this library when the pin moves. We declare it before
# soundex so this declaration wins: soundex also
# FetchContent_Declare's strings, and FetchContent dedups by the declared name,
# keeping the first declaration.
FetchContent_Declare(
  strings
  GIT_REPOSITORY https://github.com/Kronuz/strings.git
  GIT_TAG        5c98ec5a9ac21feeb3d271fcacbc0b4328ec8c26
)

# --- Dependency: soundex ---
# phonetic_metric.h is a Soundex-backed metric: it encodes both inputs with a
# soundex encoder, then defers to an inner string metric. The concrete encoders
# (SoundexEnglish in "english_soundex.h", etc.) live in the sibling soundex
# library, pulled in here so those includes resolve for consumers (and our test /
# demo). soundex itself depends on strings; declaring strings above makes that
# transitive dependency resolve to one copy.
FetchContent_Declare(
  soundex
  GIT_REPOSITORY https://github.com/Kronuz/soundex.git
  GIT_TAG        fac413e9ee4545dd82488f6e38e2a74910a67d43
)

FetchContent_MakeAvailable(strings soundex)

# --- Library target ---
# string-similarity is header-only: the metrics are a CRTP base (StringMetric<Impl>)
# plus per-algorithm subclasses, all templates / inline in headers. So the target
# is a pure INTERFACE library that just puts the source dir on the consumer's
# include path.
add_library(string_similarity INTERFACE)
target_include_directories(string_similarity INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(string_similarity INTERFACE cxx_std_20)

# Link strings and soundex INTERFACE so "strings.hh" and the soundex headers
# ride onto the consumer's include path: both are part of the public surface
# (basic_string_metric.h includes strings.hh; a consumer of phonetic_metric.h
# instantiates it with a soundex encoder), so a consumer needs them resolvable.
target_link_libraries(string_similarity INTERFACE strings::strings soundex::soundex)

# Alias so consumers can link `string_similarity::string_similarity` whether they add
# this via add_subdirectory or FetchContent.
add_library(string_similarity::string_similarity ALIAS string_similarity)

# Only build the test and demo when this repo is the top-level project, so
# consumers (e.g. via FetchContent) don't build them.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(string_similarity_test test/test.cc)
  target_link_libraries(string_similarity_test PRIVATE string_similarity)
  add_test(NAME string_similarity COMMAND string_similarity_test)

  # Differential test: the optimized kernels must match the frozen legacy
  # implementation (test/legacy_metrics.h) bit for bit.
  add_executable(string_similarity_differential test/differential.cc)
  target_link_libraries(string_similarity_differential PRIVATE string_similarity)
  add_test(NAME string_similarity_differential COMMAND string_similarity_differential)

  # Correctness test: every metric against independent textbook oracles (test/oracle.h).
  add_executable(string_similarity_correctness test/correctness.cc)
  target_link_libraries(string_similarity_correctness PRIVATE string_similarity)
  add_test(NAME string_similarity_correctness COMMAND string_similarity_correctness)

  # Long-input Levenshtein test: the banded, multi-column kernel against a textbook
  # dynamic program and the frozen previous kernel (test/frozen_levenshtein.h), at
  # lengths around the word edges and up to 10000.
  add_executable(string_similarity_levenshtein test/levenshtein_long.cc)
  target_link_libraries(string_similarity_levenshtein PRIVATE string_similarity)
  add_test(NAME string_similarity_levenshtein COMMAND string_similarity_levenshtein)

  # The usage snippet of docs/API.md compiles and says what it claims.
  add_executable(string_similarity_api_doc test/api_doc.cc)
  target_link_libraries(string_similarity_api_doc PRIVATE string_similarity)
  add_test(NAME string_similarity_api_doc COMMAND string_similarity_api_doc)

  # Copies and moves of bound metrics (a copy must not point into its source), from
  # non-const lvalues, const lvalues and rvalues, and through vector regrowth.
  add_executable(string_similarity_copy test/copy_move.cc)
  target_link_libraries(string_similarity_copy PRIVATE string_similarity)
  add_test(NAME string_similarity_copy COMMAND string_similarity_copy)

  # The protected members a subclass reads (the bound string, the case flag, the costs, the
  # Winkler factors, the encoder): the test stops building when one stops being accessible.
  add_executable(string_similarity_subclass test/subclass_compat.cc)
  target_link_libraries(string_similarity_subclass PRIVATE string_similarity)
  add_test(NAME string_similarity_subclass COMMAND string_similarity_subclass)

  # The per-thread mask scratch is capped (1 MiB by default): strings whose masks need
  # more get a block of their own. Built twice, with the default cap and with a cap of
  # 256 words that sends strings of a few hundred characters down the same path.
  add_executable(string_similarity_scratch test/scratch.cc)
  target_link_libraries(string_similarity_scratch PRIVATE string_similarity)
  add_test(NAME string_similarity_scratch COMMAND string_similarity_scratch)
  add_executable(string_similarity_scratch_small test/scratch.cc)
  target_link_libraries(string_similarity_scratch_small PRIVATE string_similarity)
  target_compile_definitions(string_similarity_scratch_small PRIVATE STRSIM_MAX_SCRATCH_WORDS=256)
  add_test(NAME string_similarity_scratch_small COMMAND string_similarity_scratch_small)

  # Several threads at once: the scratch and the bigram table are per thread.
  find_package(Threads REQUIRED)
  add_executable(string_similarity_threads test/threads.cc)
  target_link_libraries(string_similarity_threads PRIVATE string_similarity Threads::Threads)
  add_test(NAME string_similarity_threads COMMAND string_similarity_threads)

  # A runnable tour of the library (not a test): build & run ./build/string_similarity_demo
  add_executable(string_similarity_demo examples/demo.cc)
  target_link_libraries(string_similarity_demo PRIVATE string_similarity)
endif()

# rapidfuzz-cpp is a comparator: the cross-check test and the benchmark fetch it, the
# library target never depends on it. The cross-check (Levenshtein, Jaro and
# Jaro-Winkler against rapidfuzz on random pairs) is a CTest test; turn it off to
# configure without network access to GitHub's rapidfuzz repository.
option(STRING_SIMILARITY_RAPIDFUZZ_CHECK "Test the metrics against rapidfuzz-cpp (fetches it)" ON)
option(STRING_SIMILARITY_BUILD_BENCH "Build bench/bench.cc (fetches rapidfuzz-cpp)" OFF)
if((STRING_SIMILARITY_RAPIDFUZZ_CHECK OR STRING_SIMILARITY_BUILD_BENCH) AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  FetchContent_Declare(
    rapidfuzz
    GIT_REPOSITORY https://github.com/rapidfuzz/rapidfuzz-cpp.git
    GIT_TAG        v3.3.4
  )
  FetchContent_MakeAvailable(rapidfuzz)
endif()

if(STRING_SIMILARITY_RAPIDFUZZ_CHECK AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(string_similarity_rapidfuzz_check test/rapidfuzz_check.cc)
  target_link_libraries(string_similarity_rapidfuzz_check PRIVATE string_similarity rapidfuzz::rapidfuzz)
  target_compile_options(string_similarity_rapidfuzz_check PRIVATE -O2)
  add_test(NAME string_similarity_rapidfuzz_check COMMAND string_similarity_rapidfuzz_check)
endif()

if(STRING_SIMILARITY_BUILD_BENCH AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(string_similarity_bench bench/bench.cc)
  target_link_libraries(string_similarity_bench PRIVATE string_similarity rapidfuzz::rapidfuzz)
  target_compile_options(string_similarity_bench PRIVATE -O3)
endif()
