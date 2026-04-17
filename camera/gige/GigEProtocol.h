#pragma once
// ============================================================
// GigEProtocol.h
// GigE Vision 프로토콜 상수 및 구조체
// 출처: Aravis arvgvcpprivate.h / arvgvspprivate.h
// ============================================================

#include <cstdint>

// ── GVCP 포트 ────────────────────────────────────────────────────────────────
static constexpr uint16_t GVCP_PORT          = 3956;
static constexpr uint32_t GVCP_MAX_DATA_SIZE = 512;
static constexpr uint32_t GVCP_TIMEOUT_MS    = 500;
static constexpr uint32_t GVCP_MAX_RETRIES   = 5;

// ── GVCP 패킷 타입 ───────────────────────────────────────────────────────────
static constexpr uint8_t GVCP_PACKET_TYPE_CMD = 0x42;  // command
static constexpr uint8_t GVCP_PACKET_TYPE_ACK = 0x00;  // acknowledge

// ── GVCP 명령 코드 ───────────────────────────────────────────────────────────
static constexpr uint16_t GVCP_CMD_DISCOVERY       = 0x0002;
static constexpr uint16_t GVCP_ACK_DISCOVERY       = 0x0003;
static constexpr uint16_t GVCP_CMD_READ_REGISTER   = 0x0080;
static constexpr uint16_t GVCP_ACK_READ_REGISTER   = 0x0081;
static constexpr uint16_t GVCP_CMD_WRITE_REGISTER  = 0x0082;
static constexpr uint16_t GVCP_ACK_WRITE_REGISTER  = 0x0083;
static constexpr uint16_t GVCP_CMD_READ_MEMORY     = 0x0084;
static constexpr uint16_t GVCP_ACK_READ_MEMORY     = 0x0085;
static constexpr uint16_t GVCP_CMD_WRITE_MEMORY    = 0x0086;
static constexpr uint16_t GVCP_ACK_WRITE_MEMORY    = 0x0087;
static constexpr uint16_t GVCP_ACK_PENDING         = 0x0089;

// ── GVCP 플래그 ──────────────────────────────────────────────────────────────
// Aravis arvgvcpprivate.h 기준 (GigE Vision 2.0 Spec)
static constexpr uint8_t GVCP_CMD_FLAG_NONE                       = 0x00;
static constexpr uint8_t GVCP_CMD_FLAG_ACK_REQUIRED               = 0x01; // bit0
static constexpr uint8_t GVCP_CMD_FLAG_EXTENDED_IDS               = 0x10; // bit4
static constexpr uint8_t GVCP_DISCOVERY_FLAG_ALLOW_BROADCAST_ACK  = 0x10; // bit4

// ── GVCP 상태 코드 ───────────────────────────────────────────────────────────
static constexpr uint16_t GVCP_STATUS_SUCCESS = 0x0000;

// ── GVCP 패킷 헤더 (big-endian on wire) ──────────────────────────────────────
// ── GVCP 패킷 헤더 ───────────────────────────────────────────────────────────
// CMD 패킷 (host → camera):
//   byte[0]   = 0x42 (key, always)
//   byte[1]   = flags
//   byte[2-3] = command  (big-endian)
//   byte[4-5] = length   (payload length, big-endian)
//   byte[6-7] = req_id   (big-endian)
//
// ACK 패킷 (camera → host):
//   byte[0-1] = status   (big-endian, 0x0000=SUCCESS)
//   byte[2-3] = command  (big-endian)
//   byte[4-5] = length   (payload length, big-endian)
//   byte[6-7] = req_id   (big-endian)
//
// CMD와 ACK는 bytes[0-1]의 의미가 다르므로 별도 구조체 사용

#pragma pack(push, 1)
struct GvcpCmdHeader {
    uint8_t  key_code;      // 항상 0x42
    uint8_t  flags;
    uint16_t command;       // big-endian
    uint16_t length;        // payload length, big-endian
    uint16_t req_id;        // big-endian
};

struct GvcpAckHeader {
    uint16_t status;        // big-endian, 0x0000=SUCCESS
    uint16_t command;       // big-endian
    uint16_t length;        // payload length, big-endian
    uint16_t req_id;        // big-endian
};

// 하위 호환: GvcpHeader = CMD용
using GvcpHeader = GvcpCmdHeader;

struct GvcpDiscoveryCmd {
    GvcpHeader header;
    // no payload
};

// Discovery ACK: 固定 0xf8 bytes payload (bootstrap 일부)
static constexpr size_t GVBS_DISCOVERY_DATA_SIZE = 0xf8;

struct GvcpReadRegCmd {
    GvcpHeader header;
    uint32_t   address;     // big-endian
};

struct GvcpReadRegAck {
    GvcpAckHeader header;
    uint32_t   value;       // big-endian
};

struct GvcpWriteRegCmd {
    GvcpHeader header;
    uint32_t   address;     // big-endian
    uint32_t   value;       // big-endian
};

struct GvcpWriteRegAck {
    GvcpAckHeader header;
    uint16_t   reserved;
    uint16_t   data_index;  // big-endian
};

struct GvcpReadMemCmd {
    GvcpHeader header;
    uint32_t   address;     // big-endian
    uint16_t   reserved;
    uint16_t   count;       // bytes to read, big-endian
};

struct GvcpReadMemAck {
    GvcpAckHeader header;
    uint32_t   address;     // big-endian
    // followed by count bytes
};

struct GvcpWriteMemCmd {
    GvcpHeader header;
    uint32_t   address;     // big-endian
    // followed by data bytes
};

