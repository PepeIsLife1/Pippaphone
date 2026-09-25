// Colori, font e stili dell'interfaccia (vedi la proposta grafica).
//
// Il pannello ha rosso e blu a 3 bit (i bit bassi sono collegati ai piu' alti), quindi in
// pratica 5 livelli: 0, 64, 128, 192, 255. Il verde ha tutti i 6 bit. Si usano solo colori
// fatti con questi livelli, niente sfumature.
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(font_16);
LV_FONT_DECLARE(font_18);
LV_FONT_DECLARE(font_20);
LV_FONT_DECLARE(font_22);
LV_FONT_DECLARE(font_22b);
LV_FONT_DECLARE(font_26b);
LV_FONT_DECLARE(font_32b);
LV_FONT_DECLARE(font_48b);
LV_FONT_DECLARE(mono_16);
LV_FONT_DECLARE(mono_20);
LV_FONT_DECLARE(mono_28);
LV_FONT_DECLARE(mono_104);
LV_FONT_DECLARE(icone_18);
LV_FONT_DECLARE(icone_22);
LV_FONT_DECLARE(icone_28);
LV_FONT_DECLARE(icone_44);

namespace ui {

// Colori.
inline lv_color_t NERO() { return lv_color_hex(0x000000); }
inline lv_color_t GRIGIO1() { return lv_color_hex(0x404040); }  // superfici, divisori
inline lv_color_t GRIGIO2() { return lv_color_hex(0x808080); }  // testo terziario
inline lv_color_t GRIGIO3() { return lv_color_hex(0xC0C0C0); }  // testo secondario
inline lv_color_t BIANCO() { return lv_color_hex(0xFFFFFF); }
inline lv_color_t AMBRA() { return lv_color_hex(0xFFB000); }    // elemento selezionato
inline lv_color_t AMBRA_SCURO() { return lv_color_hex(0x805000); }  // nuvoletta inviata
inline lv_color_t VERDE() { return lv_color_hex(0x00C040); }
inline lv_color_t ROSSO() { return lv_color_hex(0xFF4040); }
inline lv_color_t ROSSO_SCURO() { return lv_color_hex(0xC00000); }
inline lv_color_t BLU() { return lv_color_hex(0x40A0FF); }
// Colori degli avatar.
lv_color_t avatarColor(uint16_t number);

// Icone (Font Awesome, stesse di LVGL piu' qualcuna).
#define IC_BUSTA "\xEF\x83\xA0"       // F0E0
#define IC_MICROFONO "\xEF\x84\xB0"   // F130
#define IC_PERSONA "\xEF\x80\x87"     // F007
#define IC_GRUPPO "\xEF\x83\x80"      // F0C0
#define IC_GLOBO "\xEF\x82\xAC"       // F0AC
#define IC_PUNTO "\xEF\x8F\x85"       // F3C5
#define IC_ATTENZIONE "\xEF\x81\xB1"  // F071
#define IC_IMPOSTAZIONI "\xEF\x87\x9E"  // F1DE
#define IC_TORRE "\xEF\x94\x99"       // F519
#define IC_OK "\xEF\x80\x8C"          // F00C
#define IC_DOPPIO_OK "\xEF\x95\xA0"   // F560
#define IC_OROLOGIO "\xEF\x80\x97"    // F017
#define IC_LUCCHETTO "\xEF\x80\xA3"   // F023
#define IC_SCUDO "\xEF\x8F\xAD"       // F3ED
#define IC_WIFI "\xEF\x87\xAB"        // F1EB
#define IC_GPS "\xEF\x84\xA4"         // F124
#define IC_BUSSOLA "\xEF\x85\x8E"     // F14E
#define IC_INDIETRO "\xEF\x81\xA0"    // F060
#define IC_PIU "\xEF\x81\xA7"         // F067
#define IC_CESTINO "\xEF\x8B\xAD"     // F2ED
#define IC_MATITA "\xEF\x8C\x84"      // F304
#define IC_VOLUME "\xEF\x80\xA8"      // F028
#define IC_PLAY "\xEF\x81\x8B"        // F04B
#define IC_CHIAVE "\xEF\x82\x84"      // F084
#define IC_CARICA "\xEF\x83\xA7"      // F0E7
#define IC_BATT_PIENA "\xEF\x89\x80"
#define IC_BATT_3 "\xEF\x89\x81"
#define IC_BATT_2 "\xEF\x89\x82"
#define IC_BATT_1 "\xEF\x89\x83"
#define IC_BATT_0 "\xEF\x89\x84"
#define IC_TELEFONO "\xEF\x82\x95"    // F095
#define IC_AGGIUNGI_PERSONA "\xEF\x88\xB4"  // F234
#define IC_INFO "\xEF\x81\xAA"        // F06A
#define IC_SCARICA "\xEF\x80\x99"     // F019

// Stili comuni (creati una volta).
void initStyles();

// Oggetti di base, tutti senza bordi/scorrimento se non richiesti.
lv_obj_t* box(lv_obj_t* parent);                                // contenitore trasparente
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "");
lv_obj_t* column(lv_obj_t* parent, int gap = 0);               // flex verticale
lv_obj_t* row(lv_obj_t* parent, int gap = 0);                  // flex orizzontale
void fill(lv_obj_t* o, lv_color_t color);                       // sfondo pieno
void radius(lv_obj_t* o, int r);
void pad(lv_obj_t* o, int top, int right, int bottom, int left);
void grow(lv_obj_t* o);                                         // flex-grow 1

}  // namespace ui
