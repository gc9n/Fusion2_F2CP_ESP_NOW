#pragma once

#include <stdint.h>

#define F2CP_WIRE_VERSION                 1u
#define F2CP_ESPNOW_CHANNEL               1u
#define F2CP_FRAME_MAX                    250u
#define F2CP_MAX_PAYLOAD                  200u
#define F2CP_AUTH_TAG_SIZE                16u
#define F2CP_CONTROL_KEY_MIN              16u
#define F2CP_CONTROL_KEY_MAX              64u

#define F2CP_MAGIC                        0x50433246u
#define F2CP_PAIR_MAGIC                   0x52503246u

#define F2CP_TYPE_ACK                     0x00u
#define F2CP_TYPE_FREQUENCY_RSSI_REQUEST  0x01u
#define F2CP_TYPE_FREQUENCY_RSSI_RESPONSE 0x02u
#define F2CP_TYPE_SET_FREQUENCY           0x03u
#define F2CP_TYPE_RANGE_SCAN_REQUEST      0x04u
#define F2CP_TYPE_SCAN_RESPONSE           0x06u
#define F2CP_TYPE_SET_BAND_CHANNEL        0x07u
#define F2CP_TYPE_STATUS_REQUEST          0x08u
#define F2CP_TYPE_STATUS_RESPONSE         0x09u
#define F2CP_TYPE_SCAN_STOP               0x0Cu
#define F2CP_TYPE_AUTOLOCK_START          0x10u
#define F2CP_TYPE_AUTOLOCK_FOUND          0x11u
#define F2CP_TYPE_AUTOLOCK_NEXT           0x12u
#define F2CP_TYPE_AUTOLOCK_ACCEPT         0x13u
#define F2CP_TYPE_AUTOLOCK_CANCEL         0x14u
#define F2CP_TYPE_FREQUENCY_CHANGED       0x20u
#define F2CP_TYPE_OPERATION_CHANGED       0x21u
#define F2CP_TYPE_HELLO_REQUEST           0xF0u
#define F2CP_TYPE_HELLO_RESPONSE          0xF1u

#define F2CP_FLAG_MORE_DATA               (1u << 1)
#define F2CP_FLAG_SCAN_COMPLETE           (1u << 2)
#define F2CP_SCAN_OPTION_FINE_PEAK        (1u << 0)
#define F2CP_BAND_KEEP_CURRENT            0xFFu

#define F2CP_MESSAGE_REQUEST              1u
#define F2CP_MESSAGE_RESPONSE             2u
#define F2CP_MESSAGE_EVENT                3u

#define F2CP_PAIR_REQUEST                 1u
#define F2CP_PAIR_RESPONSE                2u
#define F2CP_PAIR_CONFIRM                 3u

#define F2CP_SCAN_RECEIVER_BOTH           0u
#define F2CP_AUTOLOCK_DIRECTION_UP        1u
#define F2CP_AUTOLOCK_DIRECTION_DOWN      2u

struct __attribute__((packed)) F2cpHeader {
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

struct __attribute__((packed)) F2cpPairPacket {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t size;
    uint8_t controllerMac[6];
    uint8_t fusionMac[6];
    uint8_t controllerNonce[16];
    uint8_t fusionNonce[16];
    uint32_t sessionId;
    uint8_t tag[F2CP_AUTH_TAG_SIZE];
};

struct __attribute__((packed)) F2cpAck {
    uint8_t requestType;
    uint8_t returnCode;
};

struct __attribute__((packed)) F2cpFrequencyRssi {
    uint16_t frequencyMhz;
    uint8_t rssiA;
    uint8_t rssiB;
};

struct __attribute__((packed)) F2cpSetFrequency {
    uint16_t frequencyMhz;
    uint8_t bandHint;
    uint8_t saveSelection;
};

struct __attribute__((packed)) F2cpRangeScan {
    uint16_t startMhz;
    uint16_t stopMhz;
    uint8_t stepMhz;
    uint8_t receiver;
    uint8_t delayMs;
    uint8_t options;
};

struct __attribute__((packed)) F2cpScanResponse {
    uint8_t receiver;
    uint8_t pointOffset;
    uint8_t pointCount;
    uint8_t completed;
    uint16_t peakFrequencyMhz;
    uint8_t peakRssiA;
    uint8_t peakRssiB;
};

struct __attribute__((packed)) F2cpSetBandChannel {
    uint8_t band;
    uint8_t channel;
    uint8_t saveSelection;
};

struct __attribute__((packed)) F2cpStatus {
    uint16_t frequencyMhz;
    uint8_t band;
    uint8_t channel;
    uint8_t rssiA;
    uint8_t rssiB;
    uint8_t coreOnline;
    uint8_t operation;
    uint8_t screen;
    uint8_t paired;
};

struct __attribute__((packed)) F2cpAutoLockFound {
    uint16_t frequencyMhz;
    uint8_t band;
    uint8_t channel;
    uint8_t rssiA;
    uint8_t rssiB;
};

struct __attribute__((packed)) F2cpAutoLockNext {
    uint8_t direction;
};

struct __attribute__((packed)) F2cpAutoLockAccept {
    uint8_t saveSelection;
};

struct __attribute__((packed)) F2cpFrequencyChanged {
    uint16_t frequencyMhz;
    uint8_t band;
    uint8_t channel;
    uint8_t source;
    uint8_t rssiA;
    uint8_t rssiB;
};

struct __attribute__((packed)) F2cpOperationChanged {
    uint8_t operation;
    uint8_t active;
};

struct __attribute__((packed)) F2cpHello {
    uint32_t sessionId;
    uint8_t protocolMajor;
    uint8_t protocolMinor;
    uint32_t capabilities;
    uint16_t frequencyMhz;
    uint8_t paired;
};

static_assert(sizeof(F2cpHeader) == 26, "F2CP header size mismatch");
static_assert(sizeof(F2cpPairPacket) == 72, "F2CP pairing packet size mismatch");
