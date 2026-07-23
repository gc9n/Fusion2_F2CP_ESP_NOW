#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <mbedtls/md.h>

namespace {

constexpr uint8_t MIN_CHANNEL = 1;
constexpr uint8_t MAX_CHANNEL = 13;
constexpr uint32_t CHANNEL_DWELL_MS = 650;
constexpr uint16_t MAX_PACKET = 250;
constexpr uint16_t MAX_PAYLOAD = 200;
constexpr uint8_t TAG_SIZE = 16;
constexpr uint32_t MAGIC = 0x50433246u;      // F2CP
constexpr uint32_t PAIR_MAGIC = 0x52503246u; // F2PR
constexpr uint8_t WIRE_VERSION = 1;
constexpr uint8_t BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr uint8_t BOOTSTRAP_KEY[32] = {
    0x33,0xD0,0x62,0xD3,0x97,0x99,0x9D,0x85,
    0xB2,0xDD,0xC9,0xBC,0xD6,0x7F,0x2E,0x2F,
    0x45,0x43,0x5C,0x48,0x42,0xCA,0x5B,0x95,
    0xB7,0xB0,0xC0,0xE8,0xBB,0xD4,0xEA,0x4A
};

enum : uint8_t { MSG_REQUEST = 1, MSG_RESPONSE = 2, MSG_EVENT = 3 };
enum : uint8_t { PAIR_ADV = 0, PAIR_REQUEST = 1, PAIR_RESPONSE = 2, PAIR_CONFIRM = 3, PAIR_COMPLETE = 4 };
enum : uint16_t {
    T_ACK = 0x00, T_GET = 0x01, T_GET_RESPONSE = 0x02, T_TUNE = 0x03,
    T_SCAN = 0x04, T_SCAN_RESPONSE = 0x06, T_STATUS = 0x08,
    T_STATUS_RESPONSE = 0x09, T_SCAN_STOP = 0x0c,
    T_AUTOLOCK = 0x10, T_AUTOLOCK_FOUND = 0x11, T_NEXT = 0x12,
    T_ACCEPT = 0x13, T_CANCEL = 0x14, T_FREQ_EVENT = 0x20,
    T_OPERATION_EVENT = 0x21, T_HELLO = 0xf0, T_HELLO_RESPONSE = 0xf1
};
constexpr uint8_t KEEP_BAND = 0xff;
constexpr uint8_t SCAN_BOTH = 0;
constexpr uint8_t SCAN_FINE_PEAK = 1;
constexpr uint8_t NEXT_UP = 1;
constexpr uint8_t NEXT_DOWN = 2;

struct __attribute__((packed)) Header {
    uint32_t magic;
    uint8_t version;
    uint8_t messageType;
    uint16_t type;
    uint16_t flags;
    uint32_t transactionId;
    uint32_t sequence;
    uint32_t sessionId;
    uint16_t payloadLength;
    uint16_t reserved;
};

struct __attribute__((packed)) PairPacket {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t size;
    uint8_t controllerMac[6];
    uint8_t fusionMac[6];
    uint8_t controllerNonce[16];
    uint8_t fusionNonce[16];
    uint32_t sessionId;
    uint8_t tag[TAG_SIZE];
};

struct __attribute__((packed)) Ack { uint8_t requestType, returnCode; };
struct __attribute__((packed)) FrequencyRssi { uint16_t mhz; uint8_t rssiA, rssiB; };
struct __attribute__((packed)) TuneRequest { uint16_t mhz; uint8_t bandHint, save; };
struct __attribute__((packed)) ScanRequest {
    uint16_t startMhz, stopMhz;
    uint8_t stepMhz, receiver, delayMs, options;
};
struct __attribute__((packed)) ScanResponse {
    uint8_t receiver, pointOffset, pointCount, completed;
    uint16_t peakMhz;
    uint8_t peakA, peakB;
};
struct __attribute__((packed)) StatusResponse {
    uint16_t mhz;
    uint8_t band, channel, rssiA, rssiB, coreOnline, operation, screen, paired;
};
struct __attribute__((packed)) HelloResponse {
    uint32_t sessionId;
    uint8_t major, minor;
    uint32_t capabilities;
    uint16_t mhz;
    uint8_t paired;
};
struct __attribute__((packed)) AutoFound {
    uint16_t mhz;
    uint8_t band, channel, rssiA, rssiB;
};
struct __attribute__((packed)) NextRequest { uint8_t direction; };
struct __attribute__((packed)) AcceptRequest { uint8_t save; };
struct __attribute__((packed)) FrequencyEvent {
    uint16_t mhz;
    uint8_t band, channel, source, rssiA, rssiB;
};
struct __attribute__((packed)) OperationEvent { uint8_t operation, active; };

static_assert(sizeof(Header) == 26, "F2CP header mismatch");
static_assert(sizeof(PairPacket) == 72, "F2CP pair packet mismatch");

struct RxPacket { uint8_t mac[6]; uint8_t channel; uint16_t length; uint8_t data[MAX_PACKET]; };
struct Pending {
    bool active;
    bool done;
    uint32_t txid;
    uint16_t expectedType;
    uint16_t receivedType;
    uint16_t length;
    uint8_t payload[MAX_PAYLOAD];
};

enum class PairState : uint8_t { Idle, Requesting, AwaitComplete, Confirmed };

Preferences prefs;
QueueHandle_t rxQueue = nullptr;
Pending pending{};
PairState pairState = PairState::Idle;

uint8_t localMac[6]{};
uint8_t fusionMac[6]{};
uint8_t fusionNonce[16]{};
uint8_t controllerNonce[16]{};
uint8_t pairKey[32]{};
uint8_t sessionKey[32]{};
uint8_t lmk[ESP_NOW_KEY_LEN]{};
uint32_t pairCode = 0;
uint32_t sessionId = 0;
uint32_t txSequence = 1;
uint32_t nextTransaction = 1;
uint32_t lastRxSequence = 0;
uint32_t pairStartedMs = 0;
uint32_t pairLastSendMs = 0;
uint32_t helloDueMs = 0;
uint32_t reconnectDueMs = 0;
uint32_t channelDueMs = 0;
uint8_t currentChannel = MIN_CHANNEL;
uint8_t savedChannel = MIN_CHANNEL;
uint8_t scanChannel = MIN_CHANNEL;
bool paired = false;
bool sessionReady = false;

uint16_t scanStart = 0;
uint8_t scanStep = 0;
char lineBuffer[96]{};
size_t lineLength = 0;

bool hmac(const uint8_t key[32], const void *data, size_t length, uint8_t out[32]) {
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return md && mbedtls_md_hmac(md, key, 32,
                                 static_cast<const unsigned char *>(data), length, out) == 0;
}

bool sameTag(const uint8_t *a, const uint8_t *b, size_t length) {
    uint8_t difference = 0;
    while (length--) difference |= *a++ ^ *b++;
    return difference == 0;
}

void derive(const uint8_t key[32], const char *label,
            const void *context, size_t contextLength, uint8_t out[32]) {
    uint8_t input[96]{};
    size_t labelLength = strlen(label);
    if (labelLength > 48) labelLength = 48;
    memcpy(input, label, labelLength);
    if (context && contextLength) {
        if (contextLength > sizeof(input) - labelLength)
            contextLength = sizeof(input) - labelLength;
        memcpy(input + labelLength, context, contextLength);
    }
    hmac(key, input, labelLength + contextLength, out);
}

void randomBytes(uint8_t *out, size_t length) {
    while (length) {
        const uint32_t value = esp_random();
        const size_t count = length < sizeof(value) ? length : sizeof(value);
        memcpy(out, &value, count);
        out += count;
        length -= count;
    }
}

void derivePairKey() {
    uint8_t context[44];
    memcpy(context, localMac, 6);
    memcpy(context + 6, fusionMac, 6);
    memcpy(context + 12, controllerNonce, 16);
    memcpy(context + 28, fusionNonce, 16);
    derive(BOOTSTRAP_KEY, "F2CP-PAIR-KEY", context, sizeof(context), pairKey);
}

void deriveLmk() {
    uint8_t context[12];
    if (memcmp(localMac, fusionMac, 6) <= 0) {
        memcpy(context, localMac, 6);
        memcpy(context + 6, fusionMac, 6);
    } else {
        memcpy(context, fusionMac, 6);
        memcpy(context + 6, localMac, 6);
    }
    uint8_t full[32];
    derive(pairKey, "F2CP-LMK", context, sizeof(context), full);
    memcpy(lmk, full, ESP_NOW_KEY_LEN);
}

void deriveSessionKey() {
    uint8_t context[16];
    memcpy(context, &sessionId, 4);
    memcpy(context + 4, fusionMac, 6);
    memcpy(context + 10, localMac, 6);
    derive(pairKey, "F2CP-SESSION", context, sizeof(context), sessionKey);
}

uint32_t calculatePairCode() {
    uint8_t context[22], full[32];
    memcpy(context, fusionMac, 6);
    memcpy(context + 6, fusionNonce, 16);
    derive(BOOTSTRAP_KEY, "F2CP-PAIR-CODE", context, sizeof(context), full);
    uint32_t value;
    memcpy(&value, full, sizeof(value));
    return value % 1000000u;
}

void signPair(PairPacket &packet, const uint8_t key[32]) {
    uint8_t full[32];
    memset(packet.tag, 0, sizeof(packet.tag));
    hmac(key, &packet, sizeof(packet), full);
    memcpy(packet.tag, full, TAG_SIZE);
}

bool validPair(const PairPacket &packet, const uint8_t key[32]) {
    PairPacket copy = packet;
    uint8_t full[32];
    memset(copy.tag, 0, sizeof(copy.tag));
    return hmac(key, &copy, sizeof(copy), full) && sameTag(full, packet.tag, TAG_SIZE);
}

bool validFrame(const uint8_t *data, size_t length, const uint8_t key[32]) {
    if (length < sizeof(Header) + TAG_SIZE) return false;
    Header header;
    memcpy(&header, data, sizeof(header));
    const size_t signedLength = sizeof(Header) + header.payloadLength;
    if (header.payloadLength > MAX_PAYLOAD || signedLength + TAG_SIZE != length) return false;
    uint8_t full[32];
    return hmac(key, data, signedLength, full) && sameTag(full, data + signedLength, TAG_SIZE);
}

void printMac(const uint8_t mac[6]) {
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool setRadioChannel(uint8_t channel) {
    if (channel < MIN_CHANNEL || channel > MAX_CHANNEL) return false;
    const esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) return false;
    currentChannel = channel;
    return true;
}

bool addPeer(const uint8_t mac[6], bool encrypted) {
    if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0; /* Follow the radio's current channel. */
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = encrypted;
    if (encrypted) memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
    const esp_err_t error = esp_now_add_peer(&peer);
    return error == ESP_OK || error == ESP_ERR_ESPNOW_EXIST;
}

void savePairing() {
    prefs.putBool("paired", true);
    prefs.putBytes("fusion", fusionMac, 6);
    prefs.putBytes("pairkey", pairKey, sizeof(pairKey));
    prefs.putUChar("channel", currentChannel);
    savedChannel = currentChannel;
}

void forgetPairing() {
    if (paired && esp_now_is_peer_exist(fusionMac)) esp_now_del_peer(fusionMac);
    paired = false;
    sessionReady = false;
    pairState = PairState::Idle;
    sessionId = 0;
    memset(fusionMac, 0, sizeof(fusionMac));
    memset(pairKey, 0, sizeof(pairKey));
    savedChannel = MIN_CHANNEL;
    scanChannel = MIN_CHANNEL;
    prefs.clear();
    Serial.println("Pairing erased. Waiting for a Fusion2 60-second pair window.");
}

void signFrame(uint8_t *frame, uint16_t payloadLength, const uint8_t key[32]) {
    uint8_t full[32];
    const size_t signedLength = sizeof(Header) + payloadLength;
    hmac(key, frame, signedLength, full);
    memcpy(frame + signedLength, full, TAG_SIZE);
}

size_t makeRequest(uint16_t type, const void *payload, uint16_t payloadLength,
                   bool hello, uint32_t txid, uint8_t frame[MAX_PACKET]) {
    if (payloadLength > MAX_PAYLOAD) return 0;
    memset(frame, 0, MAX_PACKET);
    Header header{};
    header.magic = MAGIC;
    header.version = WIRE_VERSION;
    header.messageType = MSG_REQUEST;
    header.type = type;
    header.transactionId = txid;
    header.sequence = ++txSequence;
    header.sessionId = hello ? 0u : sessionId;
    header.payloadLength = payloadLength;
    memcpy(frame, &header, sizeof(header));
    if (payloadLength && payload) memcpy(frame + sizeof(header), payload, payloadLength);
    signFrame(frame, payloadLength, hello ? pairKey : sessionKey);
    return sizeof(header) + payloadLength + TAG_SIZE;
}

bool sendPairRequest() {
    PairPacket packet{};
    packet.magic = PAIR_MAGIC;
    packet.version = WIRE_VERSION;
    packet.type = PAIR_REQUEST;
    packet.size = sizeof(packet);
    memcpy(packet.controllerMac, localMac, 6);
    memcpy(packet.fusionMac, fusionMac, 6);
    memcpy(packet.controllerNonce, controllerNonce, 16);
    memcpy(packet.fusionNonce, fusionNonce, 16);
    packet.sessionId = pairCode;
    signPair(packet, BOOTSTRAP_KEY);
    return esp_now_send(BROADCAST, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet)) == ESP_OK;
}

