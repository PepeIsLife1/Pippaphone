# Simulatore dell'interfaccia del telefono sul PC (Windows con MSYS2, oppure Linux).
#   cmake -S strumenti/simulatore -B build-sim -G Ninja && cmake --build build-sim
#   build-sim/sim_tdeck screenshot-tdeck     (320x240, LilyGO T-Deck Plus)
#   build-sim/sim_pcb   screenshot-pcb       (480x480, PCB Pippaphone)
# Per confrontare l'interfaccia con un'altra versione dei sorgenti: -DUI_DIR=<cartella main>
# -DSCHEDA_DIR=<cartella del componente scheda>.
cmake_minimum_required(VERSION 3.16)
project(pippaphone_simulatore C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(UI_DIR ${FW}/telefono/main CACHE PATH "cartella main del telefono (app/ e ui/)")
set(SCHEDA_DIR ${FW}/componenti/scheda CACHE PATH "componente scheda")
set(SIM ${CMAKE_CURRENT_SOURCE_DIR})

# LVGL con lv_conf.h del simulatore.
file(GLOB_RECURSE LVGL_SRCS ${FW}/componenti/lvgl/src/*.c)
add_library(lvgl_sim STATIC ${LVGL_SRCS})
target_include_directories(lvgl_sim PUBLIC ${FW}/componenti/lvgl ${SIM})
target_compile_definitions(lvgl_sim PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)
target_compile_options(lvgl_sim PRIVATE -w)

# Protocollo PippaNet (per i tipi usati dai dati) e Monocypher.
set(PN ${FW}/componenti/pippanet)
set(MC ${FW}/componenti/monocypher/src)
add_library(pn_sim STATIC
    ${PN}/src/packet.cpp ${PN}/src/charset.cpp ${PN}/src/crypto.cpp ${PN}/src/airtime.cpp
    ${PN}/src/payloads.cpp ${PN}/src/node.cpp ${PN}/src/repeater.cpp ${PN}/src/phone.cpp
    ${MC}/monocypher.c ${MC}/optional/monocypher-ed25519.c)
target_include_directories(pn_sim PUBLIC ${PN}/include ${MC} ${MC}/optional)

function(simulatore nome tdeck)
    file(GLOB UI_SRCS ${UI_DIR}/ui/*.cpp)
    file(GLOB FONT_SRCS ${UI_DIR}/ui/font/*.c)
    add_executable(${nome}
        ${SIM}/sim_main.cpp ${SIM}/sim_rtos.cpp ${SIM}/sim_scheda.cpp ${SIM}/sim_app.cpp ${SIM}/png.cpp
        ${UI_SRCS} ${FONT_SRCS}
        ${UI_DIR}/app/dati.cpp ${UI_DIR}/app/eventi.cpp
        ${SCHEDA_DIR}/src/tastiera.cpp)
    target_include_directories(${nome} PRIVATE ${SIM}/stub ${SIM} ${UI_DIR} ${SCHEDA_DIR}/include
                               ${FW}/componenti/pippa_radio/include)
    target_compile_definitions(${nome} PRIVATE _POSIX_THREAD_SAFE_FUNCTIONS)
    if(tdeck)
        target_compile_definitions(${nome} PRIVATE CONFIG_SCHEDA_TDECK_PLUS=1)
    endif()
    target_compile_options(${nome} PRIVATE -include ${SIM}/sim_compat.h -Wno-psabi)
    target_link_libraries(${nome} lvgl_sim pn_sim)
endfunction()

simulatore(sim_pcb OFF)
simulatore(sim_tdeck ON)
