// Flussi NMEA realistici per provare sul PC l'analizzatore e il pilota del GPS del T-Deck.
// Ogni frase si scrive senza '$' e senza checksum: frase() aggiunge "$", "*XX" e "\r\n".
// (Dall'indagine sul GPS del 15/11/2026: u-blox M10 SPG 5.10, Quectel L76K, AT6558R della PCB.)
#pragma once
#include <cstdio>
#include <string>
#include <vector>

inline std::string frase(const std::string& corpo, bool minuscolo = false) {
    unsigned s = 0;
    for (char c : corpo) s ^= (unsigned char)c;
    char cs[8];
    std::snprintf(cs, sizeof cs, minuscolo ? "*%02x\r\n" : "*%02X\r\n", s);
    return "$" + corpo + cs;
}

inline std::string flusso(const std::vector<std::string>& corpi) {
    std::string out;
    for (const auto& c : corpi) out += frase(c);
    return out;
}

// --- Frasi copiate dalla documentazione u-blox M10 SPG 5.10 (UBX-21035062), con il loro checksum ---
inline const char* const DOC_UBLOX[] = {
    "$GPGSV,3,1,09,09,,,17,10,,,40,12,,,49,13,,,35,1*6F\r\n",
    "$GPGSV,3,2,09,15,,,44,17,,,45,19,,,44,24,,,50,1*64\r\n",
    "$GPGSV,3,3,09,25,,,40,1*6E\r\n",
    "$GPGSV,1,1,03,12,,,42,24,,,47,32,,,37,5*66\r\n",
    "$GAGSV,1,1,00,2*76\r\n",
    "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B\r\n",
    "$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A,V*57\r\n",
    "$GNRMC,014230.00,A,4722.80340,N,00831.68218,E,0.000,,120477,,,A,V*14\r\n",
    "$GPGLL,,,,,,V,N*64\r\n",
};

// --- u-blox M10 (SPG 5.10, default: GPS+SBAS, Galileo, QZSS; NMEA 4.11) ---
inline std::vector<std::string> ubloxAvvio() {
    return {"GNTXT,01,01,02,u-blox AG - www.u-blox.com", "GNTXT,01,01,02,HW UBX 10 000A0000",
            "GNTXT,01,01,02,FWVER=SPG 5.10",            "GNTXT,01,01,02,PROTVER=34.10",
            "GNTXT,01,01,02,MOD=MIA-M10Q",              "GNTXT,01,01,02,GPS;GLO;GAL;BDS",
            "GNTXT,01,01,02,SBAS;QZSS",                 "GNTXT,01,01,02,ANTSTATUS=DONTKNOW"};
}
// Partenza da freddo: ora sconosciuta (campi vuoti), nessun satellite.
inline std::vector<std::string> ubloxFreddo() {
    return {"GNRMC,,V,,,,,,,,,,N,V",
            "GNVTG,,,,,,,,,N",
            "GNGGA,,,,,,0,00,99.99,,,,,,",
            "GNGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99,1",
            "GNGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99,3",
            "GNGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99,5",
            "GPGSV,1,1,00,1",
            "GAGSV,1,1,00,7",
            "GQGSV,1,1,00,1",
            "GNGLL,,,,,,V,N"};
}
// Ricerca: 9 satelliti elencati (senza elevazione: non ancora noti), 4 con segnale, il migliore a 24.
inline std::vector<std::string> ubloxRicerca() {
    return {"GNRMC,,V,,,,,,,,,,N,V",
            "GNGGA,,,,,,0,00,99.99,,,,,,",
            "GPGSV,2,1,06,05,,,21,13,,,,15,,,18,18,,,,1",
            "GPGSV,2,2,06,20,,,24,29,,,,1",
            "GAGSV,1,1,03,04,,,,11,,,19,36,,,,7",
            "GQGSV,1,1,00,1"};
}
// Come alla scrivania: 18 satelliti "in vista" (previsti dall'almanacco), solo 2 deboli, ora sconosciuta.
inline std::vector<std::string> ubloxScrivania() {
    return {"GNRMC,,V,,,,,,,,,,N,V",
            "GNGGA,,,,,,0,00,99.99,,,,,,",
            "GPGSV,3,1,11,02,45,123,,05,67,301,14,07,12,045,,08,30,200,,1",
            "GPGSV,3,2,11,13,55,089,,14,20,310,,15,33,150,,18,08,270,,1",
            "GPGSV,3,3,11,20,61,010,12,29,40,240,,30,15,120,,1",
            "GAGSV,2,1,07,02,25,060,,04,50,180,,09,10,330,,11,70,090,,7",
            "GAGSV,2,2,07,12,35,210,,19,15,280,,36,45,020,,7",
            "GQGSV,1,1,00,1"};
}
// Ora nota, ancora senza posizione.
inline std::vector<std::string> ubloxOraSenzaFix() {
    return {"GNRMC,101530.00,V,,,,,,,151126,,,N,V", "GNGGA,101530.00,,,,,0,00,99.99,,,,,,"};
}
// Posizione (coordinate dell'esempio u-blox): 8 usati, 11 con segnale, il migliore a 44, 16 elencati.
inline std::vector<std::string> ubloxFix() {
    return {"GNRMC,101545.00,A,4717.11437,N,00833.91522,E,0.004,77.52,151126,,,A,V",
            "GNVTG,77.52,T,,M,0.004,N,0.008,K,A",
            "GNGGA,101545.00,4717.11437,N,00833.91522,E,1,08,1.01,499.6,M,48.0,M,,",
            "GNGSA,A,3,05,13,15,20,29,,,,,,,,1.85,1.01,1.55,1",
            "GNGSA,A,3,04,11,36,,,,,,,,,,1.85,1.01,1.55,3",
            "GPGSV,3,1,10,05,67,301,38,13,55,089,41,15,33,150,35,18,08,270,,1",
            "GPGSV,3,2,10,20,61,010,44,29,40,240,39,30,15,120,22,02,45,123,,1",
            "GPGSV,3,3,10,07,12,045,,08,30,200,18,1",
            "GAGSV,2,1,06,04,50,180,40,11,70,090,43,36,45,020,37,09,10,330,,7",
            "GAGSV,2,2,06,12,35,210,25,19,15,280,,7",
            "GQGSV,1,1,00,1",
            "GNGLL,4717.11437,N,00833.91522,E,101545.00,A,A"};
}
// u-blox con GLONASS e BeiDou accesi (altra configurazione): GLGSV e GBGSV.
inline std::vector<std::string> ubloxGloBds() {
    return {"GLGSV,2,1,05,65,40,100,31,66,60,200,,72,20,300,27,81,35,040,,1",
            "GLGSV,2,2,05,82,15,150,,1",
            "GBGSV,1,1,03,19,50,120,29,20,30,220,,46,70,300,33,1"};
}

