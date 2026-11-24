# tympanis_render — console renderer (engine checks and the CPU figure; no JUCE).

add_executable(tympanis_render main.cpp)
target_link_libraries(tympanis_render PRIVATE tympanis_engine)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(tympanis_render PRIVATE -O2 -Wall -Wextra)
endif()
if(APPLE)
    target_compile_options(tympanis_render PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()
