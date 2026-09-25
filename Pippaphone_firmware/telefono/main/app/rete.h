// Servizio di rete del telefono: collega il nodo PippaNet, la radio, i dati salvati e
// l'interfaccia. Tutti i metodi si possono chiamare da qualsiasi compito.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "app/dati.h"
#include "pippa_radio/radio_link.h"
#include "pippanet/phone.h"
#include "scheda/gnss.h"

namespace app {

class Rete : public pn::PhoneListener {
public:
    bool begin();

    // ---- identita' ----
    bool hasNumber();
    uint16_t number();
    bool isAdmin();
    std::string myCode();     // 16 cifre della mia chiave
    std::string adminCode();  // 16 cifre della chiave dell'amministratore
    // Codice della chiave di un altro telefono ("" se non ancora ricevuta).
    std::string codeOf(uint16_t number);

    // ---- azioni ----
    uint32_t sendText(const ChatId& chat, const std::string& text);
    // Segna la chat come letta e manda le conferme di lettura.
    void markRead(const ChatId& chat);
    uint32_t requestPosition(uint16_t to);
    void answerPosition(uint16_t to, uint32_t requestId, bool accept);
    void startSos();
    void stopSos();
    bool sosActive();
    std::vector<uint16_t> sosContacts();
    std::set<uint16_t> sosAcked();
    pn::GroupInfo createGroup(const std::string& name, const std::vector<uint16_t>& members);
    uint32_t sendVoice(uint16_t to, const std::vector<uint8_t>& codec2, uint16_t durationMs);

    // ---- numero e amministrazione ----
    void setProvisioning(bool on);
    bool offer(pn::Cert& cert, std::string& adminCode);
    void acceptOffer();
    void rejectOffer();
    bool createNetwork(uint16_t number);  // questo telefono diventa l'amministratore
    struct Hello {
        uint64_t shortId;
        std::string code;
        int16_t rssi;
        pn::ProvHello hello;
    };
    std::vector<Hello> hellos();
    bool assign(uint64_t shortId, uint16_t number);

    // ---- stato ----
    struct Signal {
        bool heard = false;
        uint16_t repeater = 0;
        int16_t rssi = 0;
        float snr = 0;
        uint32_t agoS = 0;
        uint8_t bars = 0;  // 0..4
    };
    Signal signal();
    struct RepeaterInfo {
        uint16_t addr;
        int16_t rssi;
        float snr;
        uint32_t agoS;
    };
    std::vector<RepeaterInfo> repeaters();
    uint32_t unixTime();
    bool timeValid();
    uint64_t airtimeMs();
    bool radioOk();
    // Potenza di trasmissione in dBm; 0 = automatica (in base al segnale del ripetitore).
    void setPower(int8_t dbm);
    int8_t power();
    // Chiede di tenere acceso il GPS per i prossimi secondi (posizione, SOS).
    void wantGnss(uint32_t seconds);
    bool gnssWanted();
    // Da chiamare ogni pochi secondi: potenza automatica.
    void service();
    // Aggiornato ogni pochi secondi dall'applicazione.
    void updateGnss(const scheda::Fix& fix, float accuracyM, uint8_t battery);
    pn::Position lastPosition();

    // ---- eventi dal nodo (PhoneListener) ----
    void onText(pn::Addr from, uint32_t msgId, const pn::TextPayload& t) override;
    void onGroupText(uint16_t group, pn::Addr from, uint32_t msgId, const pn::TextPayload& t) override;
    void onGlobalText(pn::Addr from, uint32_t msgId, const pn::TextPayload& t) override;
    void onDelivery(pn::Addr to, uint32_t msgId, pn::Delivery d) override;
    void onPositionRequest(pn::Addr from, uint32_t requestId) override;
    void onPosition(pn::Addr from, const pn::PosResponse& p) override;
    void onSos(pn::Addr from, const pn::SosPayload& s) override;
    void onSosAck(pn::Addr from) override;
    void onGroupInvite(const pn::GroupInfo& g, pn::Addr from) override;
    void onVoiceIncoming(pn::Addr from, const pn::VoiceReq& r) override;
    void onVoice(pn::Addr from, pn::Codec codec, const std::vector<uint8_t>& data, uint16_t durationMs,
                 uint32_t missing) override;
    void onVoiceState(pn::Addr to, uint32_t id, pn::VoiceSendState s) override;
    void onCertLearned(const pn::Cert& c) override;
    void onProvisionOffer(const pn::Cert& c, const pn::Key32& adminPublic) override;
    void onProvisionHello(uint64_t shortId, const pn::ProvHello& h, const pn::RxInfo& info) override;
    void onTimeSync(uint32_t unixSec) override;

private:
    void lock();
    void unlock();
    uint32_t now32();

    SemaphoreHandle_t lock_ = nullptr;
    pn::Phone* phone_ = nullptr;
    pippa::RadioLink radio_;
    bool haveOffer_ = false;
    pn::Cert offerCert_;
    pn::Key32 offerAdmin_{};
    std::vector<Hello> hellos_;
    std::set<uint16_t> sosAcked_;
    std::vector<uint16_t> sosContacts_;
    int8_t powerSetting_ = 0;
    uint32_t gnssUntilMs_ = 0;
    uint32_t lastFailMs_ = 0;
    // Vocale in invio: memorizzato per la chat quando parte.
    struct VoiceOut {
        uint32_t id = 0;
        uint16_t to = 0;
    } voiceOut_;
};

Rete& rete();

// Formatta una posizione ("45,8123 N · 9,0841 E").
std::string formatLatLon(int32_t latE7, int32_t lonE7);
// Distanza (m) e direzione (gradi da nord) da a verso b.
void distanceBearing(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2, double& meters, double& bearing);

}  // namespace app
