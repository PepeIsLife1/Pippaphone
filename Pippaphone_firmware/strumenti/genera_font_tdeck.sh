#!/bin/sh
# Font dell'interfaccia del LilyGO T-Deck Plus (display 2,8" 320x240): gli stessi caratteri della
# PCB, piu' piccoli in pixel ma della stessa grandezza vera (il T-Deck ha circa 143 punti per
# pollice contro i 200 della PCB). Nell'interfaccia i nomi dei font della PCB (font_22, icone_18...)
# sul T-Deck puntano a questi (vedi ui/tema.h): font_22 -> td_16, icone_22 -> tdi_16 e cosi' via.
#
# I font della PCB non si toccano: li rigenera solo genera_font.sh.
# Servono Node.js e lv_font_conv (la stessa versione usata per la PCB: npx lv_font_conv@1.5.2).
# I file .c risultanti sono gia' nel repository: questo script serve solo se si cambiano.
set -e
cd "$(dirname "$0")"
OUT=../telefono/main/ui/font
CONV=${LV_FONT_CONV:-npx --yes lv_font_conv@1.5.2}
TXT="0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x201D,0x2022,0x2026,0x20AC"
# Icone della PCB piu': cancella (tasto ⌫), mappa, mirino, puntina.
ICONE="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650,0xF0C0,0xF0AC,0xF3C5,0xF130,0xF519,0xF007,0xF2B9,0xF023,0xF3ED,0xF560,0xF017,0xF1D8,0xF14E,0xF3DD,0xF060,0xF234,0xF084,0xF1DE,0xF7C0,0xF2BD,0xF06A,0xF55A,0xF279,0xF05B,0xF276"

testo() { # nome dimensione file_ttf
  $CONV --bpp 4 --size $2 --no-compress --font "font/$3" -r "$TXT" --format lvgl -o "$OUT/$1.c" --force-fast-kern-format
}
icone() { # nome dimensione
  $CONV --bpp 4 --size $2 --no-compress --font font/FontAwesome5-Solid+Brands+Regular.woff -r "$ICONE" --format lvgl -o "$OUT/$1.c"
}

testo td_12 12 AtkinsonHyperlegible-Regular.ttf
testo td_14 14 AtkinsonHyperlegible-Regular.ttf
testo td_16 16 AtkinsonHyperlegible-Regular.ttf
testo td_16b 16 AtkinsonHyperlegible-Bold.ttf
testo td_20b 20 AtkinsonHyperlegible-Bold.ttf
testo td_24b 24 AtkinsonHyperlegible-Bold.ttf
testo td_34b 34 AtkinsonHyperlegible-Bold.ttf
testo tdm_12 12 IBMPlexMono-SemiBold.ttf
testo tdm_14 14 IBMPlexMono-SemiBold.ttf
testo tdm_20 20 IBMPlexMono-SemiBold.ttf
# Orologio grande: solo le cifre e i due punti.
$CONV --bpp 4 --size 56 --no-compress --font font/IBMPlexMono-SemiBold.ttf -r "0x30-0x3A" --format lvgl -o "$OUT/tdm_56.c"
icone tdi_14 14
icone tdi_16 16
icone tdi_20 20
icone tdi_32 32
# Le icone come "riserva" dei font di testo: se un carattere manca, LVGL lo cerca li'.
riserva() { # font icone
  sed -i "s|    .dsc = \&font_dsc           /\*The custom font data.*|    .dsc = \&font_dsc,\n    .fallback = \&$2,|" "$OUT/$1.c"
  # dichiarazione del font di riserva una sola volta, dopo gli include
  sed -i "0,/^#endif\$/s||#endif\n\nextern const lv_font_t $2;|" "$OUT/$1.c"
}
riserva td_12 tdi_14
riserva td_14 tdi_14
riserva td_16 tdi_16
riserva td_16b tdi_16
riserva td_20b tdi_20
riserva td_24b tdi_20
riserva td_34b tdi_32
echo fatto
