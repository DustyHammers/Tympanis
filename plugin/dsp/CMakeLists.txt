# tympanis_engine — the modal drum engine (C++ port of prototype/engine.py; no JUCE dependency).

add_library(tympanis_engine STATIC
    Json.cpp
    KitData.cpp
    Engine.cpp
)

target_include_directories(tympanis_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tympanis_engine PUBLIC cxx_std_17)
set_target_properties(tympanis_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(tympanis_engine PRIVATE -O3 -fno-fast-math -Wall -Wextra)
    # '#pragma omp simd' loops (SIMD across modes and noise bands; no OpenMP runtime). PUBLIC
    # because Filters.h has some of them inline.
    target_compile_options(tympanis_engine PUBLIC -fopenmp-simd)
endif()

# Same rule as Cymbalis: the x86_64 slice never uses AVX (Rosetta 2).
if(APPLE)
    target_compile_options(tympanis_engine PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()
