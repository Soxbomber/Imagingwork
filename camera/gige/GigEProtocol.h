#pragma once
// ============================================================
// GigEProtocol.h — GigE Vision 2.2 protocol constants and structs
// Ref: GigE Vision Specification 2.2.00
// ============================================================

#include <cstdint>

// ── GVCP port / limits ────────────────────────────────────────────────────────
static constexpr uint16_t GVCP_PORT          = 3956;
static constexpr uint32_t GVCP_MAX_DATA_SIZE = 512;
static constexpr uint32_t GVCP_TIMEOUT_MS    = 500;
static constexpr uint32_t GVCP_MAX_RETRIES   = 5;

// ── Key code ──────────────────────────────────────────────────────────────────
static constexpr uint8_t GVCP_PACKET_TYPE_CMD = 0x42;
static constexpr uint8_t GVCP_PACKET_TYPE_ACK = 0x00;

// ── GVCP command codes (GV 2.2 §14.3) ────────────────────────────────────────
static constexpr uint16_t GVCP_CMD_DISCOVERY       = 0x0002;
static constexpr uint16_t GVCP_ACK_DISCOVERY       = 0x0003;
static constexpr uint16_t GVCP_CMD_FORCEIP         = 0x0004;  // GV 2.2
static constexpr uint16_t GVCP_ACK_FORCEIP         = 0x0005;  // GV 2.2
static constexpr uint16_t GVCP_CMD_PACKETRESEND    = 0x0040;  // no ACK
static constexpr uint16_t GVCP_CMD_READ_REGISTER   = 0x0080;
static constexpr uint16_t GVCP_ACK_READ_REGISTER   = 0x0081;
static constexpr uint16_t GVCP_CMD_WRITE_REGISTER  = 0x0082;
static constexpr uint16_t GVCP_ACK_WRITE_REGISTER  = 0x0083;
static constexpr uint16_t GVCP_CMD_READ_MEMORY     = 0x0084;
static constexpr uint16_t GVCP_ACK_READ_MEMORY     = 0x0085;
static constexpr uint16_t GVCP_CMD_WRITE_MEMORY    = 0x0086;
static constexpr uint16_t GVCP_ACK_WRITE_MEMORY    = 0x0087;
static constexpr uint16_t GVCP_ACK_PENDING         = 0x0089;
static constexpr uint16_t GVCP_CMD_READMEM_ASYNC   = 0x008A;  // GV 2.2
static constexpr uint16_t GVCP_ACK_READMEM_ASYNC   = 0x008B;  // GV 2.2
static constexpr uint16_t GVCP_CMD_WRITEMEM_ASYNC  = 0x008C;  // GV 2.2
static constexpr uint16_t GVCP_ACK_WRITEMEM_ASYNC  = 0x008D;  // GV 2.2
static constexpr uint16_t GVCP_CMD_EVENT           = 0x00C0;
static constexpr uint16_t GVCP_ACK_EVENT           = 0x00C1;
static constexpr uint16_t GVCP_CMD_EVENTDATA       = 0x00C2;
static constexpr uint16_t GVCP_ACK_EVENTDATA       = 0x00C3;
static constexpr uint16_t GVCP_CMD_ACTION          = 0x0100;
static constexpr uint16_t GVCP_ACK_ACTION          = 0x0101;

// ── GVCP command flags (GV 2.2 §14.3.1) ──────────────────────────────────────
static constexpr uint8_t GVCP_CMD_FLAG_NONE                      = 0x00;
static constexpr uint8_t GVCP_CMD_FLAG_ACK_REQUIRED              = 0x01;  // bit 0
static constexpr uint8_t GVCP_CMD_FLAG_ALLOW_BROADCAST_ACK       = 0x10;  // bit 4
static constexpr uint8_t GVCP_DISCOVERY_FLAG_ALLOW_BROADCAST_ACK = 0x10;  // alias
static constexpr uint8_t GVCP_CMD_FLAG_EXTENDED_IDS              = 0x10;  // bit 4, ACTION cmd

