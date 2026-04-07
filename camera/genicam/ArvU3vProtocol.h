#pragma once
// ============================================================
// ArvU3vProtocol.h
// Exact protocol constants and structures extracted from Aravis source:
//   aravis/src/arvuvcpprivate.h  - GenCP cmd/ack + ABRM/SBRM/SIRM offsets
//   aravis/src/arvuvspprivate.h  - Leader/Trailer packet structures
//   aravis/src/arvuvinterfaceprivate.h - USB interface class constants
// ============================================================

#include <cstdint>

// ---- USB3 Vision device/interface class constants ---------------
// aravis/src/arvuvinterfaceprivate.h
static const uint8_t ARV_UV_DEVICE_CLASS      = 0xEF; // Misc
static const uint8_t ARV_UV_DEVICE_SUBCLASS   = 0x02;
static const uint8_t ARV_UV_DEVICE_PROTOCOL   = 0x01;
static const uint8_t ARV_UV_IFACE_CLASS       = 0xEF;
static const uint8_t ARV_UV_IFACE_SUBCLASS    = 0x05;
static const uint8_t ARV_UV_IFACE_CONTROL_PROTO = 0x00; // GenCP control
static const uint8_t ARV_UV_IFACE_DATA_PROTO    = 0x02; // streaming

// ---- GenCP packet magic -----------------------------------------
// aravis/src/arvuvcpprivate.h
static const uint32_t ARV_UVCP_MAGIC = 0x43563355; // "U3VC"

// ---- GenCP flags / commands / status ----------------------------
static const uint16_t ARV_UVCP_FLAG_REQUEST_ACK  = (1 << 14);

static const uint16_t ARV_UVCP_CMD_READ_MEMORY    = 0x0800;
static const uint16_t ARV_UVCP_ACK_READ_MEMORY    = 0x0801;
static const uint16_t ARV_UVCP_CMD_WRITE_MEMORY   = 0x0802;
static const uint16_t ARV_UVCP_ACK_WRITE_MEMORY   = 0x0803;
static const uint16_t ARV_UVCP_ACK_PENDING        = 0x0805;

static const uint16_t ARV_UVCP_STATUS_SUCCESS     = 0x0000;

// ---- GenCP packet structures ------------------------------------
// aravis/src/arvuvcpprivate.h  (#pragma pack(push,1))
#pragma pack(push,1)
struct ArvUvcpHeader {
    uint32_t magic;
    union { uint16_t status; uint16_t flags; };
    uint16_t command;
    uint16_t size;      // payload length (NOT including header)
    uint16_t id;        // request_id
};

struct ArvUvcpReadMemCmd {
    ArvUvcpHeader header;
    uint64_t address;
    uint16_t unknown;   // always 0
    uint16_t size;      // bytes to read
};

struct ArvUvcpWriteMemCmd {
    ArvUvcpHeader header;
    uint64_t address;
    // followed by data bytes
};

struct ArvUvcpWriteMemAck {
    ArvUvcpHeader header;
    uint16_t unknown;
    uint16_t bytes_written;
};

struct ArvUvcpPendingAck {
    ArvUvcpHeader header;
    uint16_t unknown;
    uint16_t timeout; // ms to wait before retrying
};
#pragma pack(pop)

// ---- ABRM register offsets (aravis/src/arvuvcpprivate.h) --------
static const uint64_t ARV_ABRM_MANUFACTURER_NAME    = 0x0004; // char[64]
static const uint64_t ARV_ABRM_MODEL_NAME           = 0x0044; // char[64]
static const uint64_t ARV_ABRM_SERIAL_NUMBER        = 0x0144; // char[64]
static const uint64_t ARV_ABRM_MANIFEST_TABLE_ADDR  = 0x01D0; // uint64
static const uint64_t ARV_ABRM_SBRM_ADDRESS         = 0x01D8; // uint64

// ---- SBRM register offsets (aravis/src/arvuvcpprivate.h) --------
static const uint64_t ARV_SBRM_MAX_CMD_TRANSFER     = 0x0014; // uint32
static const uint64_t ARV_SBRM_MAX_ACK_TRANSFER     = 0x0018; // uint32
static const uint64_t ARV_SBRM_SIRM_ADDRESS         = 0x0020; // uint64