struct GvcpWriteMemAck {
    GvcpAckHeader header;
    uint16_t   reserved;
    uint16_t   bytes_written; // big-endian
};

struct GvcpPendingAck {
    GvcpAckHeader header;
    uint16_t   reserved;
    uint16_t   timeout_ms;  // big-endian
};
#pragma pack(pop)

// ── GigE Bootstrap 레지스터 (Aravis arvgvcpprivate.h) ─────────────────────────
static constexpr uint32_t GVBS_VERSION_OFFSET               = 0x00000000;
static constexpr uint32_t GVBS_DEVICE_MODE_OFFSET           = 0x00000004;
static constexpr uint32_t GVBS_DEVICE_MAC_HIGH_OFFSET       = 0x00000008;
static constexpr uint32_t GVBS_DEVICE_MAC_LOW_OFFSET        = 0x0000000C;
static constexpr uint32_t GVBS_CURRENT_IP_ADDRESS_OFFSET    = 0x00000024;
static constexpr uint32_t GVBS_CURRENT_SUBNET_MASK_OFFSET   = 0x00000034;
static constexpr uint32_t GVBS_CURRENT_GATEWAY_OFFSET       = 0x00000044;
static constexpr uint32_t GVBS_MANUFACTURER_NAME_OFFSET     = 0x00000048;
static constexpr uint32_t GVBS_MANUFACTURER_NAME_SIZE       = 32;
static constexpr uint32_t GVBS_MODEL_NAME_OFFSET            = 0x00000068;
static constexpr uint32_t GVBS_MODEL_NAME_SIZE              = 32;
static constexpr uint32_t GVBS_SERIAL_NUMBER_OFFSET         = 0x000000D8;
static constexpr uint32_t GVBS_SERIAL_NUMBER_SIZE           = 16;
static constexpr uint32_t GVBS_XML_URL_0_OFFSET             = 0x00000200;
static constexpr uint32_t GVBS_XML_URL_SIZE                 = 512;
static constexpr uint32_t GVBS_N_STREAM_CHANNELS_OFFSET     = 0x00000904;
static constexpr uint32_t GVBS_GVCP_CAPABILITY_OFFSET       = 0x00000934;
static constexpr uint32_t GVBS_HEARTBEAT_TIMEOUT_OFFSET     = 0x00000938;
static constexpr uint32_t GVBS_TIMESTAMP_FREQUENCY_OFFSET   = 0x00000940;
static constexpr uint32_t GVBS_CCP_OFFSET                   = 0x00000A00; // Control Channel Privilege
// GigE Vision 2.0 Spec + Aravis arvgvcpprivate.h:
//   bit0 = Exclusive access (한 호스트만, 다른 접속 차단)
//   bit1 = Control access   (제어 가능, 다른 호스트는 읽기만)
static constexpr uint32_t GVBS_CCP_EXCLUSIVE_ACCESS         = 0x00000001; // bit0
static constexpr uint32_t GVBS_CCP_CONTROL_ACCESS           = 0x00000002; // bit1

// Stream Channel 0 레지스터 (채널 N: base + N*0x40)
static constexpr uint32_t GVBS_SC0_PORT_OFFSET              = 0x00000D00;
static constexpr uint32_t GVBS_SC0_PACKET_SIZE_OFFSET       = 0x00000D04;
static constexpr uint32_t GVBS_SC0_PACKET_DELAY_OFFSET      = 0x00000D08;
static constexpr uint32_t GVBS_SC0_IP_ADDRESS_OFFSET        = 0x00000D18; // host IP (big-endian)

// GVCP Capability bits
static constexpr uint32_t GVBS_GVCP_CAP_PENDING_ACK         = 1u << 5;
static constexpr uint32_t GVBS_GVCP_CAP_HEARTBEAT_DISABLE   = 1u << 29;

// ── GVSP 패킷 구조 ───────────────────────────────────────────────────────────
// GVSP 헤더 (big-endian on wire)
#pragma pack(push, 1)
struct GvspHeader {
    uint16_t status;        // big-endian
    uint16_t block_id;      // frame ID (lower 16bit), big-endian
    uint32_t packet_infos;  // [31]=ext_id_mode [30:24]=content_type [23:0]=packet_id, big-endian
};
#pragma pack(pop)

// GVSP content type
static constexpr uint8_t GVSP_CONTENT_LEADER  = 0x01;
static constexpr uint8_t GVSP_CONTENT_TRAILER = 0x02;
static constexpr uint8_t GVSP_CONTENT_PAYLOAD = 0x03;

// GVSP status
static constexpr uint16_t GVSP_STATUS_SUCCESS = 0x0000;

// GVSP Leader payload (image)
#pragma pack(push, 1)
struct GvspImageLeader {
    uint16_t reserved;
    uint16_t payload_type;  // 0x0001 = image, big-endian
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint32_t pixel_format;  // PFNC, big-endian
    uint32_t width;         // big-endian
    uint32_t height;        // big-endian
    uint32_t x_offset;
    uint32_t y_offset;
    uint16_t x_padding;
    uint16_t y_padding;
};

struct GvspImageTrailer {
    uint16_t reserved;
    uint16_t payload_type;
    uint32_t size_y;        // image height, big-endian
};
#pragma pack(pop)

static constexpr uint16_t GVSP_PAYLOAD_TYPE_IMAGE = 0x0001;

// ── PFNC 픽셀 포맷 (U3V와 동일) ──────────────────────────────────────────────
// ArvU3vProtocol.h와 공유 사용
