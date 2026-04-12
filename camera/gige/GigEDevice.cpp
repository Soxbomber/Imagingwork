#include "GigEDevice.h"
#include "../genicam/ArvGenApiXml.h"
#include <QNetworkInterface>
#include <QDateTime>
#include <QDebug>
#include <cstring>

// ── 바이트 순서 변환 ─────────────────────────────────────────────────────────
uint16_t GigEDevice::htons16(uint16_t v) {
    return ((v & 0xFF) << 8) | ((v >> 8) & 0xFF);
}
uint32_t GigEDevice::htonl32(uint32_t v) {
    return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) |
           (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
}
uint16_t GigEDevice::ntohs16(uint16_t v) { return htons16(v); }
uint32_t GigEDevice::ntohl32(uint32_t v) { return htonl32(v); }

// ── 생성자 / 소멸자 ───────────────────────────────────────────────────────────
GigEDevice::GigEDevice(QObject* parent)
    : QObject(parent)
{
    connect(&m_heartbeatTimer, &QTimer::timeout,
            this, &GigEDevice::onHeartbeat);
}

GigEDevice::~GigEDevice()
{
    close();
}

// ── Discovery: 브로드캐스트로 카메라 탐색 ─────────────────────────────────────
QList<GigECameraInfo> GigEDevice::discover(int timeoutMs)
{
    QList<GigECameraInfo> result;

    QUdpSocket sock;
    sock.bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0),
              QUdpSocket::ShareAddress);
    sock.setSocketOption(QAbstractSocket::MulticastTtlOption, 1);

    // Discovery CMD 패킷 구성
    QByteArray pkt(sizeof(GvcpHeader), 0);
    auto* h = reinterpret_cast<GvcpHeader*>(pkt.data());
    h->packet_type  = GVCP_PACKET_TYPE_CMD;
    h->packet_flags = GVCP_DISCOVERY_FLAG_ALLOW_BROADCAST_ACK;
    h->command      = htons16(GVCP_CMD_DISCOVERY);
    h->length       = htons16(0);
    h->req_id       = htons16(1);

    // 브로드캐스트 전송
    sock.writeDatagram(pkt, QHostAddress::Broadcast, GVCP_PORT);
    qDebug("GigEDevice::discover: sent discovery broadcast");

    // 응답 수집
    qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!sock.waitForReadyRead(50)) continue;

        while (sock.hasPendingDatagrams()) {
            QByteArray ack(sock.pendingDatagramSize(), 0);
            QHostAddress sender;
            quint16 senderPort;
            sock.readDatagram(ack.data(), ack.size(), &sender, &senderPort);

            if (ack.size() < int(sizeof(GvcpHeader) + GVBS_DISCOVERY_DATA_SIZE))
                continue;

            const auto* ah = reinterpret_cast<const GvcpHeader*>(ack.constData());
            if (ntohs16(ah->command) != GVCP_ACK_DISCOVERY) continue;
            if (ntohs16(ah->req_id) != 1) continue;

            // Discovery ACK payload: bootstrap 레지스터 0x0000 ~ 0x00F7
            const uint8_t* data = reinterpret_cast<const uint8_t*>(
                ack.constData() + sizeof(GvcpHeader));

            GigECameraInfo info;
            info.ipAddress = sender;

            // MAC address (offset 0x08, 0x0C in bootstrap → offset in discovery data)
            // Discovery data starts at 0x0000, so offset directly
            uint32_t macH = ntohl32(*reinterpret_cast<const uint32_t*>(
                data + (GVBS_DEVICE_MAC_HIGH_OFFSET)));
            uint32_t macL = ntohl32(*reinterpret_cast<const uint32_t*>(
                data + (GVBS_DEVICE_MAC_LOW_OFFSET)));
            info.macAddress = QString("%1:%2:%3:%4:%5:%6")
                .arg((macH >> 8)  & 0xFF, 2, 16, QChar('0'))
                .arg((macH)       & 0xFF, 2, 16, QChar('0'))
                .arg((macL >> 24) & 0xFF, 2, 16, QChar('0'))
                .arg((macL >> 16) & 0xFF, 2, 16, QChar('0'))
                .arg((macL >> 8)  & 0xFF, 2, 16, QChar('0'))
                .arg((macL)       & 0xFF, 2, 16, QChar('0'))
                .toUpper();

            // Manufacturer name (offset 0x48)
            info.manufacturerName = QString::fromLatin1(
                reinterpret_cast<const char*>(data + GVBS_MANUFACTURER_NAME_OFFSET),
                GVBS_MANUFACTURER_NAME_SIZE).trimmed();

            // Model name (offset 0x68)
            info.modelName = QString::fromLatin1(
                reinterpret_cast<const char*>(data + GVBS_MODEL_NAME_OFFSET),
                GVBS_MODEL_NAME_SIZE).trimmed();

            // Serial number (offset 0xD8)
            info.serialNumber = QString::fromLatin1(
                reinterpret_cast<const char*>(data + GVBS_SERIAL_NUMBER_OFFSET),
                GVBS_SERIAL_NUMBER_SIZE).trimmed();

            info.valid = true;
            result.append(info);

            qDebug("GigEDevice::discover: found [%s %s] IP=%s MAC=%s",
                   qPrintable(info.manufacturerName),
                   qPrintable(info.modelName),
                   qPrintable(sender.toString()),
                   qPrintable(info.macAddress));
        }
    }

    return result;
}