void sendPairConfirm(uint32_t fusionSession) {
    PairPacket packet{};
    packet.magic = PAIR_MAGIC;
    packet.version = WIRE_VERSION;
    packet.type = PAIR_CONFIRM;
    packet.size = sizeof(packet);
    memcpy(packet.controllerMac, localMac, 6);
    memcpy(packet.fusionMac, fusionMac, 6);
    memcpy(packet.controllerNonce, controllerNonce, 16);
    memcpy(packet.fusionNonce, fusionNonce, 16);
    packet.sessionId = fusionSession;
    signPair(packet, pairKey);
    for (uint8_t i = 0; i < 4; ++i) {
        esp_now_send(BROADCAST, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
        delay(25);
    }
}

bool sendCommand(uint16_t type, const void *payload, uint16_t payloadLength,
                 uint16_t expectedType, uint32_t timeoutMs = 1200, bool hello = false);
void processRx();

bool connectSession(bool verbose, uint32_t timeoutMs = 1200) {
    if (!paired) return false;
    lastRxSequence = 0;
    if (!sendCommand(T_HELLO, nullptr, 0, T_HELLO_RESPONSE, timeoutMs, true)) {
        if (verbose) Serial.println("Fusion2 did not answer HELLO.");
        sessionReady = false;
        return false;
    }
    if (pending.length < sizeof(HelloResponse)) return false;
    HelloResponse response;
    memcpy(&response, pending.payload, sizeof(response));
    sessionId = response.sessionId;
    deriveSessionKey();
    sessionReady = true;
    Serial.printf("Connected to Fusion2, session %08lX, frequency %u MHz.\n",
                  static_cast<unsigned long>(sessionId), response.mhz);
    return true;
}

bool sendCommand(uint16_t type, const void *payload, uint16_t payloadLength,
                 uint16_t expectedType, uint32_t timeoutMs, bool hello) {
    if (!paired || (!hello && !sessionReady)) {
        Serial.println("Not connected to Fusion2.");
        return false;
    }

    uint8_t frame[MAX_PACKET];
    const uint32_t txid = ++nextTransaction;
    const size_t length = makeRequest(type, payload, payloadLength, hello, txid, frame);
    if (!length) return false;

    pending = {};
    pending.active = true;
    pending.txid = txid;
    pending.expectedType = expectedType;

    const uint32_t started = millis();
    uint32_t nextSend = 0;
    while (millis() - started < timeoutMs && !pending.done) {
        if (static_cast<int32_t>(millis() - nextSend) >= 0) {
            esp_now_send(fusionMac, frame, length);
            nextSend = millis() + 180;
        }
        processRx();
        delay(2);
    }
    pending.active = false;
    if (!pending.done) {
        Serial.printf("No response to command 0x%02X.\n", type);
        return false;
    }
    return true;
}

void printResponse(const Header &header, const uint8_t *payload) {
    if (header.type == T_ACK && header.payloadLength >= sizeof(Ack)) {
        Ack ack;
        memcpy(&ack, payload, sizeof(ack));
        Serial.printf("ACK 0x%02X: %s (%u)\n", ack.requestType,
                      ack.returnCode == 0 ? "OK" : "ERROR", ack.returnCode);
    } else if (header.type == T_GET_RESPONSE && header.payloadLength >= sizeof(FrequencyRssi)) {
        FrequencyRssi value;
        memcpy(&value, payload, sizeof(value));
        Serial.printf("Frequency %u MHz, RSSI A=%u B=%u\n", value.mhz, value.rssiA, value.rssiB);
    } else if (header.type == T_STATUS_RESPONSE && header.payloadLength >= sizeof(StatusResponse)) {
        StatusResponse value;
        memcpy(&value, payload, sizeof(value));
        Serial.printf("Fusion2: %u MHz %c%u, A=%u B=%u, CORE=%s, operation=%u\n",
                      value.mhz, value.band ? value.band : '-', value.channel,
                      value.rssiA, value.rssiB, value.coreOnline ? "OK" : "OFFLINE",
                      value.operation);
    } else if (header.type == T_SCAN_RESPONSE && header.payloadLength >= sizeof(ScanResponse)) {
        ScanResponse value;
        memcpy(&value, payload, sizeof(value));
        const uint8_t bytesPerPoint = value.receiver == SCAN_BOTH ? 2 : 1;
        const uint8_t *rssi = payload + sizeof(value);
        Serial.printf("SCAN offset=%u count=%u", value.pointOffset, value.pointCount);
        for (uint8_t i = 0; i < value.pointCount; ++i) {
            const uint16_t mhz = scanStart + static_cast<uint16_t>(value.pointOffset + i) * scanStep;
            if (bytesPerPoint == 2)
                Serial.printf("  %u:%u/%u", mhz, rssi[i * 2], rssi[i * 2 + 1]);
            else
                Serial.printf("  %u:%u", mhz, rssi[i]);
        }
        Serial.println();
        if (value.completed)
            Serial.printf("SCAN COMPLETE: peak %u MHz, A=%u B=%u\n",
                          value.peakMhz, value.peakA, value.peakB);
    } else if (header.type == T_AUTOLOCK_FOUND && header.payloadLength >= sizeof(AutoFound)) {
        AutoFound value;
        memcpy(&value, payload, sizeof(value));
        Serial.printf("AUTOLOCK FOUND: %u MHz %c%u, A=%u B=%u\n",
                      value.mhz, value.band, value.channel, value.rssiA, value.rssiB);
    } else if (header.type == T_FREQ_EVENT && header.payloadLength >= sizeof(FrequencyEvent)) {
        FrequencyEvent value;
        memcpy(&value, payload, sizeof(value));
        Serial.printf("EVENT: %u MHz %c%u, A=%u B=%u\n",
                      value.mhz, value.band, value.channel, value.rssiA, value.rssiB);
    } else if (header.type == T_OPERATION_EVENT && header.payloadLength >= sizeof(OperationEvent)) {
        OperationEvent value;
        memcpy(&value, payload, sizeof(value));
        Serial.printf("OPERATION: %u %s\n", value.operation, value.active ? "START" : "STOP");
    }
}

void handlePair(const uint8_t source[6], const PairPacket &packet, uint8_t rxChannel) {
    if (packet.magic != PAIR_MAGIC || packet.version != WIRE_VERSION || packet.size != sizeof(packet)) return;

    if (packet.type == PAIR_ADV) {
        if (sessionReady || !validPair(packet, BOOTSTRAP_KEY) ||
            memcmp(source, packet.fusionMac, 6) != 0) return;

        if (rxChannel < MIN_CHANNEL || rxChannel > MAX_CHANNEL)
            rxChannel = currentChannel;

        if ((pairState == PairState::Requesting || pairState == PairState::AwaitComplete) &&
            (memcmp(source, fusionMac, 6) != 0 ||
             memcmp(packet.fusionNonce, fusionNonce, 16) != 0)) {
            return;
        }

        if (pairState == PairState::Idle) {
            (void)setRadioChannel(rxChannel);
            memcpy(fusionMac, packet.fusionMac, 6);
            memcpy(fusionNonce, packet.fusionNonce, 16);
            pairCode = packet.sessionId;
            if (calculatePairCode() != pairCode) return;

            randomBytes(controllerNonce, sizeof(controllerNonce));
            derivePairKey();
            deriveLmk();
            pairState = PairState::Requesting;
            pairStartedMs = millis();
            pairLastSendMs = 0;
            Serial.printf("PAIR WINDOW FOUND: code %06lu, Wi-Fi channel %u.\n",
                          static_cast<unsigned long>(pairCode), currentChannel);
        }
        return;
    }

    if (packet.type == PAIR_RESPONSE && pairState == PairState::Requesting &&
        validPair(packet, BOOTSTRAP_KEY) &&
        memcmp(source, fusionMac, 6) == 0 &&
        memcmp(packet.controllerMac, localMac, 6) == 0 &&
        memcmp(packet.fusionMac, fusionMac, 6) == 0 &&
        memcmp(packet.controllerNonce, controllerNonce, 16) == 0 &&
        memcmp(packet.fusionNonce, fusionNonce, 16) == 0) {

        sessionId = packet.sessionId;
        sendPairConfirm(sessionId);
        pairState = PairState::AwaitComplete;
        pairStartedMs = millis();
        pairLastSendMs = millis();
        Serial.println("Fusion2 accepted request. Waiting for PAIR COMPLETE...");
        return;
    }

    if (packet.type == PAIR_COMPLETE &&
        (pairState == PairState::AwaitComplete || pairState == PairState::Requesting) &&
        validPair(packet, pairKey) &&
        memcmp(source, fusionMac, 6) == 0 &&
        memcmp(packet.controllerMac, localMac, 6) == 0 &&
        memcmp(packet.fusionMac, fusionMac, 6) == 0 &&
        memcmp(packet.controllerNonce, controllerNonce, 16) == 0 &&
        memcmp(packet.fusionNonce, fusionNonce, 16) == 0 &&
        packet.sessionId == sessionId) {

        deriveLmk();
        if (!addPeer(fusionMac, true)) {
            Serial.println("PAIRING FAILED: could not add encrypted Fusion2 peer.");
            pairState = PairState::Idle;
            return;
        }
        deriveSessionKey();
        paired = true;
        sessionReady = false;
        pairState = PairState::Confirmed;
        savePairing();
        helloDueMs = millis() + 120;
        Serial.println("PAIR COMPLETE received. Unique peer key saved.");
    }
}

void handleFrame(const uint8_t source[6], const uint8_t *data, size_t length) {
    if (!paired || memcmp(source, fusionMac, 6) != 0 || length < sizeof(Header) + TAG_SIZE) return;
    Header header;
    memcpy(&header, data, sizeof(header));
    if (header.magic != MAGIC || header.version != WIRE_VERSION ||
        header.payloadLength > MAX_PAYLOAD || sizeof(Header) + header.payloadLength + TAG_SIZE != length)
        return;

    const bool hello = header.type == T_HELLO_RESPONSE && header.sessionId == 0;
    if ((!hello && header.sessionId != sessionId) || !validFrame(data, length, hello ? pairKey : sessionKey)) return;
    if (!hello && header.sequence <= lastRxSequence) return;
    lastRxSequence = header.sequence;

    const uint8_t *payload = data + sizeof(Header);
    printResponse(header, payload);
    if (pending.active && header.messageType == MSG_RESPONSE &&
        header.transactionId == pending.txid && header.type == pending.expectedType) {
        pending.receivedType = header.type;
        pending.length = header.payloadLength;
        if (pending.length) memcpy(pending.payload, payload, pending.length);
        pending.done = true;
    }
}

void processRx() {
    RxPacket packet;
    while (rxQueue && xQueueReceive(rxQueue, &packet, 0) == pdTRUE) {
        uint32_t magic = 0;
        if (packet.length >= 4) memcpy(&magic, packet.data, 4);
        if (magic == PAIR_MAGIC && packet.length == sizeof(PairPacket)) {
            PairPacket pair;
            memcpy(&pair, packet.data, sizeof(pair));
            handlePair(packet.mac, pair, packet.channel);
        } else if (magic == MAGIC) {
            handleFrame(packet.mac, packet.data, packet.length);
        }
    }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void receiveCallback(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
    if (!info || !info->src_addr || !data || !rxQueue || length <= 0 || length > MAX_PACKET) return;
    RxPacket packet{};
    memcpy(packet.mac, info->src_addr, 6);
    packet.channel = (info->rx_ctrl && info->rx_ctrl->channel) ?
                     info->rx_ctrl->channel : currentChannel;
    packet.length = static_cast<uint16_t>(length);
    memcpy(packet.data, data, packet.length);
    xQueueSend(rxQueue, &packet, 0);
}
#else
void receiveCallback(const uint8_t *mac, const uint8_t *data, int length) {
    if (!mac || !data || !rxQueue || length <= 0 || length > MAX_PACKET) return;
    RxPacket packet{};
    memcpy(packet.mac, mac, 6);
    packet.channel = currentChannel;
    packet.length = static_cast<uint16_t>(length);
    memcpy(packet.data, data, packet.length);
    xQueueSend(rxQueue, &packet, 0);
}
#endif

void help() {
    Serial.println("Commands:");
    Serial.println("  status");
    Serial.println("  get");
    Serial.println("  tune <MHz>");
    Serial.println("  scan <start> <stop> <step>");
    Serial.println("  scanstop");
    Serial.println("  autolock");
    Serial.println("  next [up|down]");
    Serial.println("  accept");
    Serial.println("  cancel");
    Serial.println("  forget");
    Serial.println("  help");
}

void runCommand(char *line) {
    char *context = nullptr;
    char *command = strtok_r(line, " \t", &context);
    if (!command) return;

    if (!strcasecmp(command, "help") || !strcmp(command, "?")) help();
    else if (!strcasecmp(command, "forget")) forgetPairing();
    else if (!strcasecmp(command, "status")) sendCommand(T_STATUS, nullptr, 0, T_STATUS_RESPONSE);
    else if (!strcasecmp(command, "get")) sendCommand(T_GET, nullptr, 0, T_GET_RESPONSE);
    else if (!strcasecmp(command, "tune")) {
        char *value = strtok_r(nullptr, " \t", &context);
        if (!value) { Serial.println("Usage: tune <MHz>"); return; }
        TuneRequest request{static_cast<uint16_t>(strtoul(value, nullptr, 10)), KEEP_BAND, 1};
        sendCommand(T_TUNE, &request, sizeof(request), T_ACK, 1800);
    } else if (!strcasecmp(command, "scan")) {
        char *start = strtok_r(nullptr, " \t", &context);
        char *stop = strtok_r(nullptr, " \t", &context);
        char *step = strtok_r(nullptr, " \t", &context);
        if (!start || !stop || !step) { Serial.println("Usage: scan <start> <stop> <step>"); return; }
        ScanRequest request{
            static_cast<uint16_t>(strtoul(start, nullptr, 10)),
            static_cast<uint16_t>(strtoul(stop, nullptr, 10)),
            static_cast<uint8_t>(strtoul(step, nullptr, 10)), SCAN_BOTH, 25, SCAN_FINE_PEAK
        };
        scanStart = request.startMhz;
        scanStep = request.stepMhz;
        sendCommand(T_SCAN, &request, sizeof(request), T_ACK);
    } else if (!strcasecmp(command, "scanstop")) sendCommand(T_SCAN_STOP, nullptr, 0, T_ACK);
    else if (!strcasecmp(command, "autolock")) sendCommand(T_AUTOLOCK, nullptr, 0, T_ACK);
    else if (!strcasecmp(command, "next")) {
        char *direction = strtok_r(nullptr, " \t", &context);
        NextRequest request{static_cast<uint8_t>(direction && !strcasecmp(direction, "down") ? NEXT_DOWN : NEXT_UP)};
        sendCommand(T_NEXT, &request, sizeof(request), T_ACK);
    } else if (!strcasecmp(command, "accept")) {
        AcceptRequest request{1};
        sendCommand(T_ACCEPT, &request, sizeof(request), T_ACK);
    } else if (!strcasecmp(command, "cancel")) sendCommand(T_CANCEL, nullptr, 0, T_ACK);
    else Serial.println("Unknown command. Type help.");
}

void serviceSerial() {
    while (Serial.available()) {
        const char character = static_cast<char>(Serial.read());
        if (character == '\r') continue;
        if (character == '\n') {
            lineBuffer[lineLength] = 0;
            if (lineLength) runCommand(lineBuffer);
            lineLength = 0;
            Serial.print("> ");
        } else if ((character == '\b' || character == 0x7f) && lineLength) {
            --lineLength;
        } else if (isprint(static_cast<unsigned char>(character)) && lineLength + 1 < sizeof(lineBuffer)) {
            lineBuffer[lineLength++] = character;
        }
    }
}

void loadPairing() {
    prefs.begin("f2cp", false);
    paired = prefs.getBool("paired", false) &&
             prefs.getBytesLength("fusion") == 6 &&
             prefs.getBytesLength("pairkey") == sizeof(pairKey);
    savedChannel = prefs.getUChar("channel", MIN_CHANNEL);
    if (savedChannel < MIN_CHANNEL || savedChannel > MAX_CHANNEL)
        savedChannel = MIN_CHANNEL;
    currentChannel = paired ? savedChannel : MIN_CHANNEL;
    scanChannel = currentChannel;
    if (paired) {
        prefs.getBytes("fusion", fusionMac, 6);
        prefs.getBytes("pairkey", pairKey, sizeof(pairKey));
    }
}

bool initializeRadio() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(60);
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (!setRadioChannel(currentChannel)) return false;
    esp_read_mac(localMac, ESP_MAC_WIFI_STA);
    if (esp_now_init() != ESP_OK) return false;
    uint8_t pmk[32];
    derive(BOOTSTRAP_KEY, "F2CP-PMK", nullptr, 0, pmk);
    if (esp_now_set_pmk(pmk) != ESP_OK) return false;
    if (esp_now_register_recv_cb(receiveCallback) != ESP_OK) return false;
    if (!addPeer(BROADCAST, false)) return false;
    if (paired) {
        deriveLmk();
        if (!addPeer(fusionMac, true)) paired = false;
    }
    return true;
}

} // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t waitStarted = millis();
    while (!Serial && millis() - waitStarted < 1800) delay(10);

    rxQueue = xQueueCreate(8, sizeof(RxPacket));
    loadPairing();
    if (!rxQueue || !initializeRadio()) {
        Serial.println("FATAL: ESP-NOW initialization failed.");
        return;
    }

    txSequence = esp_random();
    nextTransaction = esp_random();
    Serial.println("\nFusion2 F2CP ESP-NOW controller v1.4");
    Serial.print("Controller MAC: ");
    printMac(localMac);
    Serial.printf("\nPaired: %s, current Wi-Fi channel: %u\n", paired ? "YES" : "NO", currentChannel);
    if (!paired) Serial.println("Scanning Wi-Fi channels 1-13 for an open Fusion2 pairing window.");
    help();
    Serial.print("> ");
    reconnectDueMs = millis() + 300;
    channelDueMs = millis() + 50;
}