// --- Quectel L76K (CASIC, NMEA 4.1): GP/BD/GL, segnale "0" in fondo alle GSV ---
inline std::vector<std::string> l76kFreddo() {
    return {"GPTXT,01,01,02,MA=CASIC",
            "GPTXT,01,01,01,ANTENNA OPEN",
            "GNGGA,000003.000,,,,,0,00,25.5,,,,,,",
            "GNGLL,,,,,000003.000,V,N",
            "GNGSA,A,1,,,,,,,,,,,,,25.5,25.5,25.5,1",
            "GNGSA,A,1,,,,,,,,,,,,,25.5,25.5,25.5,4",
            "GPGSV,1,1,00,0",
            "BDGSV,1,1,00,0",
            "GNRMC,000003.000,V,,,,,,,060180,,,N,V",
            "GNVTG,,,,,,,,,N",
            "GNZDA,000003.000,06,01,1980,00,00"};
}
inline std::vector<std::string> l76kRicerca() {
    return {"GNGGA,101533.000,,,,,0,00,25.5,,,,,,",
            "GPGSV,3,1,09,02,,,23,05,,,,13,,,30,15,,,,0",
            "GPGSV,3,2,09,18,,,,20,,,27,25,,,,29,,,19,0",
            "GPGSV,3,3,09,30,,,,0",
            "BDGSV,1,1,02,08,,,21,13,,,,0",
            "GNRMC,101533.000,V,,,,,,,151126,,,N,V"};
}
inline std::vector<std::string> l76kFix() {
    return {"GNGGA,101550.000,4717.1144,N,00833.9152,E,1,06,1.6,499.6,M,48.0,M,,",
            "GNGSA,A,3,02,05,13,20,,,,,,,,,2.9,1.6,2.4,1",
            "GNGSA,A,3,08,13,,,,,,,,,,,2.9,1.6,2.4,4",
            "GPGSV,3,1,09,02,45,123,33,05,67,301,30,13,55,089,38,15,33,150,,0",
            "GPGSV,3,2,09,18,08,270,,20,61,010,36,25,20,100,,29,40,240,24,0",
            "GPGSV,3,3,09,30,15,120,,0",
            "BDGSV,1,1,02,08,50,120,31,13,30,220,28,0",
            "GNRMC,101550.000,A,4717.1144,N,00833.9152,E,0.00,0.00,151126,,,A,V",
            "GNVTG,0.00,T,,M,0.00,N,0.00,K,A",
            "GNZDA,101550.000,15,11,2026,00,00",
            "GPTXT,01,01,01,ANTENNA OK"};
}

// --- AT6558R della PCB (CASIC, 9600 baud): stesso stile, qui in variante NMEA 3.0 (senza segnale) ---
inline std::vector<std::string> at6558Fix() {
    return {"GNGGA,101550.000,4717.1144,N,00833.9152,E,1,05,2.1,499.6,M,48.0,M,,",
            "GPGSV,3,1,10,02,45,123,33,05,67,301,30,13,55,089,38,15,33,150,",
            "GPGSV,3,2,10,18,08,270,,20,61,010,36,25,20,100,,29,40,240,24",
            "GPGSV,3,3,10,30,15,120,,31,05,010,",
            "BDGSV,1,1,03,08,50,120,31,13,30,220,28,14,10,045,",
            "GNRMC,101550.000,A,4717.1144,N,00833.9152,E,0.00,0.00,151126,,,A",
            "GPTXT,01,01,01,ANTENNA OK"};
}