// ── GVCP status codes (GV 2.2 Table 14-1) ────────────────────────────────────
static constexpr uint16_t GVCP_STATUS_SUCCESS                          = 0x0000;
static constexpr uint16_t GVCP_STATUS_PACKET_RESEND                    = 0x0100;
static constexpr uint16_t GVCP_STATUS_NOT_IMPLEMENTED                  = 0x8001;
static constexpr uint16_t GVCP_STATUS_INVALID_PARAMETER                = 0x8002;
static constexpr uint16_t GVCP_STATUS_INVALID_ADDRESS                  = 0x8003;
static constexpr uint16_t GVCP_STATUS_WRITE_PROTECT                    = 0x8004;
static constexpr uint16_t GVCP_STATUS_BAD_ALIGNMENT                    = 0x8005;
static constexpr uint16_t GVCP_STATUS_ACCESS_DENIED                    = 0x8006;
static constexpr uint16_t GVCP_STATUS_BUSY                             = 0x8007;
static constexpr uint16_t GVCP_STATUS_LOCAL_PROBLEM                    = 0x8008;
static constexpr uint16_t GVCP_STATUS_MSG_MISMATCH                     = 0x8009;
static constexpr uint16_t GVCP_STATUS_INVALID_PROTOCOL                 = 0x800A;
static constexpr uint16_t GVCP_STATUS_NO_MSG                           = 0x800B;
static constexpr uint16_t GVCP_STATUS_PACKET_UNAVAILABLE               = 0x800C;
static constexpr uint16_t GVCP_STATUS_DATA_OVERRUN                     = 0x800D;
static constexpr uint16_t GVCP_STATUS_INVALID_HEADER                   = 0x800E;
static constexpr uint16_t GVCP_STATUS_WRONG_CONFIG                     = 0x800F;
static constexpr uint16_t GVCP_STATUS_PACKET_NOT_YET_AVAILABLE         = 0x8010;
static constexpr uint16_t GVCP_STATUS_PACKET_AND_PREV_REMOVED_FROM_MEM = 0x8011;
static constexpr uint16_t GVCP_STATUS_PACKET_REMOVED_FROM_MEM          = 0x8012;
static constexpr uint16_t GVCP_STATUS_NO_REF_TIME                      = 0x8013;
static constexpr uint16_t GVCP_STATUS_PACKET_TEMPORARILY_UNAVAILABLE   = 0x8014;
static constexpr uint16_t GVCP_STATUS_OVERFLOW                         = 0x8015;
static constexpr uint16_t GVCP_STATUS_ACTION_LATE                      = 0x8016;
static constexpr uint16_t GVCP_STATUS_ERROR                            = 0x8FFF;

// ── GVCP packet headers (big-endian on wire) ──────────────────────────────────
//
// CMD (host → camera):
//   byte[0]   = 0x42 (key)
//   byte[1]   = flags
//   byte[2-3] = command  (big-endian)
//   byte[4-5] = length   (payload length, big-endian)
//   byte[6-7] = req_id   (big-endian)
//
// ACK (camera → host):
//   byte[0-1] = status   (big-endian)
//   byte[2-3] = command  (big-endian)
//   byte[4-5] = length   (payload length, big-endian)
//   byte[6-7] = req_id   (big-endian)

#pragma pack(push, 1)
struct GvcpCmdHeader {
    uint8_t  key_code;   // always 0x42
    uint8_t  flags;
    uint16_t command;    // big-endian
    uint16_t length;     // payload length, big-endian
    uint16_t req_id;     // big-endian
};

struct GvcpAckHeader {
    uint16_t status;     // big-endian
    uint16_t command;    // big-endian
    uint16_t length;     // payload length, big-endian
    uint16_t req_id;     // big-endian
};
using GvcpHeader = GvcpCmdHeader;

