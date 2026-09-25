# Codec 2 (https://github.com/drowe67/codec2), solo il codificatore vocale.
# Licenza GNU LGPL 2.1 (vedi COPYING): i codebook sono generati dal progetto originale.
file(GLOB C2_SRCS "src/*.c")
idf_component_register(SRCS ${C2_SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-variable -Wno-unused-but-set-variable
                       -Wno-maybe-uninitialized -Wno-unused-function -Wno-stringop-overflow)
# Codebook "const": restano in flash invece di occupare 126 KB di RAM interna.
target_compile_definitions(${COMPONENT_LIB} PRIVATE __EMBEDDED__)
