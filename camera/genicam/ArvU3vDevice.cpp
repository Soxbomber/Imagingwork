#include "ArvU3vDevice.h"
#include <QDebug>
#include <cstring>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#  include <windows.h>
#endif

// ============================================================
// Aravis arvuvdevice.c 완전 포팅
//
// 패킷 레이아웃 (Aravis arvuvcpprivate.h):
//
//  ReadMemoryCmd:
//   [Header 12B][address 8B][unknown 2B][read_size 2B]  총 24B
//   header.size = sizeof(Infos) = 12  (payload 크기, 읽을 바이트 수 X)
//   infos.size  = 읽을 바이트 수 (uint16_t)
//
//  ReadMemoryAck:
//   [Header 12B][data NB]
//   data = packet + sizeof(ArvUvcpHeader)  (Aravis: get_read_memory_ack_data)
//
//  WriteMemoryCmd:
//   [Header 12B][address 8B][data NB]  총 12+8+N
//   header.size = sizeof(Infos) + N = 8 + N
//
//  WriteMemoryAck:
//   [Header 12B][unknown 2B][bytes_written 2B]
//
//  ArvUvcpHeader union:
//   cmd 방향 : flags   필드 사용 (ARV_UVCP_FLAGS_REQUEST_ACK)
//   ack 방향 : status  필드 사용 (ARV_UVCP_STATUS_SUCCESS = 0)
// ============================================================

static inline uint32_t alignUp32(uint32_t v, uint32_t a)
{
    return (a <= 1) ? v : ((v + a - 1) / a * a);
}

// ---- enumerate --------------------------------------------------
QList<ArvU3vDeviceInfo> ArvU3vDevice::enumerate(libusb_context* ctx)
{
    QList<ArvU3vDeviceInfo> result;
    if (!ctx) return result;

    libusb_device** devs = nullptr;
    const ssize_t cnt = libusb_get_device_list(ctx, &devs);
    if (cnt < 0) {
        qWarning("ArvU3vDevice::enumerate: libusb_get_device_list failed");
        return result;
    }

    int found = 0;
    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device* dev = devs[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) != 0) continue;

        // USB3 Vision: bDeviceClass=0xEF, bDeviceSubClass=0x02, bDeviceProtocol=0x01
        if (desc.bDeviceClass    != ARV_UV_DEVICE_CLASS    ||
            desc.bDeviceSubClass != ARV_UV_DEVICE_SUBCLASS ||
            desc.bDeviceProtocol != ARV_UV_DEVICE_PROTOCOL)
            continue;

        // control(proto=0x00) + data(proto=0x02) 인터페이스 모두 있어야 USB3V
        libusb_config_descriptor* config = nullptr;
        if (libusb_get_config_descriptor(dev, 0, &config) != 0) continue;

        bool ctrlFound = false, dataFound = false;
        for (int j = 0; j < config->bNumInterfaces && !(ctrlFound && dataFound); ++j) {
            for (int k = 0; k < config->interface[j].num_altsetting; ++k) {
                const libusb_interface_descriptor& alt =
                    config->interface[j].altsetting[k];
                if (alt.bInterfaceClass    != ARV_UV_IFACE_CLASS ||
                    alt.bInterfaceSubClass != ARV_UV_IFACE_SUBCLASS) continue;
                if (alt.bInterfaceProtocol == ARV_UV_IFACE_CONTROL_PROTO) ctrlFound = true;
                if (alt.bInterfaceProtocol == ARV_UV_IFACE_DATA_PROTO)    dataFound = true;
            }
        }
        libusb_free_config_descriptor(config);
        if (!ctrlFound || !dataFound) continue;

        ArvU3vDeviceInfo info;
        info.busNumber     = libusb_get_bus_number(dev);
        info.deviceAddress = libusb_get_device_address(dev);

        libusb_device_handle* h = nullptr;
        if (libusb_open(dev, &h) == 0) {
            auto getString = [&](int idx, QString& out) {
                if (idx <= 0) return;
                unsigned char buf[256]{};
                if (libusb_get_string_descriptor_ascii(
                        h, static_cast<uint8_t>(idx), buf, sizeof(buf)) > 0)
                    out = QString::fromLatin1(
                        reinterpret_cast<const char*>(buf)).trimmed();
            };
            getString(desc.iManufacturer, info.manufacturer);
            getString(desc.iProduct,      info.modelName);
            getString(desc.iSerialNumber, info.serialNumber);
            libusb_close(h);
        }
        if (info.modelName.isEmpty())
            info.modelName = QString("U3VCamera_%1_%2")
                             .arg(info.busNumber).arg(info.deviceAddress);

        qDebug("ArvU3vDevice::enumerate: [%s] bus=%d addr=%d",
               qPrintable(info.modelName), info.busNumber, info.deviceAddress);
        result.append(info);
        ++found;
    }
    libusb_free_device_list(devs, 1);
    qDebug("ArvU3vDevice::enumerate: %d USB3Vision camera(s)", found);
    return result;
}