// Discovery
struct GvcpDiscoveryCmd {
    GvcpHeader header;
    // no payload
};
static constexpr size_t GVBS_DISCOVERY_DATA_SIZE = 0xf8;

// ForceIP (GV 2.2 §14.3.2 — broadcast to reassign camera IP)
// Payload: reserved(2) + mac(6) + reserved(4) + ip(4) + reserved(4) + subnet(4) + reserved(4) + gw(4) = 32 bytes
struct GvcpForceIpCmd {
    GvcpHeader header;
    uint16_t   reserved0;
    uint8_t    mac[6];         // big-endian, 6 bytes
    uint32_t   reserved1;
    uint32_t   static_ip;      // big-endian
    uint32_t   reserved2;
    uint32_t   static_subnet;  // big-endian
    uint32_t   reserved3;
    uint32_t   static_gw;      // big-endian
};

// Read Register (supports multiple addresses: payload = N × 4-byte addresses)
struct GvcpReadRegCmd {
    GvcpHeader header;
    uint32_t   address;  // first address, big-endian; more addresses may follow
};
struct GvcpReadRegAck {
    GvcpAckHeader header;
    uint32_t   value;    // first value, big-endian; more values may follow
};

// Write Register (supports multiple pairs: payload = N × (addr4 + val4))
struct GvcpWriteRegCmd {
    GvcpHeader header;
    uint32_t   address;  // big-endian
    uint32_t   value;    // big-endian; more (address, value) pairs may follow
};
struct GvcpWriteRegAck {
    GvcpAckHeader header;
    uint16_t   reserved;
    uint16_t   data_index;  // 0-based index of last written register, big-endian
};

// Read Memory
struct GvcpReadMemCmd {
    GvcpHeader header;
    uint32_t   address;  // big-endian
    uint16_t   reserved;
    uint16_t   count;    // bytes to read, big-endian
};
struct GvcpReadMemAck {
    GvcpAckHeader header;
    uint32_t   address;  // big-endian
    // followed by count bytes of data
};

// Write Memory
struct GvcpWriteMemCmd {
    GvcpHeader header;
    uint32_t   address;  // big-endian
    // followed by data bytes
};
struct GvcpWriteMemAck {
    GvcpAckHeader header;
    uint16_t   reserved;
    uint16_t   bytes_written;  // big-endian
};

// Pending ACK
struct GvcpPendingAck {
    GvcpAckHeader header;
    uint16_t   reserved;
    uint16_t   timeout_ms;  // big-endian
};

// Packet Resend — standard 16-bit block ID (GV 2.2 §14.3.7)
// Payload: channel(2) + block_id(2) + first_packet_id(4) + last_packet_id(4) = 12 bytes
struct GvcpPacketResendCmd {
    GvcpHeader header;
    uint16_t   stream_channel;   // big-endian
    uint16_t   block_id;         // 16-bit block ID, big-endian
    uint32_t   first_packet_id;  // big-endian
    uint32_t   last_packet_id;   // big-endian
};

// Packet Resend — extended 64-bit block ID (GV 2.0+)
// Payload: channel(2) + reserved(2) + block_id_high(4) + block_id_low(4) + first(4) + last(4) = 20 bytes
struct GvcpPacketResendExtCmd {
    GvcpHeader header;
    uint16_t   stream_channel;   // big-endian
    uint16_t   reserved;
    uint32_t   block_id_high;    // upper 32 bits of 64-bit block ID, big-endian
    uint32_t   block_id_low;     // lower 32 bits of 64-bit block ID, big-endian
    uint32_t   first_packet_id;  // big-endian
    uint32_t   last_packet_id;   // big-endian
};