// ── open ──────────────────────────────────────────────────────────────────────
bool GigEDevice::open(const QHostAddress& cameraIp)
{
    m_cameraIp = cameraIp;

    // 임의 포트에서 UDP 소켓 바인딩 (포트 0 = OS가 임의 포트 할당)
    if (!m_socket.bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0),
                       QUdpSocket::DefaultForPlatform)) {
        qWarning("GigEDevice::open: bind failed: %s",
                 qPrintable(m_socket.errorString()));
        return false;
    }

    // 카메라 정보 읽기
    uint32_t val{};

    // Manufacturer name
    QByteArray mfr(GVBS_MANUFACTURER_NAME_SIZE + 1, 0);
    if (readMemory(GVBS_MANUFACTURER_NAME_OFFSET,
                   reinterpret_cast<uint8_t*>(mfr.data()),
                   GVBS_MANUFACTURER_NAME_SIZE))
        m_info.manufacturerName = QString::fromLatin1(mfr).trimmed();

    // Model name
    QByteArray mdl(GVBS_MODEL_NAME_SIZE + 1, 0);
    if (readMemory(GVBS_MODEL_NAME_OFFSET,
                   reinterpret_cast<uint8_t*>(mdl.data()),
                   GVBS_MODEL_NAME_SIZE))
        m_info.modelName = QString::fromLatin1(mdl).trimmed();

    // Serial number
    QByteArray sn(GVBS_SERIAL_NUMBER_SIZE + 1, 0);
    if (readMemory(GVBS_SERIAL_NUMBER_OFFSET,
                   reinterpret_cast<uint8_t*>(sn.data()),
                   GVBS_SERIAL_NUMBER_SIZE))
        m_info.serialNumber = QString::fromLatin1(sn).trimmed();

    // GVCP capability
    if (readRegister(GVBS_GVCP_CAPABILITY_OFFSET, val))
        m_pendingAckSupported = (val & GVBS_GVCP_CAP_PENDING_ACK) != 0;

    // Control Channel Privilege: exclusive access
    if (!writeRegister(GVBS_CCP_OFFSET, GVBS_CCP_EXCLUSIVE_ACCESS)) {
        qWarning("GigEDevice::open: failed to acquire CCP exclusive access");
        m_socket.close();
        return false;
    }

    m_open = true;

    // Heartbeat 시작 (기본 3초 타임아웃, 1초마다 keepalive)
    m_heartbeatTimer.start(1000);

    qDebug("GigEDevice::open: connected to %s [%s %s]",
           qPrintable(cameraIp.toString()),
           qPrintable(m_info.manufacturerName),
           qPrintable(m_info.modelName));
    return true;
}

// ── close ─────────────────────────────────────────────────────────────────────
void GigEDevice::close()
{
    if (!m_open) return;
    m_heartbeatTimer.stop();

    // CCP 해제 (다른 호스트가 접속 가능하도록)
    writeRegister(GVBS_CCP_OFFSET, 0x00000000);

    m_socket.close();
    m_open = false;
    qDebug("GigEDevice::close: disconnected");
}