// ---- open -------------------------------------------------------
bool ArvU3vDevice::open(libusb_context* ctx,
                         uint8_t busNumber, uint8_t deviceAddress)
{
    libusb_device** devs = nullptr;
    const ssize_t cnt = libusb_get_device_list(ctx, &devs);
    if (cnt < 0) return false;

    libusb_device* target = nullptr;
    for (ssize_t i = 0; i < cnt; ++i) {
        if (libusb_get_bus_number(devs[i])     == busNumber &&
            libusb_get_device_address(devs[i]) == deviceAddress) {
            target = devs[i]; break;
        }
    }
    if (!target) {
        libusb_free_device_list(devs, 1);
        m_lastError = "device not found";
        return false;
    }

    // config descriptor 스캔으로 인터페이스/엔드포인트 자동 탐지
    libusb_config_descriptor* config = nullptr;
    if (libusb_get_config_descriptor(target, 0, &config) == 0) {
        for (int j = 0; j < config->bNumInterfaces; ++j) {
            for (int k = 0; k < config->interface[j].num_altsetting; ++k) {
                const libusb_interface_descriptor& alt =
                    config->interface[j].altsetting[k];
                if (alt.bInterfaceClass    != ARV_UV_IFACE_CLASS ||
                    alt.bInterfaceSubClass != ARV_UV_IFACE_SUBCLASS) continue;

                if (alt.bInterfaceProtocol == ARV_UV_IFACE_CONTROL_PROTO) {
                    m_controlInterface = alt.bInterfaceNumber;
                    for (int e = 0; e < alt.bNumEndpoints; ++e) {
                        const uint8_t addr = alt.endpoint[e].bEndpointAddress;
                        if (addr & LIBUSB_ENDPOINT_IN) m_controlEpIn  = addr;
                        else                            m_controlEpOut = addr;
                    }
                }
                if (alt.bInterfaceProtocol == ARV_UV_IFACE_DATA_PROTO) {
                    m_dataInterface = alt.bInterfaceNumber;
                    if (alt.bNumEndpoints > 0)
                        m_dataEp = alt.endpoint[0].bEndpointAddress;
                }
            }
        }
        libusb_free_config_descriptor(config);
    }
    qDebug("ArvU3vDevice::open: ctrl=%d EP(out=0x%02X in=0x%02X) "
           "data=%d EP=0x%02X",
           m_controlInterface, m_controlEpOut, m_controlEpIn,
           m_dataInterface, m_dataEp);

    int r = libusb_open(target, &m_handle);
    libusb_free_device_list(devs, 1);
    if (r != LIBUSB_SUCCESS) {
        m_lastError = QString("libusb_open: %1").arg(libusb_error_name(r));
        qWarning("ArvU3vDevice::open: %s", qPrintable(m_lastError));
        return false;
    }

    libusb_set_auto_detach_kernel_driver(m_handle, 1);

    r = libusb_claim_interface(m_handle, m_controlInterface);
    if (r != LIBUSB_SUCCESS) {
        m_lastError = QString("claim ctrl iface %1: %2")
                      .arg(m_controlInterface).arg(libusb_error_name(r));
        qWarning("ArvU3vDevice::open: %s\n"
                 "  -> Windows: Zadig -> WinUSB -> Replace Driver",
                 qPrintable(m_lastError));
        libusb_close(m_handle); m_handle = nullptr;
        return false;
    }

    r = libusb_claim_interface(m_handle, m_dataInterface);
    if (r != LIBUSB_SUCCESS)
        qWarning("ArvU3vDevice::open: claim data iface %d: %s",
                 m_dataInterface, libusb_error_name(r));

    return true;
}

void ArvU3vDevice::close()
{
    if (!m_handle) return;
    libusb_release_interface(m_handle, m_dataInterface);
    libusb_release_interface(m_handle, m_controlInterface);
    libusb_close(m_handle);
    m_handle = nullptr;
}

// ---- bulkTransfer -----------------------------------------------
bool ArvU3vDevice::bulkTransfer(uint8_t endpoint, void* buf, int length,
                                  int& transferred, unsigned int timeoutMs)
{
    transferred = 0;
    if (!m_handle) return false;
    const int r = libusb_bulk_transfer(
        m_handle, endpoint,
        static_cast<unsigned char*>(buf),
        length, &transferred, timeoutMs);
    if (r != LIBUSB_SUCCESS && r != LIBUSB_ERROR_TIMEOUT)
        qDebug("ArvU3vDevice::bulkTransfer EP=0x%02X len=%d xfer=%d err=%s",
               endpoint, length, transferred, libusb_error_name(r));
    return r == LIBUSB_SUCCESS;
}

// ---- sendCmdRecvAck ---------------------------------------------
// Aravis _send_cmd_and_receive_ack() 포팅
//
// ReadMemoryCmd 패킷 레이아웃:
//   [Header 12B] [address 8B] [reserved 2B] [read_size 2B]
//   header.flags = ARV_UVCP_FLAGS_REQUEST_ACK
//   header.size  = 12 (= sizeof(ReadMemoryCmdInfos) -- payload 크기)
//   header.command = ARV_UVCP_COMMAND_READ_MEMORY_CMD
//
// WriteMemoryCmd 패킷 레이아웃:
//   [Header 12B] [address 8B] [data NB]
//   header.size = 8 + N (= sizeof(WriteMemoryCmdInfos) + data)
//
// ReadMemoryAck:
//   [Header 12B] [data NB]
//   data offset = sizeof(ArvUvcpHeader) = 12