// Action Command (GV 2.2 §14.3.9)
// Payload: device_key(4) + group_key(4) + group_mask(4) = 12 bytes
struct GvcpActionCmd {
    GvcpHeader header;
    uint32_t   device_key;  // big-endian
    uint32_t   group_key;   // big-endian
    uint32_t   group_mask;  // big-endian
};
struct GvcpActionAck {
    GvcpAckHeader header;
    // no payload
};
#pragma pack(pop)

// ── Bootstrap registers (GV 2.2 Table 8-1) ───────────────────────────────────
static constexpr uint32_t GVBS_VERSION_OFFSET                      = 0x00000000;
static constexpr uint32_t GVBS_DEVICE_MODE_OFFSET                  = 0x00000004;
static constexpr uint32_t GVBS_DEVICE_MAC_HIGH_OFFSET              = 0x00000008;
static constexpr uint32_t GVBS_DEVICE_MAC_LOW_OFFSET               = 0x0000000C;
static constexpr uint32_t GVBS_NETWORK_INTERFACE_CAPABILITY_OFFSET = 0x00000010;
static constexpr uint32_t GVBS_NETWORK_INTERFACE_CONFIG_OFFSET     = 0x00000014;
static constexpr uint32_t GVBS_CURRENT_IP_ADDRESS_OFFSET           = 0x00000024;
static constexpr uint32_t GVBS_CURRENT_SUBNET_MASK_OFFSET          = 0x00000034;
static constexpr uint32_t GVBS_CURRENT_GATEWAY_OFFSET              = 0x00000044;
static constexpr uint32_t GVBS_MANUFACTURER_NAME_OFFSET            = 0x00000048;
static constexpr uint32_t GVBS_MANUFACTURER_NAME_SIZE              = 32;
static constexpr uint32_t GVBS_MODEL_NAME_OFFSET                   = 0x00000068;
static constexpr uint32_t GVBS_MODEL_NAME_SIZE                     = 32;
static constexpr uint32_t GVBS_DEVICE_VERSION_OFFSET               = 0x00000088;  // GV 2.2
static constexpr uint32_t GVBS_DEVICE_VERSION_SIZE                 = 32;
static constexpr uint32_t GVBS_MANUFACTURER_INFO_OFFSET            = 0x000000A8;  // GV 2.2
static constexpr uint32_t GVBS_MANUFACTURER_INFO_SIZE              = 48;
static constexpr uint32_t GVBS_SERIAL_NUMBER_OFFSET                = 0x000000D8;
static constexpr uint32_t GVBS_SERIAL_NUMBER_SIZE                  = 16;
static constexpr uint32_t GVBS_USER_DEFINED_NAME_OFFSET            = 0x000000E8;  // GV 2.2
static constexpr uint32_t GVBS_USER_DEFINED_NAME_SIZE              = 16;
static constexpr uint32_t GVBS_FIRST_URL_OFFSET                    = 0x00000200;
static constexpr uint32_t GVBS_SECOND_URL_OFFSET                   = 0x00000400;  // GV 2.2
static constexpr uint32_t GVBS_XML_URL_SIZE                        = 512;
static constexpr uint32_t GVBS_XML_URL_0_OFFSET                    = GVBS_FIRST_URL_OFFSET;  // alias
static constexpr uint32_t GVBS_N_NETWORK_INTERFACES_OFFSET         = 0x00000600;  // GV 2.2
static constexpr uint32_t GVBS_N_MESSAGE_CHANNELS_OFFSET           = 0x00000900;
static constexpr uint32_t GVBS_N_STREAM_CHANNELS_OFFSET            = 0x00000904;
static constexpr uint32_t GVBS_N_ACTION_SIGNALS_OFFSET             = 0x00000908;  // GV 2.2
static constexpr uint32_t GVBS_ACTION_DEVICE_KEY_OFFSET            = 0x0000090C;  // GV 2.2
static constexpr uint32_t GVBS_GVCP_CAPABILITY_OFFSET              = 0x00000934;
static constexpr uint32_t GVBS_HEARTBEAT_TIMEOUT_OFFSET            = 0x00000938;
static constexpr uint32_t GVBS_TIMESTAMP_FREQUENCY_OFFSET          = 0x00000940;
static constexpr uint32_t GVBS_TIMESTAMP_CONTROL_OFFSET            = 0x00000944;  // GV 2.2
static constexpr uint32_t GVBS_TIMESTAMP_VALUE_HIGH_OFFSET         = 0x00000948;  // GV 2.2
static constexpr uint32_t GVBS_TIMESTAMP_VALUE_LOW_OFFSET          = 0x0000094C;  // GV 2.2
static constexpr uint32_t GVBS_DISCOVERY_ACK_DELAY_OFFSET          = 0x00000950;  // GV 2.2
static constexpr uint32_t GVBS_GVCP_CONFIGURATION_OFFSET           = 0x00000954;  // GV 2.2
static constexpr uint32_t GVBS_PENDING_TIMEOUT_OFFSET              = 0x00000958;  // GV 2.2
static constexpr uint32_t GVBS_CCP_OFFSET                          = 0x00000A00;
static constexpr uint32_t GVBS_PRIMARY_APP_PORT_OFFSET             = 0x00000A04;  // GV 2.2
static constexpr uint32_t GVBS_PRIMARY_APP_IP_OFFSET               = 0x00000A14;  // GV 2.2