void loop() {
    processRx();
    serviceSerial();

    const uint32_t now = millis();

    /* Unpaired controllers listen on every allowed 2.4 GHz channel. Fusion2
     * advertises every 500 ms, so a 650 ms dwell guarantees that an open
     * 60-second window is found without requiring a fixed channel.
     */
    if (!paired && pairState == PairState::Idle &&
        static_cast<int32_t>(now - channelDueMs) >= 0) {
        (void)setRadioChannel(scanChannel);
        scanChannel = (scanChannel >= MAX_CHANNEL) ? MIN_CHANNEL : (uint8_t)(scanChannel + 1u);
        channelDueMs = now + CHANNEL_DWELL_MS;
    }

    if (pairState == PairState::Requesting) {
        if (now - pairStartedMs > 10000u) {
            pairState = PairState::Idle;
            channelDueMs = now + 20u;
            Serial.println("Pair request timed out; resuming channel scan.");
        } else if (pairLastSendMs == 0u || now - pairLastSendMs >= 350u) {
            sendPairRequest();
            pairLastSendMs = now;
        }
    } else if (pairState == PairState::AwaitComplete) {
        if (now - pairStartedMs > 8000u) {
            pairState = PairState::Idle;
            channelDueMs = now + 20u;
            Serial.println("PAIR COMPLETE not received; resuming channel scan.");
        } else if (now - pairLastSendMs >= 500u) {
            sendPairConfirm(sessionId);
            pairLastSendMs = now;
        }
    }

    if (pairState == PairState::Confirmed && helloDueMs &&
        static_cast<int32_t>(now - helloDueMs) >= 0) {
        helloDueMs = 0;
        if (connectSession(true, 900u)) {
            pairState = PairState::Idle;
            savePairing();
            Serial.printf("PAIRING COMPLETE on Wi-Fi channel %u.\n", currentChannel);
        } else {
            helloDueMs = now + 900u;
        }
    }

    /* Fusion2 may select a different clean channel after either device
     * reboots. A saved controller therefore probes all channels with HELLO
     * until the encrypted peer answers, then stores the working channel.
     */
    if (paired && !sessionReady && pairState == PairState::Idle &&
        static_cast<int32_t>(now - reconnectDueMs) >= 0) {
        (void)setRadioChannel(scanChannel);
        (void)addPeer(fusionMac, true);
        if (connectSession(false, 320u)) {
            savePairing();
            Serial.printf("Secure Fusion2 link restored on Wi-Fi channel %u.\n", currentChannel);
        } else {
            scanChannel = (scanChannel >= MAX_CHANNEL) ? MIN_CHANNEL : (uint8_t)(scanChannel + 1u);
        }
        reconnectDueMs = millis() + 180u;
    }

    delay(2);
}
