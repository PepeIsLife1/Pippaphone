# Test del DSP delle cuffie USB-C (usb_audio/dsp.h) sul PC, da soli e ottimizzati: gli stessi test di
# test/test_dsp.cpp (che girano anche in usb_audio_test, test/CMakeLists.txt), ma senza il resto del componente
# (i descrittori, che possono essere a meta' lavoro) e con -O2, come il firmware. In piu' dsp_bench, i tempi.
# Non e' un componente di ESP-IDF: IDF guarda solo le cartelle subito sotto componenti/.
#
# Con il g++ di MSYS2 (Git Bash):
#   export PATH="/c/msys64/ucrt64/bin:$PATH"
#   "/c/Program Files/CMake/bin/cmake.exe" -S componenti/usb_audio/test_host_dsp -B <cartella> -G Ninja \
#       -DCMAKE_CXX_COMPILER=g++
#   "/c/Program Files/CMake/bin/cmake.exe" --build <cartella>
#   <cartella>/dsp_test [filtro]    i test (tutti si chiamano dsp_...)
#   <cartella>/dsp_bench            nanosecondi per campione sul PC
cmake_minimum_required(VERSION 3.16)
project(usb_audio_dsp_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(UA ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(T ${UA}/../../test)
set(PN ${UA}/../pippanet)

find_package(Threads REQUIRED)

# main.cpp (il mini-framework) e platform_host.cpp (seedRandom) sono quelli di test/.
add_executable(dsp_test
    ${T}/main.cpp
    ${T}/platform_host.cpp
    ${T}/test_dsp.cpp
    ${UA}/src/dsp.cpp)
target_include_directories(dsp_test PRIVATE ${UA}/include ${PN}/include ${T})
target_compile_options(dsp_test PRIVATE -Wall -Wextra -Wno-unused-parameter -g -O2)
target_link_libraries(dsp_test PRIVATE Threads::Threads)

add_executable(dsp_bench
    bench_dsp.cpp
    ${UA}/src/dsp.cpp)
target_include_directories(dsp_bench PRIVATE ${UA}/include)
target_compile_options(dsp_bench PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