// CCP bits (Control Channel Privilege)
static constexpr uint32_t GVBS_CCP_EXCLUSIVE_ACCESS = 0x00000001;  // bit 0
static constexpr uint32_t GVBS_CCP_CONTROL_ACCESS   = 0x00000002;  // bit 1

// Stream Channel 0 registers (channel N: base + N × 0x40)
static constexpr uint32_t GVBS_SC0_PORT_OFFSET        = 0x00000D00;
static constexpr uint32_t GVBS_SC0_PACKET_SIZE_OFFSET = 0x00000D04;
static constexpr uint32_t GVBS_SC0_PACKET_DELAY_OFFSET= 0x00000D08;
static constexpr uint32_t GVBS_SC0_IP_ADDRESS_OFFSET  = 0x00000D18;
static constexpr uint32_t GVBS_SC0_CAPABILITY_OFFSET  = 0x00000D1C;  // GV 2.2
static constexpr uint32_t GVBS_SC0_CONFIGURATION_OFFSET = 0x00000D20;  // GV 2.2

// ── GVCP Capability register bits (GV 2.2 Table 8-33, register 0x0934) ────────
static constexpr uint32_t GVBS_GVCP_CAP_PENDING_ACK          = 1u << 5;
static constexpr uint32_t GVBS_GVCP_CAP_EVENTDATA            = 1u << 6;
static constexpr uint32_t GVBS_GVCP_CAP_EVENT                = 1u << 7;
static constexpr uint32_t GVBS_GVCP_CAP_PACKET_RESEND        = 1u << 8;
static constexpr uint32_t GVBS_GVCP_CAP_WRITEREG_MULTIPLE    = 1u << 9;
static constexpr uint32_t GVBS_GVCP_CAP_READREG_MULTIPLE     = 1u << 10;
static constexpr uint32_t GVBS_GVCP_CAP_DISCOVERY_ACK_DELAY  = 1u << 11;
static constexpr uint32_t GVBS_GVCP_CAP_WRITEMEM_ASYNC       = 1u << 12;
static constexpr uint32_t GVBS_GVCP_CAP_READMEM_ASYNC        = 1u << 13;
static constexpr uint32_t GVBS_GVCP_CAP_LINK_SPEED           = 1u << 14;
static constexpr uint32_t GVBS_GVCP_CAP_ACTION               = 1u << 26;
static constexpr uint32_t GVBS_GVCP_CAP_EXT_STATUS_CODES     = 1u << 27;
static constexpr uint32_t GVBS_GVCP_CAP_PRIMARY_APP          = 1u << 28;
static constexpr uint32_t GVBS_GVCP_CAP_HEARTBEAT_DISABLE    = 1u << 29;
static constexpr uint32_t GVBS_GVCP_CAP_SERIAL_NUMBER        = 1u << 30;
static constexpr uint32_t GVBS_GVCP_CAP_USER_DEFINED_NAME    = 1u << 31;

