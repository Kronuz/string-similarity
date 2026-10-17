cmake_minimum_required(VERSION 3.14)
project(string_similarity LANGUAGES CXX)

include(FetchContent)

# --- Dependency: strings ---
# basic_string_metric.h includes "strings.hh" for strings::upper (its
# case-folding step). It lives in the sibling strings library, pulled in here so
# the include resolves. We track its tip with GIT_TAG main, like the rest of the
# family. We declare it before soundex so this declaration wins: soundex also
# FetchContent_Declare's strings, and FetchContent dedups by the declared name,
# keeping the first declaration.
FetchContent_Declare(
  strings
  GIT_REPOSITORY https://github.com/Kronuz/strings.git
  GIT_TAG        b42d3c4ce1130043a381e487ae0a693248c2c0f5
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
  GIT_TAG        fd0e06991e9593f895177e5cb7603e9cd7fb4aa9
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

  # A runnable tour of the library (not a test): build & run ./build/string_similarity_demo
  add_executable(string_similarity_demo examples/demo.cc)
  target_link_libraries(string_similarity_demo PRIVATE string_similarity)
endif()