bool ArvU3vDevice::sendCmdRecvAck(uint16_t cmdId,
                                    const void* cmdPayload,
                                    uint16_t cmdPayloadLen,
                                    void* ackPayload,
                                    uint16_t ackPayloadMaxLen,
                                    uint16_t& ackPayloadLen)
{
    const uint16_t reqId = nextId();

    // 패킷 구성 (Aravis: arv_uvcp_packet_new_read/write_memory_cmd)
    const size_t pktSize = sizeof(ArvUvcpHeader) + cmdPayloadLen;
    std::vector<uint8_t> pkt(pktSize, 0);
    auto* hdr    = reinterpret_cast<ArvUvcpHeader*>(pkt.data());
    hdr->magic   = ARV_UVCP_MAGIC;
    hdr->flags   = ARV_UVCP_FLAG_REQUEST_ACK;  // cmd 방향: flags 필드
    hdr->command = cmdId;
    hdr->size    = cmdPayloadLen;               // payload 바이트 수
    hdr->id      = reqId;
    if (cmdPayload && cmdPayloadLen)
        std::memcpy(pkt.data() + sizeof(ArvUvcpHeader),
                    cmdPayload, cmdPayloadLen);

    // Aravis: ack_packet_size_max 초기값 = 65536 + sizeof(ArvUvcpHeader)
    // bootstrap 전에는 MAX_ACK_TRANSFER를 모르므로 항상 최대 크기로 수신.
    // 실제 복사는 요청한 크기(ackPayloadMaxLen)만큼만.
    // ackPayloadMaxLen보다 큰 ack가 와도 overflow 없이 수신 가능.
    const size_t ACK_BUF_MAX = 65536 + sizeof(ArvUvcpHeader);
    const size_t ackBufSize  = std::max<size_t>(
        ACK_BUF_MAX,
        sizeof(ArvUvcpHeader) + ackPayloadMaxLen);
    std::vector<uint8_t> ackBuf(ackBufSize, 0);

    // Aravis: retry loop (ARV_UV_DEVICE_N_TRIES_MAX = 5)
    // 재시도 시 새 packet_id + 재전송
    m_lastErrorWasStatus = false;
    for (int attempt = 0; attempt < 5; ++attempt) {

        // attempt > 0: 새 ID로 재전송
        if (attempt > 0) {
            auto* h2 = reinterpret_cast<ArvUvcpHeader*>(pkt.data());
            h2->id = nextId();
        }
        const uint16_t curId =
            reinterpret_cast<ArvUvcpHeader*>(pkt.data())->id;

        int xfer = 0;
        if (!bulkTransfer(m_controlEpOut, pkt.data(),
                          static_cast<int>(pktSize), xfer, 3000)) {
            m_lastError = "send cmd bulk out failed";
            qWarning("ArvU3vDevice: %s (attempt %d)",
                     qPrintable(m_lastError), attempt);
            continue;
        }

        // PENDING_ACK 루프 (Aravis: do { ... } while (pending_ack))
        bool gotExpected = false;
        for (int pendingRetry = 0; pendingRetry < 32; ++pendingRetry) {
            int ackXfer = 0;
            std::fill(ackBuf.begin(), ackBuf.end(), 0);
            if (!bulkTransfer(m_controlEpIn, ackBuf.data(),
                              static_cast<int>(ackBufSize), ackXfer, 3000))
                break;
            if (ackXfer < static_cast<int>(sizeof(ArvUvcpHeader))) break;

            const auto* ack =
                reinterpret_cast<const ArvUvcpHeader*>(ackBuf.data());
            if (ack->magic != ARV_UVCP_MAGIC) break;

            // PENDING_ACK
            if (ack->command == ARV_UVCP_ACK_PENDING) {
                const auto* pa =
                    reinterpret_cast<const ArvUvcpPendingAck*>(ackBuf.data());
                uint16_t waitMs = 5;
                if (ackXfer >= static_cast<int>(sizeof(ArvUvcpPendingAck)))
                    waitMs = pa->timeout > 0 ? pa->timeout : 5;
#ifdef _WIN32
                Sleep(waitMs);
#else
                usleep(static_cast<useconds_t>(waitMs) * 1000);
#endif
                continue;
            }

            const uint16_t status = ack->status;
            const bool idMatch  = (ack->id      == curId);
            const bool cmdMatch = (ack->command == static_cast<uint16_t>(cmdId + 1));

            if (!idMatch || !cmdMatch) {
                qDebug("ArvU3vDevice: unexpected ack cmd=0x%04X id=%u "
                       "(expect cmd=0x%04X id=%u)",
                       ack->command, ack->id, cmdId+1, curId);
                break;
            }
            if (status != ARV_UVCP_STATUS_SUCCESS) {
                m_lastError = QString("GenCP status 0x%1")
                              .arg(status, 4, 16, QChar('0'));
                m_lastErrorWasStatus = true;
                qWarning("ArvU3vDevice: %s cmd=0x%04X",
                         qPrintable(m_lastError), cmdId);
                return false; // camera rejected — retrying won't help
            }

            // 성공
            ackPayloadLen = ack->size;
            if (ackPayload && ackPayloadMaxLen > 0 && ack->size > 0) {
                const uint16_t copyLen =
                    std::min<uint16_t>(ack->size, ackPayloadMaxLen);
                std::memcpy(ackPayload,
                            ackBuf.data() + sizeof(ArvUvcpHeader),
                            copyLen);
            }
            gotExpected = true;
            break;
        }
        if (gotExpected) return true;
    }

    m_lastError = "max retries exhausted";
    qWarning("ArvU3vDevice: %s cmd=0x%04X", qPrintable(m_lastError), cmdId);
    return false;
}