// ── GVCP Configuration register bits (GV 2.2 Table 8-34, register 0x0954) ────
static constexpr uint32_t GVBS_GVCP_CFG_HEARTBEAT_DISABLE    = 1u << 0;
static constexpr uint32_t GVBS_GVCP_CFG_EXTENDED_STATUS      = 1u << 1;  // GV 2.2
static constexpr uint32_t GVBS_GVCP_CFG_PENDING_ACK_ENABLE   = 1u << 6;

// ── Timestamp Control register values (register 0x0944) ───────────────────────
static constexpr uint32_t GVBS_TIMESTAMP_CONTROL_LATCH        = 0x00000001;
static constexpr uint32_t GVBS_TIMESTAMP_CONTROL_RESET        = 0x00000002;

// ── GVSP header (big-endian on wire) ─────────────────────────────────────────
//
// Standard (8 bytes, block_id 16-bit):
//   byte[0-1]: status
//   byte[2-3]: block_id  (lower 16 bits, or full 16-bit ID in non-ext mode)
//   byte[4-7]: packet_infos
//              [31]    = extended ID mode flag (1 = extended header follows)
//              [30:24] = content type
//              [23:0]  = packet ID (24-bit, non-ext mode only)
//
// Extended (20 bytes, block_id 64-bit, GV 2.0+, when bit 31 set):
//   byte[0-7]:   standard header (as above, packet_id field ignored)
//   byte[8-11]:  block_id_high32 (upper 32 bits of 64-bit block ID)
//   byte[12-15]: block_id_low32  (lower 32 bits of 64-bit block ID)
//   byte[16-19]: packet_id       (full 32-bit packet ID)

#pragma pack(push, 1)
struct GvspHeader {
    uint16_t status;        // big-endian
    uint16_t block_id;      // 16-bit block ID, big-endian
    uint32_t packet_infos;  // [31]=ext_id_mode, [30:24]=content_type, [23:0]=packet_id
};

struct GvspExtHeader {
    uint16_t status;           // big-endian
    uint16_t block_id_low16;   // lower 16 bits of 64-bit block ID (backward compat), big-endian
    uint32_t packet_infos;     // [31]=1, [30:24]=content_type, [23:0]=ignored
    uint32_t block_id_high32;  // upper 32 bits of block ID, big-endian
    uint32_t block_id_low32;   // lower 32 bits of block ID, big-endian
    uint32_t packet_id;        // full 32-bit packet ID, big-endian
};
#pragma pack(pop)

static constexpr int      GVSP_HEADER_SIZE     = 8;   // standard header bytes
static constexpr int      GVSP_EXT_HEADER_SIZE = 20;  // extended header bytes
static constexpr uint32_t GVSP_EXT_ID_FLAG        = 0x80000000u;
static constexpr uint32_t GVSP_CONTENT_TYPE_MASK  = 0x7F000000u;
static constexpr uint32_t GVSP_CONTENT_TYPE_SHIFT = 24u;
static constexpr uint32_t GVSP_PACKET_ID_MASK     = 0x00FFFFFFu;

// GVSP content types
static constexpr uint8_t GVSP_CONTENT_LEADER  = 0x01;
static constexpr uint8_t GVSP_CONTENT_TRAILER = 0x02;
static constexpr uint8_t GVSP_CONTENT_PAYLOAD = 0x03;
static constexpr uint8_t GVSP_CONTENT_ALL_IN  = 0x04;  // GV 2.2 all-in packet
static constexpr uint8_t GVSP_CONTENT_MULTIPART_LEADER  = 0x05;  // GV 2.1+
static constexpr uint8_t GVSP_CONTENT_MULTIPART_TRAILER = 0x06;  // GV 2.1+
static constexpr uint8_t GVSP_CONTENT_MULTIPART_PAYLOAD = 0x07;  // GV 2.1+

