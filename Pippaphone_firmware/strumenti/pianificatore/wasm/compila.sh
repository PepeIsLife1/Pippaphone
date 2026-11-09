#!/bin/sh
# Compila il simulatore PippaNet in WebAssembly per il pianificatore (serve emsdk).
#   source /c/emsdk/emsdk_env.sh      (una volta per terminale)
#   sh strumenti/pianificatore/wasm/compila.sh
# Esce: strumenti/pianificatore/web/pippanet.js + pippanet.wasm
# I sorgenti del protocollo si leggono da componenti/pippanet senza modificarli; gli unici file
# diversi sono include/pippanet/airtime.h (parametri radio variabili), trovato per primo, e
# airtime.cpp (tempo in aria giusto anche a SF5/SF6).
# Prova sul PC (MinGW o gcc), stessi sorgenti con il main di prova:
#   g++ -std=c++17 -O2 -I wasm/include -I ../../componenti/pippanet/include -I ../../componenti/monocypher/src \
#     -I ../../componenti/monocypher/src/optional wasm/simulatore.cpp wasm/airtime.cpp <sorgenti pippanet senza airtime.cpp> \
#     <monocypher compilato in C> -o simulatore && ./simulatore 7 20
set -e
QUI=$(cd "$(dirname "$0")" && pwd)
FW="$QUI/../../.."
PN="$FW/componenti/pippanet"
MC="$FW/componenti/monocypher/src"
EMXX=${EMXX:-em++}
EMCC=${EMCC:-emcc}
TMP="$QUI/build"
mkdir -p "$TMP"

"$EMCC" -O2 -c "$MC/monocypher.c" -o "$TMP/monocypher.o"
"$EMCC" -O2 -I "$MC" -c "$MC/optional/monocypher-ed25519.c" -o "$TMP/monocypher-ed25519.o"
"$EMXX" -std=c++17 -O2 \
  -I "$QUI/include" -I "$PN/include" -I "$MC" -I "$MC/optional" \
  "$QUI/simulatore.cpp" \
  "$PN/src/packet.cpp" "$PN/src/charset.cpp" "$PN/src/crypto.cpp" "$QUI/airtime.cpp" \
  "$PN/src/payloads.cpp" "$PN/src/node.cpp" "$PN/src/repeater.cpp" "$PN/src/phone.cpp" \
  "$TMP/monocypher.o" "$TMP/monocypher-ed25519.o" \
  -sMODULARIZE=1 -sEXPORT_NAME=CreaPippaNet -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS=_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=cwrap,UTF8ToString,HEAP32 \
  -o "$QUI/../web/pippanet.js"
echo "fatto: $QUI/../web/pippanet.js"