// ---- readMemory -------------------------------------------------
// Aravis: arv_uv_device_read_memory()
// ReadMemoryCmdInfos = { address(8) | reserved(2) | size(2) } = 12 bytes
// #pragma pack(push,1) 필수 - 컴파일러 패딩 방지

bool ArvU3vDevice::readMemory(uint64_t address, void* data,
                               uint32_t size, int retries)
{
    // GenCP ReadMem size 필드 = uint16_t (최대 65535)
    // 카메라마다 한 번에 처리 가능한 크기가 다름
    // Aravis: data_size_max = ack_packet_size_max - sizeof(Header)
    // 실용적 상한: 512 bytes (IDS 카메라 권장, 안정적)
    // bootstrap 후 m_maxAckTransfer 갱신되면 그 값 기준, 단 512 이하로 제한
    const uint32_t dataMax =
        (m_maxAckTransfer > static_cast<uint32_t>(sizeof(ArvUvcpHeader)))
        ? std::min<uint32_t>(m_maxAckTransfer
                             - static_cast<uint32_t>(sizeof(ArvUvcpHeader)),
                             512u)   // 안전한 상한
        : 500u;

    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t blockSize =
            std::min<uint32_t>(dataMax, size - offset);

        // Aravis ArvUvcpReadMemoryCmdInfos: 반드시 pack(1)
#pragma pack(push, 1)
        struct ReadInfos {
            uint64_t address;
            uint16_t reserved;  // always 0
            uint16_t size;      // 읽을 바이트 수
        } infos;
#pragma pack(pop)
        static_assert(sizeof(ReadInfos) == 12,
                      "ReadInfos must be 12 bytes (no padding)");

        infos.address  = address + offset;
        infos.reserved = 0;
        infos.size     = static_cast<uint16_t>(blockSize);

        std::vector<uint8_t> ackBuf(blockSize, 0);
        uint16_t ackLen = 0;

        bool ok = false;
        for (int i = 0; i < retries; ++i) {
            if (sendCmdRecvAck(ARV_UVCP_CMD_READ_MEMORY,
                               &infos,
                               static_cast<uint16_t>(sizeof(infos)),
                               ackBuf.data(),
                               static_cast<uint16_t>(blockSize),
                               ackLen)) {
                ok = true; break;
            }
            if (m_lastErrorWasStatus) break; // camera rejected — don't retry
            qWarning("ArvU3vDevice::readMemory: retry %d "
                     "addr=0x%llX size=%u err=[%s]",
                     i+1, (unsigned long long)(address+offset),
                     blockSize, qPrintable(m_lastError));
        }
        if (!ok) {
            qWarning("ArvU3vDevice::readMemory: FAILED "
                     "addr=0x%llX size=%u retries=%d",
                     (unsigned long long)(address+offset), blockSize, retries);
            return false;
        }

        std::memcpy(static_cast<uint8_t*>(data) + offset,
                    ackBuf.data(), blockSize);
        offset += blockSize;
    }
    return true;
}

// ---- writeMemory ------------------------------------------------
// Aravis: arv_uv_device_write_memory()
// WriteMemoryCmd: [Header][address(8)][data(N)]
// header.size = 8 + N

