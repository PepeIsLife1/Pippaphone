// Cuffie USB-C: stranezze note dei dispositivi USB Audio, per VID:PID (vedi uac_desc.h).
// Niente ESP-IDF: si compila anche nei test sul PC.
//
// Ogni voce ha la sua fonte. Quasi tutte vengono dalla tabella quirk_flags_table di Linux (sound/usb/quirks.c,
// ramo master, letta il 2026-11-14; nei commenti il nome del dispositivo e le bandierine come le scrive Linux),
// tradotte cosi':
//  - QUIRK_FLAG_CTL_MSG_DELAY (20 ms), _DELAY_5M (5 ms), _DELAY_1M (1 ms): QUIRK_PAUSA_20MS, _5MS, _1MS. In
//    Linux la pausa e' dopo ogni richiesta di controllo della classe (snd_usb_ctl_msg_quirk), come qui;
//  - QUIRK_FLAG_SKIP_CLOCK_SELECTOR: QUIRK_NON_TOCCARE_SELETTORE;
//  - QUIRK_FLAG_SET_IFACE_FIRST: QUIRK_INTERFACCIA_PRIMA.
// Le altre bandierine di Linux qui non hanno un equivalente (GET_SAMPLE_RATE, MIXER_PLAYBACK_MIN_MUTE,
// IFACE_DELAY, ...): il volume vero lo fa il software, e dopo SET_CUR la frequenza non si rilegge con nessun
// dispositivo (Linux la salta solo per quelli con GET_SAMPLE_RATE).
// Non ci sono i dispositivi che non possono suonare (le webcam, solo microfono) ne' quelli che vogliono cose
// che qui non ci sono (Zoom R16/24: QUIRK_FLAG_TX_LENGTH, una lunghezza davanti a ogni pacchetto).
#include "usb_audio/uac_desc.h"

namespace uac {

namespace {

struct Voce {
    uint16_t vid;
    uint16_t pid;
    uint32_t quirk;
};

// Dispositivi, per VID:PID.
constexpr Voce DISPOSITIVI[] = {
    // MS USB Link headset: GET_SAMPLE_RATE | CTL_MSG_DELAY | DISABLE_AUTOSUSPEND.
    {0x045e, 0x083c, QUIRK_PAUSA_20MS},
    // Samsung USBC Headset (AKG): SKIP_CLOCK_SELECTOR | CTL_MSG_DELAY_5M. Ha due selettori di clock e, se li si
    // imposta, smette di funzionare (lkml 2021-07, "Samsung clock selectors"; studio uac.md).
    {0x04e8, 0xa051, QUIRK_PAUSA_5MS | QUIRK_NON_TOCCARE_SELETTORE},
    // Sony WALKMAN NW-A45 DAC: SET_IFACE_FIRST.
    {0x054c, 0x0b8c, QUIRK_INTERFACCIA_PRIMA},
    // Apple: adattatore USB-C - jack 3,5 mm (05ac:110a) ed EarPods USB-C (05ac:110b). Non e' una voce di Linux:
    // Apple dichiara al massimo 24 bit / 48 kHz e il titolo del kernel bugzilla 217501 dice che l'adattatore
    // rifiuta 44,1 kHz (studio uac.md). Solo 48 kHz, che e' comunque la prima scelta.
    {0x05ac, 0x110a, QUIRK_SOLO_48K},
    {0x05ac, 0x110b, QUIRK_SOLO_48K},
    // SPACETOUCH USB Audio: FORCE_IFACE_RESET | IFACE_DELAY | CTL_MSG_DELAY_5M.
    {0x0666, 0x0880, QUIRK_PAUSA_5MS},
    // MOTU M Series 2nd hardware revision: CTL_MSG_DELAY_1M.
    {0x07fd, 0x000b, QUIRK_PAUSA_1MS},
    // Kingston HyperX: CTL_MSG_DELAY_1M.
    {0x0951, 0x16ad, QUIRK_PAUSA_1MS},
    // Jabra 550a: CTL_MSG_DELAY_1M.
    {0x0b0e, 0x0349, QUIRK_PAUSA_1MS},
    // C-Media: CTL_MSG_DELAY_1M | MIXER_PLAYBACK_MIN_MUTE.
    {0x0d8c, 0x0014, QUIRK_PAUSA_1MS},
    // LH Labs Geek Out HD Audio 1V5: SET_IFACE_FIRST.
    {0x2522, 0x0007, QUIRK_INTERFACCIA_PRIMA},
    // VIVO USB-C HEADSET e VIVO USB-C-XE710 HEADSET: CTL_MSG_DELAY_1M.
    {0x2d95, 0x8011, QUIRK_PAUSA_1MS},
    {0x2d95, 0x8021, QUIRK_PAUSA_1MS},
    // MOONDROP Moonriver2 Ti: CTL_MSG_DELAY.
    {0x2fc6, 0xf06b, QUIRK_PAUSA_20MS},
    // iBasso DC-Elite e iBasso DC07 Pro: CTL_MSG_DELAY_1M.
    {0x2fc6, 0xf0b5, QUIRK_PAUSA_1MS},
    {0x2fc6, 0xf0b7, QUIRK_PAUSA_1MS},
};

struct VoceProduttore {
    uint16_t vid;
    uint32_t quirk;
};

// Produttori interi (VENDOR_FLG di Linux): valgono per ogni prodotto che non e' nella tabella sopra.
constexpr VoceProduttore PRODUTTORI[] = {
    // Logitech: CTL_MSG_DELAY_1M (cuffie e webcam).
    {0x046d, QUIRK_PAUSA_1MS},
    // Plantronics: GET_SAMPLE_RATE | CTL_MSG_DELAY.
    {0x047f, QUIRK_PAUSA_20MS},
    // Focusrite Novation: SKIP_CLOCK_SELECTOR | SKIP_IFACE_SETUP.
    {0x1235, QUIRK_NON_TOCCARE_SELETTORE},
};

}  // namespace

uint32_t trovaQuirk(uint16_t vid, uint16_t pid) {
    for (const Voce& v : DISPOSITIVI)
        if (v.vid == vid && v.pid == pid) return v.quirk;
    for (const VoceProduttore& v : PRODUTTORI)
        if (v.vid == vid) return v.quirk;
    return 0;
}

}  // namespace uac
