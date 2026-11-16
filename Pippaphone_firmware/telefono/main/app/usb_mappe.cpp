#include "app/usb_mappe.h"

#include "sdkconfig.h"

#if CONFIG_SCHEDA_TDECK_PLUS
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/aggiorna.h"
#include "app/cuffie.h"
#include "app/eventi.h"
#include "app/memoria.h"
#include "app/rete.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "lwip/sockets.h"
#include "scheda/gnss.h"
#include "scheda/scheda.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {

namespace {
const char* TAG = "usb";
constexpr size_t MAX_FILE = 1024 * 1024;
// Il file arriva a pezzi da 4 kB, ognuno confermato con "@PIPPA +": il driver della USB butta i
// byte se il suo buffer si riempie, quindi chi manda deve aspettare.
constexpr size_t PEZZO = 4096;
constexpr uint32_t ATTESA_MS = 5000;

uint32_t g_tab[256];

void crcInit() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_tab[i] = c;
    }
}

// CRC-32 come zlib.
uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = g_tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

// Risposta: una riga sola (una printf sola, cosi' non si mescola col log degli altri compiti).
void rispondi(const char* fmt, ...) {
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    printf("@PIPPA %s\n", b);
    fflush(stdout);
}

// Riga di testo dalla USB, senza \r\n. Con attesa = portMAX_DELAY si aspetta finche' arriva; se no
// false quando per attesa non arriva niente (la riga a meta' si butta).
bool riga(char* out, size_t cap, TickType_t attesa) {
    size_t n = 0;
    for (;;) {
        uint8_t c;
        if (usb_serial_jtag_read_bytes(&c, 1, attesa) <= 0) {
            if (attesa == portMAX_DELAY) continue;
            out[0] = 0;
            return false;
        }
        if (c == '\n') {
            if (n && out[n - 1] == '\r') n--;
            out[n] = 0;
            return true;
        }
        if (n + 1 < cap) out[n++] = char(c);
        else n = 0;  // riga troppo lunga: non e' un comando, si butta
    }
}

// Esattamente n byte (false se per ATTESA_MS non arriva niente).
bool leggi(uint8_t* p, size_t n) {
    size_t got = 0;
    while (got < n) {
        const int r = usb_serial_jtag_read_bytes(p + got, n - got, pdMS_TO_TICKS(ATTESA_MS));
        if (r <= 0) return false;
        got += size_t(r);
    }
    return true;
}

// "z/x/y.png" scritto esattamente cosi' (niente altro: niente "..", zeri davanti, segni), zoom 0-20.
bool percorso(const char* rel, int& z, long& x, long& y) {
    char g = 0;
    if (std::sscanf(rel, "%d/%ld/%ld.pn%c", &z, &x, &y, &g) != 4 || g != 'g') return false;
    char b[48];
    snprintf(b, sizeof(b), "%d/%ld/%ld.png", z, x, y);
    if (std::strcmp(b, rel) != 0) return false;
    return z >= 0 && z <= 20 && x >= 0 && y >= 0 && x < (1L << z) && y < (1L << z);
}

// Tutti gli n byte sul socket (send puo' mandarne meno).
bool inviaTutto(int s, const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    while (n) {
        const int r = send(s, c, n, 0);
        if (r <= 0) return false;
        c += r;
        n -= size_t(r);
    }
    return true;
}