bool ArvU3vDevice::writeMemory(uint64_t address, const void* data,
                                uint32_t size, int retries)
{
    // Aravis: data_size_max = ack_packet_size_max - sizeof(ArvUvcpHeader)
    const uint32_t dataMax =
        (m_maxCmdTransfer > static_cast<uint32_t>(sizeof(ArvUvcpHeader)) + 8u)
        ? std::min<uint32_t>(m_maxCmdTransfer
                             - static_cast<uint32_t>(sizeof(ArvUvcpHeader)) - 8u,
                             512u)   // 안전한 상한
        : 500u;

    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t blockSize =
            std::min<uint32_t>(dataMax, size - offset);

        // WriteMemoryCmdInfos: address(8) + data(blockSize)
        // header.size = 8 + blockSize (Aravis: sizeof(WriteMemoryCmdInfos) + size)
        std::vector<uint8_t> cmdBuf(8 + blockSize, 0);
        const uint64_t blockAddr = address + offset;
        std::memcpy(cmdBuf.data(), &blockAddr, 8);
        std::memcpy(cmdBuf.data() + 8,
                    static_cast<const uint8_t*>(data) + offset,
                    blockSize);

        // WriteMemoryAck infos: reserved(2) + bytes_written(2)
#pragma pack(push, 1)
        struct WriteAck { uint16_t reserved; uint16_t bytes_written; } ack{};
#pragma pack(pop)
        uint16_t ackLen = 0;

        bool ok = false;
        for (int i = 0; i < retries; ++i) {
            if (sendCmdRecvAck(ARV_UVCP_CMD_WRITE_MEMORY,
                               cmdBuf.data(),
                               static_cast<uint16_t>(cmdBuf.size()),
                               &ack, sizeof(ack), ackLen)) {
                if (ack.bytes_written == static_cast<uint16_t>(blockSize)) {
                    ok = true; break;
                }
                qWarning("ArvU3vDevice::writeMemory: "
                         "bytes_written=%u != blockSize=%u",
                         ack.bytes_written, blockSize);
            } else {
                if (m_lastErrorWasStatus) break; // camera rejected — don't retry
                qWarning("ArvU3vDevice::writeMemory: retry %d "
                         "addr=0x%llX size=%u err=[%s]",
                         i+1, (unsigned long long)(address+offset),
                         blockSize, qPrintable(m_lastError));
            }
        }
        if (!ok) return false;
        offset += blockSize;
    }
    return true;
}

bool ArvU3vDevice::readUInt32(uint64_t addr, uint32_t& out)
{ return readMemory(addr, &out, 4); }

bool ArvU3vDevice::readUInt64(uint64_t addr, uint64_t& out)
{ return readMemory(addr, &out, 8); }

bool ArvU3vDevice::writeUInt32(uint64_t addr, uint32_t v)
{ return writeMemory(addr, &v, 4); }

bool ArvU3vDevice::readString(uint64_t addr, char* buf, size_t maxLen)
{
    std::memset(buf, 0, maxLen);
    readMemory(addr, buf, static_cast<uint32_t>(maxLen - 1));
    buf[maxLen - 1] = '\0';
    return true; // 읽기 실패해도 빈 문자열 반환
}

// ---- bootstrap --------------------------------------------------
// Aravis _bootstrap() 포팅
// ABRM -> SBRM -> SIRM 순서로 읽기
// USB3 Vision ABRM 오프셋 = GenCP 표준 (GigE와 동일한 주소 공간)
// 단, 실제 접근은 USB GenCP ReadMem 커맨드 사용

bool ArvU3vDevice::bootstrap()
{
    if (!m_handle) return false;

    // ── 1. ABRM: 제조사명 (Aravis: manufacturer[64]) ─────────────────────
    char manuf[65]{};
    if (!readMemory(ARV_ABRM_MANUFACTURER_NAME, manuf, 64)) {
        qWarning("ArvU3vDevice::bootstrap: manufacturer read failed");
        return false;
    }
    manuf[64] = '\0';
    qDebug("ArvU3vDevice::bootstrap: MANUFACTURER_NAME='%s'", manuf);

    // 모델명
    char model[65]{};
    readMemory(ARV_ABRM_MODEL_NAME, model, 64);
    model[64] = '\0';
    m_modelName = QString::fromLatin1(model).trimmed();

    // 시리얼
    char serial[65]{};
    readMemory(ARV_ABRM_SERIAL_NUMBER, serial, 64);
    serial[64] = '\0';
    m_serialNumber = QString::fromLatin1(serial).trimmed();

    qDebug("ArvU3vDevice::bootstrap: model='%s' serial='%s'",
           qPrintable(m_modelName), qPrintable(m_serialNumber));

    // ── 2. ABRM: SBRM 주소 (Aravis: offset = SBRM_ADDRESS) ──────────────
    // 실패하면 bootstrap 전체 실패
    if (!readUInt64(ARV_ABRM_SBRM_ADDRESS, m_sbrmAddr) || m_sbrmAddr == 0) {
        qWarning("ArvU3vDevice::bootstrap: SBRM_ADDRESS read failed "
                 "or zero (check WinUSB driver binding)");
        return false;
    }
    qDebug("ArvU3vDevice::bootstrap: SBRM=0x%llX",
           (unsigned long long)m_sbrmAddr);

    // ── 3. SBRM: MaxCmd/Ack, SIRM 주소 ──────────────────────────────────
    uint32_t maxCmd = 0, maxAck = 0;
    if (!readUInt32(m_sbrmAddr + ARV_SBRM_MAX_CMD_TRANSFER, maxCmd) ||
        !readUInt32(m_sbrmAddr + ARV_SBRM_MAX_ACK_TRANSFER, maxAck)) {
        qWarning("ArvU3vDevice::bootstrap: SBRM MAX_CMD/ACK read failed");
        return false;
    }
    if (maxCmd > 0) m_maxCmdTransfer = maxCmd;
    if (maxAck > 0) m_maxAckTransfer = maxAck;
    qDebug("ArvU3vDevice::bootstrap: maxCmd=%u maxAck=%u",
           m_maxCmdTransfer, m_maxAckTransfer);

    if (!readUInt64(m_sbrmAddr + ARV_SBRM_SIRM_ADDRESS, m_sirmAddr)
        || m_sirmAddr == 0) {
        qWarning("ArvU3vDevice::bootstrap: SIRM_ADDRESS read failed or zero");
        return false;
    }
    qDebug("ArvU3vDevice::bootstrap: SIRM=0x%llX",
           (unsigned long long)m_sirmAddr);

    return true;
}

