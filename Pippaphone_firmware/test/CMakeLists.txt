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
