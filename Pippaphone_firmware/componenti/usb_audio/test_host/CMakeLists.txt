# Test sul PC delle parti di usb_audio senza ESP-IDF che riguardano i descrittori: lettura dei descrittori
# USB Audio 1.0 e 2.0, strada del suono, clock, scelta del formato e quirk (src/uac_desc.cpp, src/quirk.cpp).
# Con i descrittori veri di cuffie e adattatori (descrittori_veri.h), i casi rotti e un fuzzer.
#
#   cmake -S componenti/usb_audio/test_host -B <cartella> -G Ninja
#   cmake --build <cartella>
#   <cartella>/uac_desc_test [filtro]
#
# Variabili d'ambiente: PN_SEED (seme dei numeri casuali, come gli altri test), UAC_FUZZ_MS (quanto dura il
# fuzzer, 3000 ms se manca).
#
# Il programma usa il mini-framework dei test del progetto (test/testing.h e test/main.cpp). Non e' un
# componente di ESP-IDF: il CMakeLists.txt del componente (la cartella sopra) non guarda qui dentro.
cmake_minimum_required(VERSION 3.16)
project(uac_desc_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(UA ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(TEST_PROGETTO ${CMAKE_CURRENT_SOURCE_DIR}/../../../test)

add_executable(uac_desc_test
    ${TEST_PROGETTO}/main.cpp
    aiuti.cpp
    test_descrittori.cpp
    test_casi.cpp
    test_fuzz.cpp
    ${UA}/src/uac_desc.cpp
    ${UA}/src/quirk.cpp)
target_include_directories(uac_desc_test PRIVATE ${UA}/include ${TEST_PROGETTO} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(uac_desc_test PRIVATE -Wall -Wextra -Wno-unused-parameter -g -O2 -D_GLIBCXX_ASSERTIONS)

# I sanitizer. Completi (AddressSanitizer e UBSan) se il compilatore li ha; se no (il g++ di MSYS2 non ha libasan
# e libubsan) almeno UBSan in modo "trap", che non vuole librerie: indici degli array fuori, shift e overflow con
# segno fermano il programma con un'istruzione illegale. Le letture fuori dal buffer dei descrittori le trovano
# comunque le pagine proibite di aiuti.h.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=address,undefined)
check_cxx_source_compiles("int main() { return 0; }" UAC_SANITIZER_COMPLETI)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined -fsanitize-undefined-trap-on-error")
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=undefined -fsanitize-undefined-trap-on-error)
check_cxx_source_compiles("int main() { return 0; }" UAC_SANITIZER_TRAP)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(UAC_SANITIZER_COMPLETI)
    target_compile_options(uac_desc_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(uac_desc_test PRIVATE -fsanitize=address,undefined)
elseif(UAC_SANITIZER_TRAP)
    target_compile_options(uac_desc_test PRIVATE -fsanitize=undefined -fsanitize-undefined-trap-on-error)
    target_link_options(uac_desc_test PRIVATE -fsanitize=undefined -fsanitize-undefined-trap-on-error)
endif()

# I due sorgenti del firmware anche con le opzioni del firmware (quelle di ESP-IDF per l'ESP32-S3: gnu++2b,
# senza eccezioni e RTTI, -Werror=all) e in piu' -Werror: un avviso qui e' un errore anche sul T-Deck.
add_library(uac_desc_come_firmware OBJECT ${UA}/src/uac_desc.cpp ${UA}/src/quirk.cpp)
target_include_directories(uac_desc_come_firmware PRIVATE ${UA}/include)
set_target_properties(uac_desc_come_firmware PROPERTIES CXX_STANDARD 23 CXX_EXTENSIONS ON)
target_compile_options(uac_desc_come_firmware PRIVATE -O2 -fno-exceptions -fno-rtti -Wall -Werror=all -Wextra
                       -Wno-unused-parameter -Werror)
add_dependencies(uac_desc_test uac_desc_come_firmware)