// ── sendCmd: GVCP 명령 송수신 (재전송 포함) ──────────────────────────────────
bool GigEDevice::sendCmd(const QByteArray& cmd, QByteArray& ack,
                          uint16_t expectedAck, int timeoutMs)
{
    for (uint32_t attempt = 0; attempt < GVCP_MAX_RETRIES; ++attempt) {
        m_socket.writeDatagram(cmd, m_cameraIp, GVCP_PORT);

        int remain = timeoutMs;
        while (remain > 0) {
            if (!m_socket.waitForReadyRead(std::min(remain, 50))) {
                remain -= 50;
                continue;
            }

            while (m_socket.hasPendingDatagrams()) {
                QByteArray buf(int(m_socket.pendingDatagramSize()), 0);
                m_socket.readDatagram(buf.data(), buf.size());

                if (buf.size() < int(sizeof(GvcpHeader))) continue;
                const auto* ah =
                    reinterpret_cast<const GvcpHeader*>(buf.constData());

                // Pending ACK: 카메라가 처리 중 → 타임아웃 연장
                if (ntohs16(ah->command) == GVCP_ACK_PENDING) {
                    const auto* pa =
                        reinterpret_cast<const GvcpPendingAck*>(buf.constData());
                    remain += ntohs16(pa->timeout_ms);
                    continue;
                }

                if (ntohs16(ah->command) != expectedAck) continue;

                // req_id 확인
                const auto* ch =
                    reinterpret_cast<const GvcpHeader*>(cmd.constData());
                if (ntohs16(ah->req_id) != ntohs16(ch->req_id)) continue;

                if (ntohs16(ah->req_id) != 0 &&
                    static_cast<uint16_t>(ntohs16(ah->req_id)) !=
                    static_cast<uint16_t>(ntohs16(ch->req_id)))
                    continue;

                ack = buf;
                return true;
            }
        }
    }
    return false;
}

// ── readRegister ──────────────────────────────────────────────────────────────
bool GigEDevice::readRegister(uint32_t address, uint32_t& value)
{
    QByteArray pkt(sizeof(GvcpReadRegCmd), 0);
    auto* c    = reinterpret_cast<GvcpReadRegCmd*>(pkt.data());
    c->header.packet_type  = GVCP_PACKET_TYPE_CMD;
    c->header.packet_flags = GVCP_CMD_FLAG_ACK_REQUIRED;
    c->header.command      = htons16(GVCP_CMD_READ_REGISTER);
    c->header.length       = htons16(4);
    c->header.req_id       = htons16(nextReqId());
    c->address             = htonl32(address);

    QByteArray ack;
    if (!sendCmd(pkt, ack, GVCP_ACK_READ_REGISTER)) return false;
    if (ack.size() < int(sizeof(GvcpReadRegAck))) return false;

    const auto* a = reinterpret_cast<const GvcpReadRegAck*>(ack.constData());
    if (ntohs16(a->header.req_id) == 0 &&
        ntohs16(a->header.command) != GVCP_ACK_READ_REGISTER)
        return false;

    value = ntohl32(a->value);
    return true;
}

// ── writeRegister ─────────────────────────────────────────────────────────────
bool GigEDevice::writeRegister(uint32_t address, uint32_t value)
{
    QByteArray pkt(sizeof(GvcpWriteRegCmd), 0);
    auto* c    = reinterpret_cast<GvcpWriteRegCmd*>(pkt.data());
    c->header.packet_type  = GVCP_PACKET_TYPE_CMD;
    c->header.packet_flags = GVCP_CMD_FLAG_ACK_REQUIRED;
    c->header.command      = htons16(GVCP_CMD_WRITE_REGISTER);
    c->header.length       = htons16(8);
    c->header.req_id       = htons16(nextReqId());
    c->address             = htonl32(address);
    c->value               = htonl32(value);

    QByteArray ack;
    return sendCmd(pkt, ack, GVCP_ACK_WRITE_REGISTER);
}

// ── readMemory ────────────────────────────────────────────────────────────────
bool GigEDevice::readMemory(uint32_t address, uint8_t* data, uint32_t size)
{
    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t chunk = std::min(size - offset,
                                        static_cast<uint32_t>(GVCP_MAX_DATA_SIZE));

        QByteArray pkt(sizeof(GvcpReadMemCmd), 0);
        auto* c    = reinterpret_cast<GvcpReadMemCmd*>(pkt.data());
        c->header.packet_type  = GVCP_PACKET_TYPE_CMD;
        c->header.packet_flags = GVCP_CMD_FLAG_ACK_REQUIRED;
        c->header.command      = htons16(GVCP_CMD_READ_MEMORY);
        c->header.length       = htons16(8);
        c->header.req_id       = htons16(nextReqId());
        c->address             = htonl32(address + offset);
        c->reserved            = 0;
        c->count               = htons16(static_cast<uint16_t>(chunk));

        QByteArray ack;
        if (!sendCmd(pkt, ack, GVCP_ACK_READ_MEMORY)) return false;

        const int dataOffset = int(sizeof(GvcpReadMemAck));
        if (ack.size() < dataOffset + int(chunk)) return false;

        std::memcpy(data + offset,
                    ack.constData() + dataOffset, chunk);
        offset += chunk;
    }
    return true;
}

