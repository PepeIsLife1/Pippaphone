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

# Parti dell'app compilate cosi' come sono (le altre sono finte, in sim_app.cpp). Quelle nuove del
# 13/11/2026 (orologio, giochi, musica) ci sono solo nei sorgenti di oggi: con -DUI_DIR verso una
# versione vecchia si saltano, e SIM_NUOVE dice al copione di non aprire le schermate che mancano.
set(APP_SRCS ${UI_DIR}/app/dati.cpp ${UI_DIR}/app/eventi.cpp)
set(APP_NUOVE ${UI_DIR}/app/orologio.cpp ${UI_DIR}/app/giochi.cpp)
set(SIM_NUOVE OFF)
foreach(f ${APP_NUOVE})
    if(EXISTS ${f})
        list(APPEND APP_SRCS ${f})
        set(SIM_NUOVE ON)
    endif()
endforeach()

function(simulatore nome tdeck)
    file(GLOB UI_SRCS ${UI_DIR}/ui/*.cpp)
    file(GLOB FONT_SRCS ${UI_DIR}/ui/font/*.c)
    add_executable(${nome}
        ${SIM}/sim_main.cpp ${SIM}/sim_rtos.cpp ${SIM}/sim_scheda.cpp ${SIM}/sim_app.cpp ${SIM}/png.cpp
        ${UI_SRCS} ${FONT_SRCS}
        ${APP_SRCS}
        ${SCHEDA_DIR}/src/tastiera.cpp)
    if(SIM_NUOVE)
        target_compile_definitions(${nome} PRIVATE SIM_NUOVE=1)
    endif()
    target_include_directories(${nome} PRIVATE ${SIM}/stub ${SIM} ${UI_DIR} ${SCHEDA_DIR}/include
                               ${FW}/componenti/pippa_radio/include)
    target_compile_definitions(${nome} PRIVATE _POSIX_THREAD_SAFE_FUNCTIONS)
    if(tdeck)
        target_compile_definitions(${nome} PRIVATE CONFIG_SCHEDA_TDECK_PLUS=1)
    endif()
    target_compile_options(${nome} PRIVATE -include ${SIM}/sim_compat.h -Wno-psabi)
    target_link_libraries(${nome} lvgl_sim pn_sim)
endfunction()

if(NOT EMSCRIPTEN)
    simulatore(sim_pcb OFF)
    simulatore(sim_tdeck ON)
else()
    # Simulatore nel browser (T-Deck Plus): una pagina HTML sola, col WebAssembly dentro.
    #   emcmake cmake -S strumenti/simulatore -B build-web -G Ninja && cmake --build build-web
    #   -> build-web/pippaphone_tdeck.html
    file(GLOB UI_SRCS ${UI_DIR}/ui/*.cpp)
    file(GLOB FONT_SRCS ${UI_DIR}/ui/font/*.c)
    add_executable(pippaphone_tdeck
        ${SIM}/web/sim_web.cpp ${SIM}/web/rete_web.cpp
        ${SIM}/sim_main.cpp ${SIM}/sim_rtos.cpp ${SIM}/sim_scheda.cpp ${SIM}/sim_app.cpp ${SIM}/png.cpp
        ${UI_SRCS} ${FONT_SRCS}
        ${APP_SRCS}
        ${SCHEDA_DIR}/src/tastiera.cpp ${SCHEDA_DIR}/src/tdeck/tastiera_tdeck.cpp)
    if(SIM_NUOVE)
        target_compile_definitions(pippaphone_tdeck PRIVATE SIM_NUOVE=1)
    endif()
    target_include_directories(pippaphone_tdeck PRIVATE ${SIM}/stub ${SIM} ${UI_DIR} ${SCHEDA_DIR}/include
                               ${FW}/componenti/pippa_radio/include)
    target_compile_definitions(pippaphone_tdeck PRIVATE CONFIG_SCHEDA_TDECK_PLUS=1 SIM_WEB=1)
    target_compile_options(pippaphone_tdeck PRIVATE -include ${SIM}/sim_compat.h)
    target_link_libraries(pippaphone_tdeck lvgl_sim pn_sim)
    set_target_properties(pippaphone_tdeck PROPERTIES SUFFIX ".html")
    set_property(TARGET pippaphone_tdeck APPEND PROPERTY LINK_DEPENDS ${SIM}/web/shell.html)
    target_link_options(pippaphone_tdeck PRIVATE
        -O2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1048576
        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sENVIRONMENT=web -sSINGLE_FILE=1 -sSINGLE_FILE_BINARY_ENCODE=0 -sMINIFY_HTML=0
        -sEXPORTED_FUNCTIONS=_main,_web_matrix,_web_trackball,_web_click,_web_cmd
        -sEXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU16
        --shell-file ${SIM}/web/shell.html)
endif()