// ---- downloadXml ------------------------------------------------
QByteArray ArvU3vDevice::downloadXml()
{
    if (!m_handle || !m_sbrmAddr) return {};

    uint64_t manifestAddr = 0;
    if (!readUInt64(ARV_ABRM_MANIFEST_TABLE_ADDR, manifestAddr)
        || !manifestAddr) {
        qWarning("ArvU3vDevice::downloadXml: no manifest table");
        return {};
    }

    // Aravis: manifest_n_entries (uint64)
    uint64_t numEntries = 0;
    readUInt64(manifestAddr, numEntries);
    if (numEntries == 0) return {};

    // Aravis: entry at manifest_table_address + 0x08
    ArvUvcpManifestEntry entry{};
    if (!readMemory(manifestAddr + 0x08, &entry, sizeof(entry)))
        return {};

    if (entry.size == 0 || entry.size > 32 * 1024 * 1024) {
        qWarning("ArvU3vDevice::downloadXml: bad entry size %llu",
                 (unsigned long long)entry.size);
        return {};
    }

    const uint32_t schemaType =
        (entry.schema & ARV_UVCP_SCHEMA_TYPE_MASK) >> ARV_UVCP_SCHEMA_TYPE_SHIFT;
    qDebug("ArvU3vDevice::downloadXml: addr=0x%llX size=%llu schema=%s",
           (unsigned long long)entry.address,
           (unsigned long long)entry.size,
           schemaType == ARV_UVCP_SCHEMA_ZIP ? "ZIP" : "RAW");

    // 청크 단위 읽기
    // ReadInfos.size 는 uint16_t (max 65535) 이므로 blockSize 상한 존재
    // 카메라가 큰 단일 요청을 거부할 수 있으므로 512 bytes로 안전하게 제한
    // (Aravis 실제 동작: ack_packet_size_max 기준이지만 IDS는 작은 chunk 권장)
    static const uint32_t XML_CHUNK = 512;
    QByteArray xml;
    xml.reserve(static_cast<int>(entry.size));

    qDebug("ArvU3vDevice::downloadXml: reading %llu bytes in %llu chunk(s)...",
           (unsigned long long)entry.size,
           (unsigned long long)((entry.size + XML_CHUNK - 1) / XML_CHUNK));

    for (uint64_t off = 0; off < entry.size; off += XML_CHUNK) {
        const uint32_t toRead = static_cast<uint32_t>(
            std::min<uint64_t>(XML_CHUNK, entry.size - off));
        std::vector<uint8_t> buf(toRead);

        if (!readMemory(entry.address + off, buf.data(), toRead)) {
            qWarning("ArvU3vDevice::downloadXml: "
                     "chunk read failed @offset=%llu/%llu err=[%s]",
                     (unsigned long long)off,
                     (unsigned long long)entry.size,
                     qPrintable(m_lastError));
            return {};
        }
        xml.append(reinterpret_cast<const char*>(buf.data()),
                   static_cast<int>(toRead));

        // 진행 상황 로깅 (매 10KB마다)
        if ((off % 10240) < XML_CHUNK)
            qDebug("ArvU3vDevice::downloadXml: %llu/%llu bytes",
                   (unsigned long long)(off + toRead),
                   (unsigned long long)entry.size);
    }

    qDebug("ArvU3vDevice::downloadXml: done (%d bytes total)",
           xml.size());
    return xml;
}

// ---- resetDataEndpoint ------------------------------------------
// Aravis: reset_endpoint() → SET_FEATURE(ENDPOINT_HALT) + clear_halt
void ArvU3vDevice::resetDataEndpoint()
{
    if (!m_handle) return;

    int r = libusb_control_transfer(
        m_handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_STANDARD
            | LIBUSB_RECIPIENT_ENDPOINT,
        LIBUSB_REQUEST_SET_FEATURE,
        0,        // ENDPOINT_HALT
        m_dataEp,
        nullptr, 0, 1000);
    if (r < 0)
        qDebug("ArvU3vDevice::resetDataEndpoint: SET_HALT=%s",
               libusb_error_name(r));

    r = libusb_clear_halt(m_handle, m_dataEp);
    if (r < 0)
        qDebug("ArvU3vDevice::resetDataEndpoint: clear_halt=%s",
               libusb_error_name(r));
}