// ---- SIRM register offsets (aravis/src/arvuvcpprivate.h) --------
static const uint64_t ARV_SIRM_INFO                 = 0x0000; // uint32
static const uint64_t ARV_SIRM_CONTROL              = 0x0004; // uint32
static const uint64_t ARV_SIRM_REQ_PAYLOAD_SIZE     = 0x0008; // uint64
static const uint64_t ARV_SIRM_REQ_LEADER_SIZE      = 0x0010; // uint32
static const uint64_t ARV_SIRM_REQ_TRAILER_SIZE     = 0x0014; // uint32
static const uint64_t ARV_SIRM_MAX_LEADER_SIZE      = 0x0018; // uint32
static const uint64_t ARV_SIRM_PAYLOAD_SIZE         = 0x001C; // uint32 (we write)
static const uint64_t ARV_SIRM_PAYLOAD_COUNT        = 0x0020; // uint32 (we write)
static const uint64_t ARV_SIRM_TRANSFER1_SIZE       = 0x0024; // uint32 (we write)
static const uint64_t ARV_SIRM_TRANSFER2_SIZE       = 0x0028; // uint32 (we write)
static const uint64_t ARV_SIRM_MAX_TRAILER_SIZE     = 0x002C; // uint32

static const uint32_t ARV_SIRM_INFO_ALIGNMENT_MASK  = 0xFF000000;
static const uint32_t ARV_SIRM_INFO_ALIGNMENT_SHIFT = 0x0018;
static const uint32_t ARV_SIRM_CONTROL_STREAM_ENABLE = 0x00000001;

// ---- Manifest entry (aravis/src/arvuvcpprivate.h) ---------------
// schema field bits [14:10] = type: 0=raw, 1=zip
#pragma pack(push,1)
struct ArvUvcpManifestEntry {
    uint16_t file_version_subminor;
    uint8_t  file_version_minor;
    uint8_t  file_version_major;
    uint32_t schema;         // bits[14:10] = ARV_UVCP_SCHEMA_ZIP(1) or RAW(0)
    uint64_t address;
    uint64_t size;
    uint64_t reserved[5];
};
#pragma pack(pop)

static const uint32_t ARV_UVCP_SCHEMA_TYPE_MASK  = 0x00007C00; // bits[14:10]
static const uint32_t ARV_UVCP_SCHEMA_TYPE_SHIFT = 10;
static const uint32_t ARV_UVCP_SCHEMA_ZIP        = 1;

// ---- Stream packet header magic (aravis/src/arvuvspprivate.h) ---
static const uint32_t ARV_UVSP_LEADER_MAGIC  = 0x4C563355; // "U3VL"
static const uint32_t ARV_UVSP_TRAILER_MAGIC = 0x54563355; // "U3VT"

// ---- Stream packet structures (aravis/src/arvuvspprivate.h) -----
static const uint16_t ARV_UVSP_PAYLOAD_TYPE_IMAGE = 0x0001;

#pragma pack(push,1)
struct ArvUvspHeader {
    uint32_t magic;
    uint16_t unknown0;
    uint16_t size;      // size of header (leader: sizeof(ArvUvspLeader))
    uint64_t frame_id;
};

struct ArvUvspLeaderInfos {
    uint16_t unknown0;
    uint16_t payload_type;  // ARV_UVSP_PAYLOAD_TYPE_IMAGE = 0x0001
    uint64_t timestamp;
    uint32_t pixel_format;  // PFNC
    uint32_t width;
    uint32_t height;
    uint32_t x_offset;
    uint32_t y_offset;
    uint16_t x_padding;
    uint16_t unknown1;
};

struct ArvUvspLeader {
    ArvUvspHeader     header;
    ArvUvspLeaderInfos infos;
};

struct ArvUvspTrailerInfos {
    uint32_t unknown0;
    uint64_t payload_size;
};

struct ArvUvspTrailer {
    ArvUvspHeader      header;
    ArvUvspTrailerInfos infos;
};
#pragma pack(pop)

// ---- PFNC pixel format codes ------------------------------------
static const uint32_t PFNC_Mono8          = 0x01080001;
static const uint32_t PFNC_Mono10         = 0x01100003;
static const uint32_t PFNC_Mono12         = 0x01100005;
static const uint32_t PFNC_BayerRG8       = 0x01080009;
static const uint32_t PFNC_BayerGR8       = 0x01080008;
static const uint32_t PFNC_BayerGB8       = 0x0108000A;
static const uint32_t PFNC_BayerBG8       = 0x0108000B;
static const uint32_t PFNC_RGB8Packed     = 0x02180014;
static const uint32_t PFNC_BGR8Packed     = 0x02180015;
static const uint32_t PFNC_YUV422_8_UYVY  = 0x0210001F;
static const uint32_t PFNC_YUV422_8       = 0x02100032;
