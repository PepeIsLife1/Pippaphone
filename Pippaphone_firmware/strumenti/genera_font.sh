#!/bin/sh
# Rigenera i font dell'interfaccia (servono Node.js e lv_font_conv: npm install -g lv_font_conv).
# I file .c risultanti sono gia' nel repository: questo script serve solo se si cambiano.
set -e
cd "$(dirname "$0")"
OUT=../telefono/main/ui/font
CONV=${LV_FONT_CONV:-lv_font_conv}
TXT="0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x201D,0x2022,0x2026,0x20AC"
# Icone (Font Awesome 5, licenza SIL OFL / CC BY 4.0): quelle di LVGL piu' alcune in piu'.
ICONE="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650,0xF0C0,0xF0AC,0xF3C5,0xF130,0xF519,0xF007,0xF2B9,0xF023,0xF3ED,0xF560,0xF017,0xF1D8,0xF14E,0xF3DD,0xF060,0xF234,0xF084,0xF1DE,0xF7C0,0xF2BD,0xF06A"

testo() { # nome dimensione file_ttf
  $CONV --bpp 4 --size $2 --no-compress --font "font/$3" -r "$TXT" --format lvgl -o "$OUT/$1.c" --force-fast-kern-format
}
icone() { # nome dimensione
  $CONV --bpp 4 --size $2 --no-compress --font font/FontAwesome5-Solid+Brands+Regular.woff -r "$ICONE" --format lvgl -o "$OUT/$1.c"
}

testo font_16 16 AtkinsonHyperlegible-Regular.ttf
testo font_18 18 AtkinsonHyperlegible-Regular.ttf
testo font_20 20 AtkinsonHyperlegible-Regular.ttf
testo font_22 22 AtkinsonHyperlegible-Regular.ttf
testo font_22b 22 AtkinsonHyperlegible-Bold.ttf
testo font_26b 26 AtkinsonHyperlegible-Bold.ttf
testo font_32b 32 AtkinsonHyperlegible-Bold.ttf
testo font_48b 48 AtkinsonHyperlegible-Bold.ttf
testo mono_16 16 IBMPlexMono-SemiBold.ttf
testo mono_20 20 IBMPlexMono-SemiBold.ttf
testo mono_28 28 IBMPlexMono-SemiBold.ttf
$CONV --bpp 4 --size 104 --no-compress --font font/IBMPlexMono-SemiBold.ttf -r "0x30-0x3A" --format lvgl -o "$OUT/mono_104.c"
icone icone_18 18
icone icone_22 22
icone icone_28 28
icone icone_44 44
# Le icone come "riserva" dei font di testo: se un carattere manca, LVGL lo cerca li'.
riserva() { # font icone
  sed -i "s|    .dsc = \&font_dsc           /\*The custom font data.*|    .dsc = \&font_dsc,\n    .fallback = \&$2,|" "$OUT/$1.c"
  # dichiarazione del font di riserva una sola volta, dopo gli include
  sed -i "0,/^#endif\$/s||#endif\n\nextern const lv_font_t $2;|" "$OUT/$1.c"
}
riserva font_16 icone_18
riserva font_18 icone_18
riserva font_20 icone_18
riserva font_22 icone_22
riserva font_22b icone_22
riserva font_26b icone_28
riserva font_32b icone_28
riserva font_48b icone_44
echo fatto