// ---- enableStream -----------------------------------------------
// Aravis: arvuvstream.c SIRM 설정 로직
bool ArvU3vDevice::enableStream(ArvStreamParams& out)
{
    if (!m_handle || !m_sirmAddr) return false;

    uint32_t siInfo     = 0;
    uint64_t reqPayload = 0;
    uint32_t reqLeader  = 0;
    uint32_t reqTrailer = 0;

    readUInt32(m_sirmAddr + ARV_SIRM_INFO,             siInfo);
    readUInt64(m_sirmAddr + ARV_SIRM_REQ_PAYLOAD_SIZE, reqPayload);
    readUInt32(m_sirmAddr + ARV_SIRM_REQ_LEADER_SIZE,  reqLeader);
    readUInt32(m_sirmAddr + ARV_SIRM_REQ_TRAILER_SIZE, reqTrailer);

    qDebug("ArvU3vDevice::enableStream: "
           "SI_INFO=0x%08X REQ_PAYLOAD=%llu REQ_LEADER=%u REQ_TRAILER=%u",
           siInfo, (unsigned long long)reqPayload, reqLeader, reqTrailer);

    // alignment = 1 << ((SI_INFO >> 24) & 0xFF)
    const uint32_t alignment =
        1u << ((siInfo & ARV_SIRM_INFO_ALIGNMENT_MASK)
               >> ARV_SIRM_INFO_ALIGNMENT_SHIFT);

    // maximum_transfer_size (Aravis 기본: ack_size_max, 상한 1MB)
    const uint32_t maxTransfer =
        std::min<uint32_t>(m_maxAckTransfer > 0 ? m_maxAckTransfer
                                                 : 1024 * 1024,
                           1024 * 1024);

    const uint32_t alignedMax = (alignment <= 1)
        ? maxTransfer
        : (maxTransfer / alignment * alignment);

    const uint32_t siLeaderSize  = (reqLeader  > 0 && reqLeader  <= alignedMax)
                                   ? reqLeader  : alignedMax;
    const uint32_t siTrailerSize = (reqTrailer > 0 && reqTrailer <= alignedMax)
                                   ? reqTrailer : alignedMax;

    const uint32_t siPayloadSize  =
        // ── 핵심 최적화: payloadSize를 전체 reqPayload와 동일하게 설정 ──────
        // payloadCount = reqPayload / payloadSize = 1 → bulkRead 1회로 수신
        // libusb가 내부적으로 maxPacketSize 단위로 분할하므로 드라이버 레벨에서는 OK
        // 이전: payloadSize=maxAckTransfer(512KB~1MB) → payloadCount=9회 → 150ms+
        // 이후: payloadSize=reqPayload(4.8MB) → payloadCount=1 → ~15ms
        static_cast<uint32_t>(reqPayload);
    const uint32_t siPayloadCount = 1;
    const uint32_t siTransfer1    = 0;  // 나머지 없음
    const uint32_t siTransfer2 = 0;

    qDebug("ArvU3vDevice::enableStream: "
           "align=%u maxXfer=%u "
           "pSize=%u pCount=%u t1=%u leader=%u trailer=%u",
           alignment, alignedMax,
           siPayloadSize, siPayloadCount, siTransfer1,
           siLeaderSize, siTrailerSize);

    writeUInt32(m_sirmAddr + ARV_SIRM_MAX_LEADER_SIZE,  siLeaderSize);
    writeUInt32(m_sirmAddr + ARV_SIRM_MAX_TRAILER_SIZE, siTrailerSize);
    writeUInt32(m_sirmAddr + ARV_SIRM_PAYLOAD_SIZE,     siPayloadSize);
    writeUInt32(m_sirmAddr + ARV_SIRM_PAYLOAD_COUNT,    siPayloadCount);
    writeUInt32(m_sirmAddr + ARV_SIRM_TRANSFER1_SIZE,   siTransfer1);
    writeUInt32(m_sirmAddr + ARV_SIRM_TRANSFER2_SIZE,   siTransfer2);

    // data endpoint reset (Aravis: arv_uv_device_reset_stream_endpoint)
    resetDataEndpoint();

    // SI_CONTROL_STREAM_ENABLE
    if (!writeUInt32(m_sirmAddr + ARV_SIRM_CONTROL,
                     ARV_SIRM_CONTROL_STREAM_ENABLE)) {
        m_lastError = "SI_CONTROL STREAM_ENABLE failed";
        qWarning("ArvU3vDevice::enableStream: %s", qPrintable(m_lastError));
        return false;
    }

    out.reqPayloadSize     = reqPayload;
    out.payloadSize        = siPayloadSize;
    out.payloadCount       = siPayloadCount;
    out.transfer1Size      = siTransfer1;
    out.leaderSize         = siLeaderSize;
    out.trailerSize        = siTrailerSize;
    out.maximumTransferSize = alignedMax;

    qDebug("ArvU3vDevice::enableStream: stream enabled");
    return true;
}

bool ArvU3vDevice::disableStream()
{
    if (!m_handle || !m_sirmAddr) return true;
    writeUInt32(m_sirmAddr + ARV_SIRM_CONTROL, 0);
    return true;
}

// ---- bulkReadData -----------------------------------------------
bool ArvU3vDevice::bulkReadData(void* buf, int length,
                                 int& transferred, unsigned int timeoutMs)
{
    transferred = 0;
    if (!m_handle) return false;
    const int r = libusb_bulk_transfer(
        m_handle, m_dataEp,
        static_cast<unsigned char*>(buf),
        length, &transferred, timeoutMs);
    // short packet (LIBUSB_ERROR_TIMEOUT with transferred>0) = end of data = OK
    return r == LIBUSB_SUCCESS
        || (r == LIBUSB_ERROR_TIMEOUT && transferred > 0);
}

