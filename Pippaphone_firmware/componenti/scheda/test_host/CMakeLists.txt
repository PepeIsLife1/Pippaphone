# Test sul PC del GPS del T-Deck: l'analizzatore NMEA (satelliti con segnale, scadenze, contatori) e il
# pilota del ricevitore (src/tdeck/gnss_pilota.cpp: riconoscimento, standby e sveglia coi loro tempi, PMREQ
# ignorato o perso, cane da guardia, freddo, versione, prova al banco, storia) con un u-blox finto
# (finto.cpp). Nessun hardware: l'orologio e la seriale sono finti.
#
#   cmake -S componenti/scheda/test_host -B <cartella> -G Ninja
#   cmake --build <cartella>
#   <cartella>/gnss_test [filtro]          # per esempio: gnss_test nmea_   oppure   gnss_test gps_prova
#
# Su Windows con MSYS2 (UCRT64: g++ e ninja in C:\msys64\ucrt64\bin nel PATH). Usa il mini-framework dei
# test del progetto (test/testing.h e test/main.cpp). Non e' un componente di ESP-IDF: il CMakeLists.txt
# del componente (la cartella sopra) non guarda qui dentro.
cmake_minimum_required(VERSION 3.16)
project(gnss_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SCHEDA ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(TEST_PROGETTO ${CMAKE_CURRENT_SOURCE_DIR}/../../../test)

add_executable(gnss_test
    ${TEST_PROGETTO}/main.cpp
    finto.cpp
    test_nmea.cpp
    test_pilota.cpp
    ${SCHEDA}/src/tdeck/gnss_pilota.cpp)
# stub/ ha il sdkconfig.h col T-Deck acceso (scheda/gnss.h lo include).
target_include_directories(gnss_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub ${SCHEDA}/include ${SCHEDA}/src/tdeck
                           ${TEST_PROGETTO} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(gnss_test PRIVATE -Wall -Wextra -Wno-unused-parameter -g -O2 -D_GLIBCXX_ASSERTIONS)

# I sanitizer: completi se il compilatore li ha; se no (il g++ di MSYS2 non ha libasan e libubsan) almeno
# UBSan in modo "trap", che non vuole librerie.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=address,undefined)
check_cxx_source_compiles("int main() { return 0; }" GNSS_SANITIZER_COMPLETI)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined -fsanitize-undefined-trap-on-error")
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=undefined -fsanitize-undefined-trap-on-error)
check_cxx_source_compiles("int main() { return 0; }" GNSS_SANITIZER_TRAP)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(GNSS_SANITIZER_COMPLETI)
    target_compile_options(gnss_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(gnss_test PRIVATE -fsanitize=address,undefined)
elseif(GNSS_SANITIZER_TRAP)
    target_compile_options(gnss_test PRIVATE -fsanitize=undefined -fsanitize-undefined-trap-on-error)
    target_link_options(gnss_test PRIVATE -fsanitize=undefined -fsanitize-undefined-trap-on-error)
endif()

# Il sorgente del firmware anche con le opzioni del firmware (quelle di ESP-IDF per l'ESP32-S3: gnu++2b,
# senza eccezioni e RTTI, -Werror=all) e in piu' -Werror: un avviso qui e' un errore anche sul T-Deck.
add_library(gnss_come_firmware OBJECT ${SCHEDA}/src/tdeck/gnss_pilota.cpp)
target_include_directories(gnss_come_firmware PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub ${SCHEDA}/include)
set_target_properties(gnss_come_firmware PROPERTIES CXX_STANDARD 23 CXX_EXTENSIONS ON)
target_compile_options(gnss_come_firmware PRIVATE -O2 -fno-exceptions -fno-rtti -Wall -Werror=all -Wextra
                       -Wno-unused-parameter -Werror)
add_dependencies(gnss_test gnss_come_firmware)
