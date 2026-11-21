# Test del protocollo PippaNet sul PC (non serve l'ESP32).
#   cmake -S test -B build-test && cmake --build build-test && ./build-test/pippanet_test
cmake_minimum_required(VERSION 3.16)
project(pippanet_test CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PN ${CMAKE_CURRENT_SOURCE_DIR}/../componenti/pippanet)
set(MC ${CMAKE_CURRENT_SOURCE_DIR}/../componenti/monocypher/src)

add_executable(pippanet_test
    main.cpp
    platform_host.cpp
    test_base.cpp
    test_sim.cpp
    ${PN}/src/packet.cpp
    ${PN}/src/charset.cpp
    ${PN}/src/crypto.cpp
    ${PN}/src/airtime.cpp
    ${PN}/src/payloads.cpp
    ${PN}/src/node.cpp
    ${PN}/src/repeater.cpp
    ${PN}/src/phone.cpp
    ${MC}/monocypher.c
    ${MC}/optional/monocypher-ed25519.c)

target_include_directories(pippanet_test PRIVATE ${PN}/include ${MC} ${MC}/optional ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(pippanet_test PRIVATE -Wall -Wextra -Wno-unused-parameter -g -fsanitize=address,undefined)
target_link_options(pippanet_test PRIVATE -fsanitize=address,undefined)

# Cuffie USB-C (T-Deck): le parti del componente usb_audio senza ESP-IDF (descrittori USB Audio, quirk,
# ricampionatore, coda, pacchetti, volume).
#   cmake -S test -B build-test -G Ninja && cmake --build build-test --target usb_audio_test
#   ./build-test/usb_audio_test [filtro]
# I sanitizer solo se il compilatore li ha: il g++ di MSYS2 (ucrt64) non ha libasan e libubsan.
set(UA ${CMAKE_CURRENT_SOURCE_DIR}/../componenti/usb_audio)
add_executable(usb_audio_test
    main.cpp
    platform_host.cpp
    test_uac_desc.cpp
    test_dsp.cpp
    ${UA}/src/uac_desc.cpp
    ${UA}/src/quirk.cpp
    ${UA}/src/dsp.cpp)
target_include_directories(usb_audio_test PRIVATE ${UA}/include ${PN}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(usb_audio_test PRIVATE -Wall -Wextra -Wno-unused-parameter -g)
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address,undefined")
check_cxx_source_compiles("int main() { return 0; }" USB_AUDIO_SANITIZER)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(USB_AUDIO_SANITIZER)
    target_compile_options(usb_audio_test PRIVATE -fsanitize=address,undefined)
    target_link_options(usb_audio_test PRIVATE -fsanitize=address,undefined)
endif()

# GPS del T-Deck: la politica che decide quando tenerlo acceso (telefono/main/app/gps_politica.h: avvio,
# ricerche periodiche, schermate, richieste, grazia), un giro al secondo come in main.cpp.
#   cmake --build build-test --target gps_politica_test && ./build-test/gps_politica_test
add_executable(gps_politica_test
    main.cpp
    platform_host.cpp
    test_gps_politica.cpp)
target_include_directories(gps_politica_test PRIVATE ${PN}/include ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../telefono/main)
target_compile_options(gps_politica_test PRIVATE -Wall -Wextra -Wno-unused-parameter -g)
if(USB_AUDIO_SANITIZER)
    target_compile_options(gps_politica_test PRIVATE -fsanitize=address,undefined)
    target_link_options(gps_politica_test PRIVATE -fsanitize=address,undefined)
endif()

# Mappe dal computer a flusso (T-Deck): il lettore dei byte della pagina del flasher
# (telefono/main/app/flusso_mappe.h). Con PN_FLUSSO=<file> legge anche un flusso registrato dalla pagina.
#   cmake --build build-test --target flusso_mappe_test && ./build-test/flusso_mappe_test
add_executable(flusso_mappe_test
    main.cpp
    platform_host.cpp
    test_flusso_mappe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../telefono/main/app/flusso_mappe.cpp)
target_include_directories(flusso_mappe_test PRIVATE ${PN}/include ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../telefono/main)
target_compile_options(flusso_mappe_test PRIVATE -Wall -Wextra -Wno-unused-parameter -g)
if(USB_AUDIO_SANITIZER)
    target_compile_options(flusso_mappe_test PRIVATE -fsanitize=address,undefined)
    target_link_options(flusso_mappe_test PRIVATE -fsanitize=address,undefined)
endif()