// Prova completa dell'aggiornamento via Wi-Fi senza un altro apparecchio: il telefono accende la sua
// rete e manda a se' stesso (192.168.4.1: il traffico verso il proprio indirizzo torna indietro) il
// firmware che sta usando, come farebbe il browser. Se va tutto bene si riavvia dall'altra partizione
// con lo stesso firmware.
void provaOta() {
    const esp_partition_t* run = esp_ota_get_running_partition();
    esp_image_metadata_t md = {};
    const esp_partition_pos_t pos = {run->address, run->size};
    if (esp_image_get_metadata(&pos, &md) != ESP_OK || !md.image_len) {
        rispondi("ota errore non leggo il firmware attuale");
        return;
    }
    // otaStart e otaStop sempre in coppia: se la rete e' anche della schermata Aggiorna (aperta prima o
    // durante la prova) resta accesa per lei. A firmware scritto otaStop() fa ripartire il telefono.
    if (!otaStart(rete().number())) {
        rispondi("ota errore %s", otaStatus().error.c_str());
        return;
    }
    auto spegni = [] { otaStop(); };
    rispondi("ota mando %u byte da %s", unsigned(md.image_len), run->label);
    vTaskDelay(pdMS_TO_TICKS(1500));  // la rete del telefono prende l'indirizzo poco dopo l'avvio
    const int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(80);
    a.sin_addr.s_addr = inet_addr("192.168.4.1");
    if (s < 0 || connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        rispondi("ota errore non mi collego al server (errno %d)", errno);
        if (s >= 0) close(s);
        spegni();
        return;
    }
    timeval tv = {90, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char h[160];
    const int hl = snprintf(h, sizeof(h),
                            "POST /aggiorna HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: %u\r\n\r\n",
                            unsigned(md.image_len));
    bool ok = inviaTutto(s, h, size_t(hl));
    constexpr uint32_t BLOCCO = 4096;
    uint8_t* b = static_cast<uint8_t*>(heap_caps_malloc(BLOCCO, MALLOC_CAP_SPIRAM));
    ok = ok && b;
    const uint32_t t0 = uint32_t(esp_log_timestamp());
    for (uint32_t at = 0; ok && at < md.image_len;) {
        const uint32_t n = std::min<uint32_t>(BLOCCO, md.image_len - at);
        ok = esp_partition_read(run, at, b, n) == ESP_OK && inviaTutto(s, b, n);
        at += n;
        if (ok && at % (512 * 1024) == 0) rispondi("ota mandati %u kB", unsigned(at / 1024));
    }
    heap_caps_free(b);
    char r[320] = {};
    int got = 0;
    if (ok) {
        // La risposta arriva quando il server ha scritto e controllato tutto. Il server tiene aperta la
        // connessione: si legge finche' c'e' tutto il corpo (Content-Length).
        for (int k; got < int(sizeof(r)) - 1 && (k = recv(s, r + got, sizeof(r) - 1 - got, 0)) > 0;) {
            got += k;
            r[got] = 0;
            const char* testa = std::strstr(r, "\r\n\r\n");
            const char* cl = std::strstr(r, "Content-Length:");
            if (testa && cl && cl < testa && int(testa + 4 - r) + std::atoi(cl + 15) <= got) break;
        }
    }
    close(s);
    const char* corpo = std::strstr(r, "\r\n\r\n");
    rispondi("ota %s in %u s: %s", ok ? "mandato" : "errore nell'invio",
             unsigned((uint32_t(esp_log_timestamp()) - t0) / 1000), corpo ? corpo + 4 : (got ? r : "(nessuna risposta)"));
    rispondi("ota stato: %s, RAM interna minima dall'avvio %u byte",
             otaStatus().done ? "scritto, riavvio" : otaStatus().error.c_str(),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
    spegni();  // se il firmware e' stato scritto, qui il telefono si riavvia
}

// Salva una tile: cartelle /mappe/z e /mappe/z/x, poi il file (prima .tmp, rinominato solo se
// scritto e chiuso tutto: una tile a meta' non resta mai col nome giusto). false con il passo che
// non e' riuscito e il motivo (errno della FatFs: per esempio "Input/output error" se la scheda non
// risponde, "Permission denied" se e' piena o la cartella non si puo' allargare).
bool salvaTile(int z, long x, long y, const uint8_t* dati, size_t n, char* motivo, size_t m) {
    char dir[64], path[80], tmp[84];
    auto errore = [&](const char* passo, const char* chi) {
        const int e = errno;
        // Se il percorso e' lungo il motivo si accorcia: va bene, l'inizio basta per capire.
        if (snprintf(motivo, m, "%s %s: %s, errno %d", passo, chi, std::strerror(e), e) >= int(m)) motivo[m - 1] = 0;
        return false;
    };
    // Una cartella che c'e' gia' va bene; un altro errore si dice subito (se no l'apertura dopo
    // direbbe solo "No such file or directory").
    auto cartella = [&](const char* d) { return mkdir(d, 0775) == 0 || errno == EEXIST; };
    if (!cartella(MAPPE)) return errore("cartella", MAPPE);
    snprintf(dir, sizeof(dir), "%s/%d", MAPPE, z);
    if (!cartella(dir)) return errore("cartella", dir);
    snprintf(dir, sizeof(dir), "%s/%d/%ld", MAPPE, z, x);
    if (!cartella(dir)) return errore("cartella", dir);
    snprintf(path, sizeof(path), "%s/%ld.png", dir, y);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    errno = 0;
    FILE* f = std::fopen(tmp, "wb");
    if (!f) return errore("apertura", tmp);
    const size_t scritti = std::fwrite(dati, 1, n, f);
    const int eScrittura = errno;
    if (std::fclose(f) != 0 || scritti != n) {  // la chiusura scrive gli ultimi pezzi: si controlla anche lei
        if (scritti != n) errno = eScrittura;
        errore(scritti != n ? "scrittura" : "chiusura", tmp);
        std::remove(tmp);
        return false;
    }
    if (std::remove(path) != 0 && errno != ENOENT) return errore("cancellazione", path);
    if (std::rename(tmp, path) != 0) return errore("rinomina", path);
    return true;
}

// I comandi del GPS stanno in funzioni a parte, mai copiate dentro compito() (noinline): le loro variabili
// (la Versione da mezzo kB, righe lunghe) occupano lo stack solo mentre girano. Dentro compito() lo
// occuperebbero per ogni comando, anche mentre @WIFI? e @OTA! accendono il Wi-Fi (lo stack del compito e' 6 kB).

// @GPS?: ricevitore, acceso, satelliti in vista e usati, posizione, ora e da quanti secondi e' arrivata
// l'ultima.
__attribute__((noinline)) void comandoGps() {
    const scheda::Fix f = scheda::gnss().fix();
    const uint64_t ms = uint64_t(esp_timer_get_time() / 1000);
    rispondi("gps %s acceso %d in vista %u usati %u posizione %d ora %d da %lu s unix %lu", scheda::gnss().modulo(),
             scheda::gnss().powered() ? 1 : 0, unsigned(f.inVista), unsigned(f.sats), f.valid ? 1 : 0,
             f.timeValid ? 1 : 0, f.timeValid ? (unsigned long)((ms - f.timeAtMs) / 1000) : 0UL,
             (unsigned long)f.unixTime);
    // Seconda riga (piu' lunga di quelle di rispondi()): ricevitore in standby, stato del pilota, ms
    // dall'ultima frase valida e dall'ultima RMC, GGA e GSV (-1 = mai), satelliti con segnale e C/N0 migliore
    // (dB-Hz), frasi buone e scartate, ultima sveglia (ms dal primo 0xFF alla prima frase, -1 = nessuna), PMREQ
    // rispettato o ignorato (? = mai provato), byte di stack del compito del GPS mai usati.
    const scheda::Gnss::Diagnosi d = scheda::gnss().diagnosi();
    auto eta = [ms](uint64_t t) { return t ? long(std::min<uint64_t>(ms > t ? ms - t : 0, 999999999)) : -1L; };
    char b[320];
    snprintf(b, sizeof(b),
             "@PIPPA gps2 standby %d stato %s frase %ld ms rmc %ld gga %ld gsv %ld segnale %u max %u ok %lu "
             "ko %lu sveglia %ld ms pmreq %s stack %lu\n",
             d.standby ? 1 : 0, scheda::Gnss::nomeStato(d.stato), eta(f.frasiAtMs), eta(f.rmcAtMs), eta(f.ggaAtMs),
             eta(f.gsvAtMs), unsigned(f.conSegnale), unsigned(f.snrMax), (unsigned long)f.frasiOk,
             (unsigned long)f.frasiKo, long(d.svegliaMs), d.pmreq > 0 ? "ok" : d.pmreq < 0 ? "ignorato" : "?",
             (unsigned long)d.stackLibero);
    fputs(b, stdout);
    fflush(stdout);
}

// @GPS!STORIA: gli ultimi eventi del GPS (acceso e spento, sveglie, standby, ricerca della velocita', prima ora
// e prima posizione, ogni minuto di ricerca), dal piu' vecchio, coi secondi dall'avvio.
__attribute__((noinline)) void comandoGpsStoria() {
    static scheda::Gnss::Evento ev[scheda::Gnss::STORIA];
    const int n = scheda::gnss().storia(ev, scheda::Gnss::STORIA);
    for (int i = 0; i < n; i++) {
        char t[128];
        scheda::Gnss::testoEvento(ev[i], t, sizeof(t));
        rispondi("gps storia %lu.%03lu %s", (unsigned long)(ev[i].ms / 1000), (unsigned long)(ev[i].ms % 1000), t);
    }
    const uint64_t ms = uint64_t(esp_timer_get_time() / 1000);
    rispondi("gps storia fine: %d eventi, adesso %lu.%03lu", n, (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
}

// Quante risposte a UBX-MON-VER e UBX-MON-HW3 sono arrivate dall'avvio. A parte: la Versione intera (mezzo kB)
// sta sullo stack solo qui dentro, non insieme a quella che comandoGpsVer() stampa.
__attribute__((noinline)) void risposteVersione(uint32_t& ver, uint32_t& hw3) {
    const scheda::Gnss::Versione v = scheda::gnss().versione();
    ver = v.risposteVer;
    hw3 = v.risposteHw3;
}

// @GPS!VER: versione del ricevitore (UBX-MON-VER: firmware, hardware, estensioni) e UBX-MON-HW3 (flag del
// quarzo RTC e dei pin). L'u-blox risponde solo da sveglio: se serve lo si accende per 30 s (il compito dei
// servizi lo accende entro un secondo).
__attribute__((noinline)) void comandoGpsVer() {
    if (!std::strcmp(scheda::gnss().modulo(), "L76K")) {
        rispondi("gps versione: solo con l'u-blox (questo e' un L76K)");
        return;
    }
    uint32_t primaVer, primaHw3;
    risposteVersione(primaVer, primaHw3);
    rete().wantGnss(30);
    scheda::gnss().chiediVersione();
    uint32_t nVer = primaVer, nHw3 = primaHw3;
    for (int i = 0; i < 150 && (nVer == primaVer || nHw3 == primaHw3); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        risposteVersione(nVer, nHw3);
    }
    const scheda::Gnss::Versione v = scheda::gnss().versione();
    const bool ver = v.risposteVer != primaVer, hw3 = v.risposteHw3 != primaHw3;
    if (!ver && !hw3) {
        rispondi("gps versione nessuna risposta (%s, stato %s)", scheda::gnss().modulo(),
                 scheda::Gnss::nomeStato(scheda::gnss().stato()));
        return;
    }
    if (ver) {
        rispondi("gps ver sw \"%s\" hw \"%s\" estensioni %u", v.sw, v.hw, unsigned(v.nEstensioni));
        for (int i = 0; i < v.nEstensioni; i++) rispondi("gps ver estensione \"%s\"", v.estensioni[i]);
    } else {
        rispondi("gps ver nessuna risposta");
    }
    if (hw3) {
        rispondi("gps hw3 flag 0x%02x rtcCalib %d safeBoot %d xtalAbsent %d pin %u", unsigned(v.hw3Flags),
                 v.hw3Flags & 1, (v.hw3Flags >> 1) & 1, (v.hw3Flags >> 2) & 1, unsigned(v.hw3Pin));
        // I dati com'e' arrivati (versione, pin...), in esadecimale: una riga lunga.
        char b[sizeof(v.hw3Dati) * 2 + 32];
        int k = snprintf(b, sizeof(b), "@PIPPA gps hw3 dati ");
        for (int i = 0; i < v.hw3Len && k + 3 < int(sizeof(b)); i++) k += snprintf(b + k, sizeof(b) - k, "%02x", v.hw3Dati[i]);
        if (k + 2 <= int(sizeof(b))) {
            b[k++] = '\n';
            b[k] = 0;
        }
        fputs(b, stdout);
        fflush(stdout);
    } else {
        rispondi("gps hw3 nessuna risposta");
    }
}

// @GPS!PROVA: prova al banco dello standby dell'u-blox, "@GPS!PROVA [cicli 1-10, se manca 5] [forza]" (forza =
// PMREQ col flag force, 0x06). Ogni ciclo: PMREQ, frasi che si fermano, 5 s di sonno, sveglia misurata. Una
// riga per ciclo e un riassunto; alla fine il GPS torna come lo vuole il telefono.
__attribute__((noinline)) void comandoGpsProva(char* line) {
    int cicli = 5;
    bool forza = false, ok = true;
    for (char* p = line + 10; *p && ok;) {
        while (*p == ' ') p++;
        if (!*p) break;
        char* fine = p;
        while (*fine && *fine != ' ') fine++;
        const char tieni = *fine;
        *fine = 0;
        if (!std::strcmp(p, "forza")) {
            forza = true;
        } else {
            char* resto = nullptr;
            const long c = std::strtol(p, &resto, 10);
            ok = resto != p && !*resto && c >= 1 && c <= scheda::Gnss::PROVA_MAX;
            if (ok) cicli = int(c);
        }
        *fine = tieni;
        p = fine;
    }
    if (!ok) {
        rispondi("gps prova: @GPS!PROVA [cicli da 1 a 10] [forza]");
        return;
    }
    if (scheda::gnss().provaStato().inCorso) {
        rispondi("gps prova gia' in corso");
        return;
    }
    scheda::gnss().prova(cicli, forza);
    // Il PMREQ ha sempre backup + force (0x06, gnss_pilota.h), anche senza "forza".
    rispondi("gps prova: %d cicli, PMREQ flag 0x06 (circa %d s)", cicli, cicli * 10);
    scheda::Gnss::Prova pr = scheda::gnss().provaStato();
    int stampati = 0;
    for (int attesa = 0; attesa < 3000; attesa++) {  // al massimo 5 minuti (10 cicli: un paio)
        vTaskDelay(pdMS_TO_TICKS(100));
        pr = scheda::gnss().provaStato();
        for (; stampati < pr.fatti && stampati < scheda::Gnss::PROVA_MAX; stampati++) {
            const scheda::Gnss::CicloProva& c = pr.ciclo[stampati];
            if (c.esito == 0)
                rispondi("gps prova %d/%d: PMREQ ignorato (frasi ancora dopo 3 s)", stampati + 1, cicli);
            else if (c.sveglioMs >= 0)
                rispondi("gps prova %d/%d: standby, frasi ferme in %d ms, %u frasi nel sonno, sveglio in %d ms "
                         "(%u 0xFF)",
                         stampati + 1, cicli, int(c.fermeMs), unsigned(c.frasiNelSonno), int(c.sveglioMs),
                         unsigned(c.tentativi));
            else
                rispondi("gps prova %d/%d: standby, frasi ferme in %d ms, %u frasi nel sonno, non si sveglia "
                         "(%u 0xFF)",
                         stampati + 1, cicli, int(c.fermeMs), unsigned(c.frasiNelSonno), unsigned(c.tentativi));
        }
        if (!pr.inCorso) break;
    }
    int standby = 0, ignorati = 0, sveglie = 0, somma = 0, massimo = 0;
    for (int i = 0; i < pr.fatti && i < scheda::Gnss::PROVA_MAX; i++) {
        const scheda::Gnss::CicloProva& c = pr.ciclo[i];
        if (c.esito == 1) standby++;
        if (c.esito == 0) ignorati++;
        if (c.sveglioMs >= 0) {
            sveglie++;
            somma += c.sveglioMs;
            massimo = std::max<int>(massimo, c.sveglioMs);
        }
    }
    if (pr.inCorso) {
        rispondi("gps prova: non e' finita in 5 minuti (stato %s)", scheda::Gnss::nomeStato(scheda::gnss().stato()));
        return;
    }
    rispondi("gps prova fine: %u cicli su %d, standby %d, ignorato %d, sveglia media %d ms massima %d ms, "
             "flag 0x06%s%s",
             unsigned(pr.fatti), cicli, standby, ignorati, sveglie ? somma / sveglie : -1, sveglie ? massimo : -1,
             pr.errore ? ": " : "", pr.errore ? pr.errore : "");
}

// @GPS!FREDDO: GPS acceso per mezz'ora e fatto ripartire da freddo, come dopo l'interruttore. La ripartenza la
// fa il compito del GPS appena il ricevitore e' sveglio (il compito dei servizi lo accende entro un secondo).
__attribute__((noinline)) void comandoGpsFreddo() {
    rete().wantGnss(1800);
    const uint32_t prima = scheda::gnss().diagnosi().freddi;
    scheda::gnss().freddo();
    rispondi("gps: lo accendo, riparte da freddo appena e' sveglio");
    bool fatto = false;
    for (int i = 0; i < 200 && !fatto; i++) {  // fino a 20 s
        vTaskDelay(pdMS_TO_TICKS(100));
        fatto = scheda::gnss().diagnosi().freddi != prima;
    }
    if (fatto) rispondi("gps ripartito da freddo");
    else
        rispondi("gps non ancora ripartito da freddo (%s, stato %s): lo fa se si sveglia entro un minuto",
                 scheda::gnss().modulo(), scheda::Gnss::nomeStato(scheda::gnss().stato()));
}

void compito(void*) {
    crcInit();
    char line[160];
    uint32_t ricevuti = 0;
    uint8_t* buf = nullptr;
    // Fine di una copia: la mappa rilegge la microSD e l'interfaccia dice quanti tile sono arrivati.
    auto fineSessione = [&](const char* come) {
        ESP_LOGI(TAG, "mappe dal computer: %lu tile (%s)", (unsigned long)ricevuti, come);
        post({Ev::MAPPE, {}, 0, 1, int32_t(ricevuti)});
        ricevuti = 0;
    };
    for (;;) {
        // Con dei tile arrivati e niente @FINE (pagina chiusa, cavo staccato), dopo un minuto senza
        // niente la copia e' finita. Il minuto e' piu' lungo delle pause della pagina (30 s).
        if (!riga(line, sizeof(line), ricevuti ? pdMS_TO_TICKS(60000) : portMAX_DELAY)) {
            fineSessione("copia interrotta");
            continue;
        }
        if (!std::strcmp(line, "@PIPPA?")) {
            if (ricevuti) fineSessione("nuova copia senza @FINE della precedente");
            uint64_t tot = 0, libero = 0;
            const bool sd = sdPresente() && esp_vfs_fat_info("/sd", &tot, &libero) == ESP_OK;
            rispondi("ciao %s sd=%d libero=%lu", firmwareVersion(), sd ? 1 : 0, (unsigned long)(libero / (1024 * 1024)));
        } else if (!std::strncmp(line, "@HAI ", 5)) {
            char rel[48] = {};
            unsigned long size = 0;
            int z;
            long x, y;
            if (std::sscanf(line + 5, "%47s %lu", rel, &size) != 2 || !percorso(rel, z, x, y)) {
                rispondi("errore comando non valido");
                continue;
            }
            struct stat st;
            const std::string p = std::string(MAPPE) + "/" + rel;
            rispondi(stat(p.c_str(), &st) == 0 && (unsigned long)st.st_size == size ? "si" : "no");
        } else if (!std::strncmp(line, "@FILE ", 6)) {
            char rel[48] = {}, crcHex[16] = {};
            unsigned long size = 0;
            int z;
            long x, y;
            if (std::sscanf(line + 6, "%47s %lu %15s", rel, &size, crcHex) != 3 || !percorso(rel, z, x, y)) {
                rispondi("errore comando non valido");
                continue;
            }
            if (!size || size > MAX_FILE) {
                rispondi(size ? "errore file troppo grande" : "errore file vuoto");
                continue;
            }
            if (!sdPresente()) {
                rispondi("errore niente microSD");
                continue;
            }
            if (!buf) buf = static_cast<uint8_t*>(heap_caps_malloc(MAX_FILE, MALLOC_CAP_SPIRAM));
            if (!buf) {
                rispondi("errore memoria finita");
                continue;
            }
            rispondi("pronto");
            bool ok = true;
            for (size_t at = 0; at < size && ok;) {
                const size_t n = std::min<size_t>(PEZZO, size - at);
                ok = leggi(buf + at, n);
                at += n;
                if (ok && at < size) rispondi("+");
            }
            if (!ok) {
                rispondi("errore tempo scaduto");
                continue;
            }
            if (crc32(buf, size) != uint32_t(std::strtoul(crcHex, nullptr, 16))) {
                rispondi("errore CRC sbagliato");
                continue;
            }
            // Una seconda prova dopo un attimo (un intoppo sul bus SPI comune passa); se fallisce
            // ancora, la risposta dice quale passo e perche' (prima diceva solo "scrittura").
            char motivo[96];
            bool salvata = salvaTile(z, x, y, buf, size, motivo, sizeof(motivo));
            if (!salvata) {
                ESP_LOGW(TAG, "tile %s non salvata (%s): riprovo", rel, motivo);
                vTaskDelay(pdMS_TO_TICKS(200));
                salvata = salvaTile(z, x, y, buf, size, motivo, sizeof(motivo));
            }
            if (!salvata) {
                ESP_LOGE(TAG, "tile %s non salvata: %s", rel, motivo);
                rispondi("errore scrittura sulla microSD (%s)", motivo);
                continue;
            }
            if (!ricevuti) post({Ev::MAPPE, {}, 0, 0, 0});
            ricevuti++;
            rispondi("ok %s", rel);
        } else if (!std::strcmp(line, "@WIFI?")) {
            // Diagnosi dell'aggiornamento via Wi-Fi: accende la rete del telefono per 20 secondi (come
            // Impostazioni > Aggiorna il firmware), dice se e' partita, poi la spegne. Se l'ha gia'
            // accesa la schermata Aggiorna la si lascia stare.
            if (otaStatus().active) {
                rispondi("wifi gia' acceso %s (schermata Aggiorna), RAM interna libera %u",
                         otaStatus().ssid.c_str(), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
                continue;
            }
            const bool mia = otaStart(rete().number());
            const OtaStatus st = otaStatus();
            if (mia)
                rispondi("wifi acceso %s per 20 s, RAM interna libera %u", st.ssid.c_str(),
                         unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
            else rispondi("wifi errore %s", st.error.c_str());
            if (mia) {
                vTaskDelay(pdMS_TO_TICKS(20000));
                otaStop();  // se intanto si e' aperta la schermata Aggiorna, la rete resta sua
                rispondi(otaStatus().active ? "wifi resta acceso (schermata Aggiorna)" : "wifi spento");
            }
        } else if (!std::strcmp(line, "@OTA!")) {
            provaOta();
        } else if (!std::strcmp(line, "@SPEGNI!")) {
            // Prova: "Spegni" (sonno profondo) con un risveglio da solo dopo 10 secondi, come una
            // sveglia: dopo deve ritrovare l'ora.
            rispondi("spengo, mi risveglio tra 10 s");
            vTaskDelay(pdMS_TO_TICKS(200));
            scheda::wakeAfter(10);
            scheda::powerOff();
        } else if (!std::strcmp(line, "@GPS?")) {
            comandoGps();
        } else if (!std::strcmp(line, "@GPS!STORIA")) {
            comandoGpsStoria();
        } else if (!std::strcmp(line, "@GPS!VER")) {
            comandoGpsVer();
        } else if (!std::strncmp(line, "@GPS!PROVA", 10) && (line[10] == 0 || line[10] == ' ')) {
            comandoGpsProva(line);
        } else if (!std::strcmp(line, "@GPS!FREDDO")) {
            comandoGpsFreddo();
        } else if (!std::strcmp(line, "@NMEA!")) {
            scheda::gnss().eco(60);
            rispondi("frasi NMEA nel registro per 60 s");
        } else if (!std::strcmp(line, "@ORA?")) {
            // Prova dell'ora: quella del telefono (0 = non nota), da dove viene (0 nessuna, 1 GPS,
            // 2 rete, 3 a mano, 4 ritrovata nell'orologio del chip) e l'ultima salvata.
            rispondi("ora %lu fonte %d ultima %lu", (unsigned long)rete().unixTime(), int(rete().timeSource()),
                     (unsigned long)loadLastTime());
        } else if (!std::strncmp(line, "@ORA=", 5)) {
            // Prova: ora messa a mano, come Impostazioni > Data e ora.
            // Solo secondi Unix dal 2026 al 2079, come la schermata: "0", un testo o i millisecondi
            // davano un'ora del 1970 (o del 2106) che restava anche dopo un riavvio.
            char* fine = nullptr;
            errno = 0;
            const unsigned long v = std::strtoul(line + 5, &fine, 10);
            if (fine == line + 5 || *fine || errno == ERANGE || v < 1767225600UL || v > 3471292799UL) {
                rispondi("ora non valida: secondi Unix dal 2026 al 2079");
                continue;
            }
            const uint32_t t = uint32_t(v);
            if (rete().setManualTime(t)) rispondi("ora messa %lu", (unsigned long)t);
            else rispondi("ora non messa: arriva dal GPS");
        } else if (!std::strcmp(line, "@RIAVVIA!")) {
            // Prova: riavvio software, come dopo un aggiornamento.
            rispondi("riavvio");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        } else if (!std::strcmp(line, "@RETI?")) {
            // Diagnosi: le reti Wi-Fi che il telefono sente intorno (per capire se un computer che
            // non vede la rete dell'aggiornamento sente i 2,4 GHz).
            std::vector<std::string> reti;
            std::string nota;
            const bool ok = otaScan(reti, nota);
            for (const auto& r : reti) rispondi("rete %s", r.c_str());
            if (ok) rispondi("reti %u, %s", unsigned(reti.size()), nota.c_str());
            else rispondi("reti errore %s", nota.c_str());
        } else if (!std::strcmp(line, "@CUFFIE?")) {
            // Diagnosi delle cuffie USB-C: la sessione in corso o l'ultima, in una riga (piu' lunga di quelle di
            // rispondi(), che ne taglia 160 caratteri).
            const Cuffie::Statistiche s = cuffie().statistiche();
            char b[400];
            snprintf(b, sizeof(b),
                     "@PIPPA cuffie %s errore %s sessioni %lu nome \"%s\" id %04x:%04x uac %u formato %lu Hz %u "
                     "canali %u bit suonate %lu s sottoflussi %lu saltati %lu errori %lu volume %u ram interna "
                     "prima %lu minima %lu dopo %lu\n",
                     Cuffie::testoStato(s.stato), Cuffie::testoErrore(s.errore), (unsigned long)s.sessioni, s.nome,
                     unsigned(s.vid), unsigned(s.pid), unsigned(s.uac), (unsigned long)s.hz, unsigned(s.canali),
                     unsigned(s.bit), (unsigned long)s.secondiSuonati, (unsigned long)s.sottoflussi,
                     (unsigned long)s.saltati, (unsigned long)s.errori, unsigned(cuffie().volume()),
                     (unsigned long)s.ramInternaPrima, (unsigned long)s.ramInternaMin, (unsigned long)s.ramInternaDopo);
            fputs(b, stdout);
            fflush(stdout);
        } else if (!std::strncmp(line, "@CUFFIE! ", 9)) {
            // Prova da banco: cuffie USB-C accese per <secondi> (5-600) anche col computer attaccato (il comando
            // arriva proprio da li'); poi la porta torna al computer da sola. Prima la risposta, poi la porta
            // cambia (le cuffie aspettano 100 ms): fino al ritorno questa console non c'e'.
            char* fine = nullptr;
            errno = 0;
            const unsigned long sec = std::strtoul(line + 9, &fine, 10);
            if (fine == line + 9 || *fine || errno == ERANGE || sec < 5 || sec > 600) {
                rispondi("cuffie errore secondi da 5 a 600");
                continue;
            }
            rispondi("cuffie accese per %lu s: poi la console torna", sec);
            if (!cuffie().prova(uint32_t(sec))) rispondi("cuffie errore %s", Cuffie::testoErrore(cuffie().errore()));
        } else if (!std::strcmp(line, "@FINE")) {
            rispondi("fine %lu", (unsigned long)ricevuti);
            fineSessione("@FINE");
        }
        // Altre righe: il telefono non ha altri comandi sulla USB.
    }
}
}  // namespace

void usbMappeAvvia() {
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = PEZZO * 2;
    cfg.tx_buffer_size = 1024;
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "driver della USB non installato: niente mappe dal computer");
        return;
    }
    // Il log passa dal driver: senza un computer che legge si perde (dopo 50 ms), non blocca.
    usb_serial_jtag_vfs_use_driver();
    xTaskCreatePinnedToCore(compito, "usb", 6144, nullptr, 4, nullptr, 0);
}

}  // namespace app
#endif  // CONFIG_SCHEDA_TDECK_PLUS
