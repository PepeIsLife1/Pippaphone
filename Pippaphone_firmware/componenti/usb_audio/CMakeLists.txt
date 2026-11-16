# Cuffie USB-C del T-Deck Plus: driver USB Audio (classe 1.0 e 2.0, solo riproduzione) sulla USB host di
# ESP-IDF (componente "usb", quello dentro ESP-IDF: niente componenti scaricati). Le parti senza ESP-IDF
# (descrittori, ricampionatore, coda, volume) si provano anche sul PC: vedi test/CMakeLists.txt.
#
# Solo il T-Deck. Sulla PCB il componente resta vuoto (solo gli header, nessun sorgente) e non porta dentro
# il componente "usb": la PCB compila e collega esattamente quello di prima.
#
# Le dipendenze (REQUIRES) pero' ESP-IDF le legge PRIMA di avere la configurazione: questo file viene letto
# una prima volta da solo ("early expansion", CMAKE_BUILD_EARLY_EXPANSION), quando CONFIG_SCHEDA_TDECK_PLUS
# non esiste ancora, e lo dice anche la documentazione ("REQUIRES and PRIV_REQUIRES should not depend on any
# configuration options", api-guides/build-system.rst). Li' la scheda si legge direttamente dai file di
# configurazione, come fa kconfgen: prima i sdkconfig.defaults usati (piu' quelli ".esp32s3", se ci sono),
# poi sdkconfig, che vince. Nella seconda lettura, quella vera, si usa CONFIG_SCHEDA_TDECK_PLUS e si
# controlla che le due cose vadano d'accordo.

# ON se la configurazione che sta per essere usata e' quella del T-Deck Plus.
function(usb_audio_scheda_tdeck risultato)
    idf_build_get_property(defaults SDKCONFIG_DEFAULTS)
    idf_build_get_property(sdkconfig SDKCONFIG)
    idf_build_get_property(target IDF_TARGET)
    set(file_config)
    foreach(f ${defaults})
        list(APPEND file_config "${f}")
        if(EXISTS "${f}.${target}")
            list(APPEND file_config "${f}.${target}")
        endif()
    endforeach()
    list(APPEND file_config "${sdkconfig}")
    set(tdeck OFF)
    foreach(f ${file_config})
        if(EXISTS "${f}")
            file(STRINGS "${f}" righe REGEX "CONFIG_SCHEDA_(TDECK_PLUS|PIPPAPHONE)")
            foreach(r ${righe})
                if(r MATCHES "^CONFIG_SCHEDA_TDECK_PLUS=y")
                    set(tdeck ON)
                elseif(r MATCHES "^CONFIG_SCHEDA_PIPPAPHONE=y" OR r MATCHES "^# CONFIG_SCHEDA_TDECK_PLUS is not set")
                    set(tdeck OFF)
                endif()
            endforeach()
        endif()
    endforeach()
    set(${risultato} ${tdeck} PARENT_SCOPE)
endfunction()

if(CMAKE_BUILD_EARLY_EXPANSION)
    usb_audio_scheda_tdeck(tdeck)
else()
    set(tdeck OFF)
    if(CONFIG_SCHEDA_TDECK_PLUS)
        set(tdeck ON)
        # Il componente usb entra nella build solo se la prima lettura ha visto il T-Deck.
        idf_build_get_property(componenti BUILD_COMPONENTS)
        if(NOT "usb" IN_LIST componenti)
            message(FATAL_ERROR "usb_audio: la configurazione e' quella del T-Deck Plus ma il componente usb "
                    "di ESP-IDF non e' nella build (prima della configurazione sdkconfig diceva un'altra "
                    "scheda). Basta configurare di nuovo: idf.py reconfigure, oppure cancellare la cartella "
                    "di build.")
        endif()
    endif()
endif()

set(srcs)
set(requires)
if(tdeck)
    set(srcs "src/uac_desc.cpp"
             "src/quirk.cpp"
             "src/dsp.cpp"
             "src/uac_host.cpp")
    # Pubblico: uac_host.h include usb/usb_host.h, quindi chi la usa (app/cuffie.cpp) vede gli header di usb.
    set(requires usb)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES ${requires})