// GVSP status codes
static constexpr uint16_t GVSP_STATUS_SUCCESS       = 0x0000;
static constexpr uint16_t GVSP_STATUS_PACKET_RESEND = 0x0100;
static constexpr uint16_t GVSP_STATUS_NOT_READY     = 0x8005;
static constexpr uint16_t GVSP_STATUS_ERROR         = 0x8FFF;

// GVSP payload types (GV 2.2 Table 16-2)
static constexpr uint16_t GVSP_PAYLOAD_TYPE_RAW_DATA             = 0x0000;
static constexpr uint16_t GVSP_PAYLOAD_TYPE_IMAGE                = 0x0001;
static constexpr uint16_t GVSP_PAYLOAD_TYPE_JPEG                 = 0x0002;
static constexpr uint16_t GVSP_PAYLOAD_TYPE_JPEG2000             = 0x0003;
static constexpr uint16_t GVSP_PAYLOAD_TYPE_H264                 = 0x0005;
static constexpr uint16_t GVSP_PAYLOAD_TYPE_MULTIZONE_IMAGE      = 0x0006;
static constexpr uint16_t GVSP_PAYLOAD_TYPE_MULTIPART            = 0x0007;  // GV 2.1+
static constexpr uint16_t GVSP_PAYLOAD_TYPE_GENERIC_RAW_DATA     = 0x0008;  // GV 2.2
static constexpr uint16_t GVSP_PAYLOAD_TYPE_FILE                 = 0x0009;  // GV 2.2
static constexpr uint16_t GVSP_PAYLOAD_TYPE_JPEG_XS              = 0x000B;  // GV 2.2
static constexpr uint16_t GVSP_PAYLOAD_TYPE_CHUNK_DATA           = 0x4000;
static constexpr uint16_t GVSP_PAYLOAD_TYPE_IMAGE_EXTENDED_CHUNK = 0x4001;

// ── GVSP Image Data Leader (GV 2.2 Table 16-9) ───────────────────────────────
//   payload[0-1]   = reserved (0x0000)
//   payload[2-3]   = payload_type  (big-endian, 0x0001 = IMAGE)
//   payload[4-7]   = timestamp_high (big-endian)
//   payload[8-11]  = timestamp_low  (big-endian)
//   payload[12-15] = pixel_format  (PFNC, big-endian)
//   payload[16-19] = size_x / width (big-endian)
//   payload[20-23] = size_y / height (big-endian)
//   payload[24-27] = offset_x (big-endian)
//   payload[28-31] = offset_y (big-endian)
//   payload[32-33] = padding_x (big-endian)
//   payload[34-35] = padding_y (big-endian)
static constexpr int GVSP_LEADER_MIN_SIZE = 24;  // through height field

#pragma pack(push, 1)
struct GvspImageLeader {
    uint16_t reserved;
    uint16_t payload_type;   // big-endian, 0x0001 = image
    uint32_t timestamp_high; // big-endian
    uint32_t timestamp_low;  // big-endian
    uint32_t pixel_format;   // PFNC, big-endian
    uint32_t size_x;         // width, big-endian
    uint32_t size_y;         // height, big-endian
    uint32_t offset_x;       // big-endian
    uint32_t offset_y;       // big-endian
    uint16_t padding_x;      // big-endian
    uint16_t padding_y;      // big-endian
};

struct GvspImageTrailer {
    uint16_t reserved;
    uint16_t payload_type;  // big-endian
    uint32_t size_y;        // image height, big-endian
};
#pragma pack(pop)

// ── PFNC pixel format codes ───────────────────────────────────────────────────
// (shared with U3V — see ArvU3vProtocol.h)