// ---- loadGenApi -------------------------------------------------
// XML 다운로드 -> ZIP 해제 -> 파싱 -> AcquisitionStart/Stop 주소 확보
bool ArvU3vDevice::loadGenApi()
{
    if (m_genApi.parsed) return m_genApi.acquisitionStart.valid;

    QByteArray xmlRaw = downloadXml();
    if (xmlRaw.isEmpty()) {
        qWarning("ArvU3vDevice::loadGenApi: XML download failed");
        return false;
    }

    // ZIP 압축 여부: ZIP local file signature "PK\x03\x04"
    QByteArray xmlData = xmlRaw;
    if (xmlRaw.size() >= 4 &&
        static_cast<uint8_t>(xmlRaw[0]) == 'P' &&
        static_cast<uint8_t>(xmlRaw[1]) == 'K' &&
        static_cast<uint8_t>(xmlRaw[2]) == 0x03 &&
        static_cast<uint8_t>(xmlRaw[3]) == 0x04) {
        qDebug("ArvU3vDevice::loadGenApi: decompressing ZIP...");
        xmlData = ArvGenApiXml::decompressZip(xmlRaw);
        if (xmlData.isEmpty()) {
            qWarning("ArvU3vDevice::loadGenApi: ZIP decompression failed");
            return false;
        }
    }

    m_genApi = ArvGenApiXml::parse(xmlData);

    // Log ExposureTime and Gain addresses for confirmation
    const struct { const char* name; } kNodes[] = { {"ExposureTime"}, {"Gain"}, {"ExposureMode"}};
    for (auto& entry : kNodes) {
        if (const GenApiNode* n = m_genApi.get(entry.name)) {
            qDebug("ArvU3vDevice::loadGenApi: %-20s addr=0x%010llX  len=%u  type=%d  valid=%d",
                   entry.name, (unsigned long long)n->address,
                   n->length, (int)n->type, (int)n->valid);
        } else {
            qWarning("ArvU3vDevice::loadGenApi: %-20s NOT FOUND in parsed nodes", entry.name);
        }
    }

    return m_genApi.acquisitionStart.valid;
}

// ---- executeAcquisitionStart / Stop ------------------------------
// Aravis: arv_camera_execute_command("AcquisitionStart")
// GenApi XML의 Command 노드 -> IntReg 주소에 CommandValue 씀

static bool execCommandNode(ArvU3vDevice* dev,
                             const ArvCommandNode& node,
                             const QString& tag)
{
    if (!node.valid) {
        qWarning("ArvU3vDevice::%s: command node not valid "
                 "(XML not parsed or node not found)",
                 qPrintable(tag));
        return false;
    }

    uint32_t value = node.commandValue;

    // BigEndian: 바이트 순서 변환 (카메라는 보통 LE, 일부 BigEndian)
    if (node.bigEndian) {
        value = ((value & 0xFF000000u) >> 24) |
                ((value & 0x00FF0000u) >> 8)  |
                ((value & 0x0000FF00u) << 8)  |
                ((value & 0x000000FFu) << 24);
    }

    const bool ok = dev->writeMemory(node.address, &value, node.length);
    if (!ok)
        qWarning("ArvU3vDevice::%s: writeMemory to 0x%llX failed",
                 qPrintable(tag),
                 (unsigned long long)node.address);
    else
        qDebug("ArvU3vDevice::%s: wrote 0x%08X to 0x%llX",
               qPrintable(tag), value,
               (unsigned long long)node.address);
    return ok;
}

bool ArvU3vDevice::executeAcquisitionStart()
{
    return execCommandNode(this, m_genApi.acquisitionStart,
                           "executeAcquisitionStart");
}

bool ArvU3vDevice::executeAcquisitionStop()
{
    return execCommandNode(this, m_genApi.acquisitionStop,
                           "executeAcquisitionStop");
}

ArvU3vDevice::ArvU3vDevice()  {}
ArvU3vDevice::~ArvU3vDevice() { close(); }

// ── ArvU3vDeviceRegAdapter 구현 ──────────────────────────────────────────────
bool ArvU3vDeviceRegAdapter::readRegister(uint32_t addr, uint32_t& val)
{
    uint32_t v{};
    if (!m_dev->readMemory(static_cast<uint64_t>(addr), &v, 4)) return false;
    val = v; return true;
}
bool ArvU3vDeviceRegAdapter::writeRegister(uint32_t addr, uint32_t val)
{
    return m_dev->writeMemory(static_cast<uint64_t>(addr), &val, 4);
}
bool ArvU3vDeviceRegAdapter::readMemory(uint32_t addr, uint8_t* buf, uint32_t sz)
{
    return m_dev->readMemory(static_cast<uint64_t>(addr), buf, sz);
}
bool ArvU3vDeviceRegAdapter::writeMemory(uint32_t addr, const uint8_t* buf, uint32_t sz)
{
    return m_dev->writeMemory(static_cast<uint64_t>(addr), buf, sz);
}