// ── writeMemory ───────────────────────────────────────────────────────────────
bool GigEDevice::writeMemory(uint32_t address, const uint8_t* data,
                               uint32_t size)
{
    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t chunk = std::min(size - offset,
                                        static_cast<uint32_t>(GVCP_MAX_DATA_SIZE));

        QByteArray pkt(int(sizeof(GvcpWriteMemCmd)) + int(chunk), 0);
        auto* c = reinterpret_cast<GvcpWriteMemCmd*>(pkt.data());
        c->header.packet_type  = GVCP_PACKET_TYPE_CMD;
        c->header.packet_flags = GVCP_CMD_FLAG_ACK_REQUIRED;
        c->header.command      = htons16(GVCP_CMD_WRITE_MEMORY);
        c->header.length       = htons16(static_cast<uint16_t>(4 + chunk));
        c->header.req_id       = htons16(nextReqId());
        c->address             = htonl32(address + offset);
        std::memcpy(pkt.data() + sizeof(GvcpWriteMemCmd),
                    data + offset, chunk);

        QByteArray ack;
        if (!sendCmd(pkt, ack, GVCP_ACK_WRITE_MEMORY)) return false;
        offset += chunk;
    }
    return true;
}

// ── setStreamDestination ──────────────────────────────────────────────────────
bool GigEDevice::setStreamDestination(const QHostAddress& hostIp,
                                       uint16_t hostPort)
{
    // SC0: host IP — QHostAddress::toIPv4Address()는 host byte order 반환
    // writeRegister 내부에서 htonl32()로 big-endian 변환하므로 그대로 전달
    if (!writeRegister(GVBS_SC0_IP_ADDRESS_OFFSET, hostIp.toIPv4Address()))
        return false;

    // SC0: host port (상위 16bit에 저장)
    return writeRegister(GVBS_SC0_PORT_OFFSET,
                         static_cast<uint32_t>(hostPort) << 16);
}

// ── setStreamPacketSize ───────────────────────────────────────────────────────
bool GigEDevice::setStreamPacketSize(uint16_t packetSize)
{
    return writeRegister(GVBS_SC0_PACKET_SIZE_OFFSET,
                         static_cast<uint32_t>(packetSize));
}

// ── negotiatePacketSize ───────────────────────────────────────────────────────
uint16_t GigEDevice::negotiatePacketSize(uint16_t desired)
{
    // 방화벽/MTU 제한으로 Jumbo frame 불가 시 표준 MTU로 폴백
    if (!setStreamPacketSize(desired)) {
        setStreamPacketSize(1500 - 28);  // IP(20) + UDP(8) overhead
        return 1472;
    }
    return desired;
}

// ── loadGenApiXml ─────────────────────────────────────────────────────────────
QByteArray GigEDevice::loadGenApiXml()
{
    // XML URL 레지스터 읽기: "Local:filename.zip;address;size"
    QByteArray urlBuf(GVBS_XML_URL_SIZE, 0);
    if (!readMemory(GVBS_XML_URL_0_OFFSET,
                    reinterpret_cast<uint8_t*>(urlBuf.data()),
                    GVBS_XML_URL_SIZE))
        return {};

    const QString url = QString::fromLatin1(urlBuf).trimmed();
    qDebug("GigEDevice::loadGenApiXml: URL = [%s]", qPrintable(url));

    // "Local:filename.zip;0x000100;0x001234" 파싱
    if (url.startsWith("Local:", Qt::CaseInsensitive)) {
        // format: Local:name;addr;size
        const QStringList parts = url.mid(6).split(';');
        if (parts.size() >= 3) {
            bool ok1, ok2;
            const uint32_t addr = parts[1].trimmed().toUInt(&ok1, 0);
            const uint32_t sz   = parts[2].trimmed().toUInt(&ok2, 0);
            if (ok1 && ok2 && sz > 0 && sz < 4 * 1024 * 1024) {
                QByteArray xmlData(int(sz), 0);
                if (readMemory(addr, reinterpret_cast<uint8_t*>(xmlData.data()), sz)) {
                    // ZIP 여부 확인
                    if (xmlData.startsWith("PK") ||
                        (xmlData.size() > 4 &&
                         uint8_t(xmlData[0]) == 0x50 &&
                         uint8_t(xmlData[1]) == 0x4B))
                        return ArvGenApiXml::decompressZip(xmlData);
                    return xmlData;
                }
            }
        }
    }

    qWarning("GigEDevice::loadGenApiXml: unsupported URL format [%s]",
             qPrintable(url));
    return {};
}

// ── Heartbeat ─────────────────────────────────────────────────────────────────
void GigEDevice::onHeartbeat()
{
    // CCP 레지스터 읽기로 keepalive (heartbeat 역할)
    uint32_t val{};
    if (!readRegister(GVBS_CCP_OFFSET, val)) {
        qWarning("GigEDevice: heartbeat failed — camera may have disconnected");
    }
}
