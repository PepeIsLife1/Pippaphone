# minimp3 (https://github.com/lieff/minimp3, commit afb604c), decodificatore MP3 in un file solo.
# Pubblico dominio (CC0, vedi LICENSE). Lo usa il lettore musicale del T-Deck.
idf_component_register(SRCS "minimp3.c" INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-function -Wno-maybe-uninitialized)
