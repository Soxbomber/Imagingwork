#include "GigEDevice.h"
#include "../genicam/ArvGenApiXml.h"
#include <QNetworkInterface>
#include <QDateTime>
#include <QThread>
#include <QSet>
#include <vector>
#include <memory>
#include <QDebug>
#include <cstring>

// ── 전역: 유니캐스트 탐색 대상 IP 목록 ──────────────────────────────────────
static QList<QHostAddress> s_knownIps;

void GigEDevice::addKnownIp(const QHostAddress& ip)
{
    if (!ip.isNull() && !s_knownIps.contains(ip))
        s_knownIps.append(ip);
}

void GigEDevice::clearKnownIps()
{
    s_knownIps.clear();
}

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

// ── Discovery (Aravis 방식 직접 포팅) ────────────────────────────────────────
// ── discover() ───────────────────────────────────────────────────────────────
// 브로드캐스트 + 서브넷 지향 브로드캐스트로 카메라 탐색
// Switch hub 환경: addKnownIp()로 카메라 IP 등록 시 유니캐스트 병행
QList<GigECameraInfo> GigEDevice::discover(int timeoutMs)
{
    QList<GigECameraInfo> result;
    QMap<QString, bool>   seen;

    // ── Discovery CMD 패킷 ───────────────────────────────────────────────────
    QByteArray pkt(sizeof(GvcpCmdHeader), 0);
    {
        auto* h     = reinterpret_cast<GvcpCmdHeader*>(pkt.data());
        h->key_code = GVCP_PACKET_TYPE_CMD;
        h->flags    = GVCP_DISCOVERY_FLAG_ALLOW_BROADCAST_ACK;
        h->command  = htons16(GVCP_CMD_DISCOVERY);
        h->length   = htons16(0);
        h->req_id   = htons16(1);
    }

    // ── 단일 소켓 바인딩 ─────────────────────────────────────────────────────
    QUdpSocket sock;
    if (!sock.bind(QHostAddress(QHostAddress::AnyIPv4), 0,
                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning("GigEDevice::discover: bind failed: %s",
                 qPrintable(sock.errorString()));
        return result;
    }
    qDebug("GigEDevice::discover: socket bound port=%u", sock.localPort());

    // ── 전송 대상 수집 ───────────────────────────────────────────────────────
    QSet<quint32> sentSet;
    auto sendTo = [&](const QHostAddress& dst) {
        quint32 a = dst.toIPv4Address();
        if (sentSet.contains(a)) return;
        sentSet.insert(a);
        sock.writeDatagram(pkt, dst, GVCP_PORT);
    };

    // 1. 제한 브로드캐스트
    sendTo(QHostAddress("255.255.255.255"));

    // 2. 인터페이스별 서브넷 지향 브로드캐스트
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))      continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        if (  iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (!entry.broadcast().isNull())
                sendTo(entry.broadcast());
        }
    }

    qDebug("GigEDevice::discover: sent to %d targets", (int)sentSet.size());

    // ── ACK 수신 ─────────────────────────────────────────────────────────────
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!sock.waitForReadyRead(50)) continue;
        while (sock.hasPendingDatagrams()) {
            const qint64 psz = sock.pendingDatagramSize();
            if (psz <= 0) { sock.readDatagram(nullptr, 0); continue; }
            QByteArray ack(static_cast<int>(psz), 0);
            QHostAddress sender; quint16 sport{};
            sock.readDatagram(ack.data(), ack.size(), &sender, &sport);

            qDebug("GigEDevice::discover: recv %d bytes from %s:%u",
                   ack.size(), qPrintable(sender.toString()), sport);

            if (ack.size() < int(sizeof(GvcpAckHeader) + GVBS_DISCOVERY_DATA_SIZE))
                continue;
            const auto* ah = reinterpret_cast<const GvcpAckHeader*>(ack.constData());
            if (ntohs16(ah->status)  != GVCP_STATUS_SUCCESS) continue;
            if (ntohs16(ah->command) != GVCP_ACK_DISCOVERY)  continue;

            const uint8_t* data = reinterpret_cast<const uint8_t*>(
                ack.constData() + sizeof(GvcpAckHeader));
            const int dsz = ack.size() - int(sizeof(GvcpAckHeader));
            if (dsz < int(GVBS_DEVICE_MAC_LOW_OFFSET + 4)) continue;

            const uint32_t macH = ntohl32(*reinterpret_cast<const uint32_t*>(
                data + GVBS_DEVICE_MAC_HIGH_OFFSET));
            const uint32_t macL = ntohl32(*reinterpret_cast<const uint32_t*>(
                data + GVBS_DEVICE_MAC_LOW_OFFSET));
            const QString mac = QString("%1:%2:%3:%4:%5:%6")
                .arg((macH>> 8)&0xFF,2,16,QChar('0'))
                .arg((macH   )&0xFF,2,16,QChar('0'))
                .arg((macL>>24)&0xFF,2,16,QChar('0'))
                .arg((macL>>16)&0xFF,2,16,QChar('0'))
                .arg((macL>> 8)&0xFF,2,16,QChar('0'))
                .arg((macL   )&0xFF,2,16,QChar('0'))
                .toUpper();
            if (seen.contains(mac)) continue;
            seen[mac] = true;

            GigECameraInfo info;
            info.ipAddress  = sender;
            info.macAddress = mac;
            if (dsz >= int(GVBS_MANUFACTURER_NAME_OFFSET + GVBS_MANUFACTURER_NAME_SIZE))
                info.manufacturerName = QString::fromLatin1(
                    reinterpret_cast<const char*>(data + GVBS_MANUFACTURER_NAME_OFFSET),
                    GVBS_MANUFACTURER_NAME_SIZE).trimmed();
            if (dsz >= int(GVBS_MODEL_NAME_OFFSET + GVBS_MODEL_NAME_SIZE))
                info.modelName = QString::fromLatin1(
                    reinterpret_cast<const char*>(data + GVBS_MODEL_NAME_OFFSET),
                    GVBS_MODEL_NAME_SIZE).trimmed();
            if (dsz >= int(GVBS_SERIAL_NUMBER_OFFSET + GVBS_SERIAL_NUMBER_SIZE))
                info.serialNumber = QString::fromLatin1(
                    reinterpret_cast<const char*>(data + GVBS_SERIAL_NUMBER_OFFSET),
                    GVBS_SERIAL_NUMBER_SIZE).trimmed();
            info.valid = true;
            result.append(info);
            qDebug("GigEDevice::discover: FOUND [%s %s] sn=%s ip=%s",
                   qPrintable(info.manufacturerName), qPrintable(info.modelName),
                   qPrintable(info.serialNumber), qPrintable(sender.toString()));
        }
    }

    sock.close();
    qDebug("GigEDevice::discover: total %d camera(s) found", result.size());
    return result;
}


// ── discoverUnicast: 카메라 IP 직접 지정 (Switch/VLAN 환경) ──────────────────
// 브로드캐스트가 차단된 Switch hub 환경에서 카메라 IP를 직접 알고 있을 때 사용
// GigE Vision 스펙: Discovery CMD를 유니캐스트로 전송해도 유효
QList<GigECameraInfo> GigEDevice::discoverUnicast(
    const QList<QHostAddress>& knownIps, int timeoutMs)
{
    QList<GigECameraInfo> result;
    if (knownIps.isEmpty()) return result;

    // Discovery CMD (ALLOW_BROADCAST_ACK 없음 = 유니캐스트 ACK)
    QByteArray pkt(sizeof(GvcpCmdHeader), 0);
    {
        auto* h     = reinterpret_cast<GvcpCmdHeader*>(pkt.data());
        h->key_code = GVCP_PACKET_TYPE_CMD;
        h->flags    = GVCP_CMD_FLAG_NONE;   // 브로드캐스트 ACK 불필요
        h->command  = htons16(GVCP_CMD_DISCOVERY);
        h->length   = htons16(0);
        h->req_id   = htons16(1);
    }

    // 인터페이스별 소켓 생성 (각 인터페이스에서 해당 서브넷 카메라 접근)
    struct IfSock {
        QUdpSocket   sock;
        QHostAddress ifaceIp;
        quint32      netmask;
    };
    std::vector<std::unique_ptr<IfSock>> sockets;

    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))      continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        if (  iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            auto s = std::make_unique<IfSock>();
            s->ifaceIp = entry.ip();
            s->netmask = entry.netmask().toIPv4Address();
            if (s->sock.bind(entry.ip(), 0,
                             QUdpSocket::ShareAddress |
                             QUdpSocket::ReuseAddressHint))
                sockets.push_back(std::move(s));
        }
    }

    if (sockets.empty()) return result;

    // 각 카메라 IP에 대해 해당 서브넷 소켓으로 유니캐스트 전송
    for (const QHostAddress& camIp : knownIps) {
        const quint32 camAddr = camIp.toIPv4Address();
        bool sent = false;
        for (auto& s : sockets) {
            const quint32 ifAddr  = s->ifaceIp.toIPv4Address();
            // 같은 서브넷이면 해당 인터페이스 소켓으로 전송
            if ((camAddr & s->netmask) == (ifAddr & s->netmask)) {
                qint64 n = s->sock.writeDatagram(pkt, camIp, GVCP_PORT);
                qDebug("GigEDevice::discoverUnicast: [%s] -> %s:%u (%lld bytes)",
                       qPrintable(s->ifaceIp.toString()),
                       qPrintable(camIp.toString()), GVCP_PORT, (long long)n);
                sent = true;
                break;
            }
        }
        if (!sent) {
            // 서브넷 일치 소켓 없음 → 첫 번째 소켓으로 시도
            if (!sockets.empty()) {
                sockets[0]->sock.writeDatagram(pkt, camIp, GVCP_PORT);
                qDebug("GigEDevice::discoverUnicast: (fallback) -> %s",
                       qPrintable(camIp.toString()));
            }
        }
    }

    // ACK 수집
    QMap<QString, bool> seen;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        for (auto& s : sockets) {
            s->sock.waitForReadyRead(10);
            while (s->sock.hasPendingDatagrams()) {
                const qint64 psz = s->sock.pendingDatagramSize();
                if (psz <= 0) { s->sock.readDatagram(nullptr, 0); continue; }
                QByteArray ack(static_cast<int>(psz), 0);
                QHostAddress sender; quint16 sport{};
                s->sock.readDatagram(ack.data(), ack.size(), &sender, &sport);

                qDebug("GigEDevice::discoverUnicast: recv %d bytes from %s",
                       ack.size(), qPrintable(sender.toString()));

                if (ack.size() < int(sizeof(GvcpAckHeader) + GVBS_DISCOVERY_DATA_SIZE))
                    continue;
                const auto* ah = reinterpret_cast<const GvcpAckHeader*>(ack.constData());
                if (ntohs16(ah->status)  != GVCP_STATUS_SUCCESS) continue;
                if (ntohs16(ah->command) != GVCP_ACK_DISCOVERY)  continue;

                const uint8_t* data = reinterpret_cast<const uint8_t*>(
                    ack.constData() + sizeof(GvcpAckHeader));

                // MAC
                const uint32_t macH = ntohl32(*reinterpret_cast<const uint32_t*>(
                    data + GVBS_DEVICE_MAC_HIGH_OFFSET));
                const uint32_t macL = ntohl32(*reinterpret_cast<const uint32_t*>(
                    data + GVBS_DEVICE_MAC_LOW_OFFSET));
                const QString mac = QString("%1:%2:%3:%4:%5:%6")
                    .arg((macH>> 8)&0xFF,2,16,QChar('0'))
                    .arg((macH   )&0xFF,2,16,QChar('0'))
                    .arg((macL>>24)&0xFF,2,16,QChar('0'))
                    .arg((macL>>16)&0xFF,2,16,QChar('0'))
                    .arg((macL>> 8)&0xFF,2,16,QChar('0'))
                    .arg((macL   )&0xFF,2,16,QChar('0'))
                    .toUpper();
                if (seen.contains(mac)) continue;
                seen[mac] = true;

                GigECameraInfo info;
                info.ipAddress  = sender;
                info.macAddress = mac;
                const int dsz = ack.size() - int(sizeof(GvcpAckHeader));
                if (dsz >= int(GVBS_MANUFACTURER_NAME_OFFSET + GVBS_MANUFACTURER_NAME_SIZE))
                    info.manufacturerName = QString::fromLatin1(
                        reinterpret_cast<const char*>(data + GVBS_MANUFACTURER_NAME_OFFSET),
                        GVBS_MANUFACTURER_NAME_SIZE).trimmed();
                if (dsz >= int(GVBS_MODEL_NAME_OFFSET + GVBS_MODEL_NAME_SIZE))
                    info.modelName = QString::fromLatin1(
                        reinterpret_cast<const char*>(data + GVBS_MODEL_NAME_OFFSET),
                        GVBS_MODEL_NAME_SIZE).trimmed();
                if (dsz >= int(GVBS_SERIAL_NUMBER_OFFSET + GVBS_SERIAL_NUMBER_SIZE))
                    info.serialNumber = QString::fromLatin1(
                        reinterpret_cast<const char*>(data + GVBS_SERIAL_NUMBER_OFFSET),
                        GVBS_SERIAL_NUMBER_SIZE).trimmed();
                info.valid = true;
                result.append(info);
                qDebug("GigEDevice::discoverUnicast: FOUND [%s %s] ip=%s",
                       qPrintable(info.manufacturerName),
                       qPrintable(info.modelName),
                       qPrintable(sender.toString()));
            }
        }
    }
    qDebug("GigEDevice::discoverUnicast: total %d camera(s)", result.size());
    return result;
}


// ── open ──────────────────────────────────────────────────────────────────────
bool GigEDevice::open(const QHostAddress& cameraIp)
{
    m_cameraIp = cameraIp;

    if (!m_socket.bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0),
                       QUdpSocket::DefaultForPlatform)) {
        qWarning("GigEDevice::open: bind failed: %s",
                 qPrintable(m_socket.errorString()));
        return false;
    }
    qDebug("GigEDevice::open: socket bound port=%u target=%s",
           m_socket.localPort(), qPrintable(cameraIp.toString()));

    // ── 통신 테스트: Version 레지스터 읽기 (CCP 없이 가능) ───────────────────
    uint32_t version{};
    if (!readRegister(GVBS_VERSION_OFFSET, version)) {
        qWarning("GigEDevice::open: cannot read Version register "
                 "→ GVCP communication failed (check firewall/route/IP)");
        m_socket.close();
        return false;
    }
    qDebug("GigEDevice::open: GigE Vision version %u.%u",
           (version >> 16) & 0xFFFF, version & 0xFFFF);

    // ── CCP 현재 값 확인 ──────────────────────────────────────────────────────
    uint32_t currentCcp{};
    readRegister(GVBS_CCP_OFFSET, currentCcp);
    qDebug("GigEDevice::open: current CCP = 0x%08X", currentCcp);

    // ── CCP 획득 ─────────────────────────────────────────────────────────────
    // GigE Vision Spec / Aravis:
    //   0x00000002 = Control access (Aravis 기본, ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE_CONTROL)
    //   0x00000001 = Exclusive access
    bool ccpOk = writeRegister(GVBS_CCP_OFFSET, GVBS_CCP_CONTROL_ACCESS);
    if (!ccpOk) {
        qDebug("GigEDevice::open: Control access failed, trying Exclusive");
        ccpOk = writeRegister(GVBS_CCP_OFFSET, GVBS_CCP_EXCLUSIVE_ACCESS);
    }
    if (!ccpOk && currentCcp != 0) {
        qWarning("GigEDevice::open: CCP busy (0x%08X), waiting 3.5s...",
                 currentCcp);
        QThread::msleep(3500);
        ccpOk = writeRegister(GVBS_CCP_OFFSET, GVBS_CCP_CONTROL_ACCESS);
    }
    if (!ccpOk) {
        qWarning("GigEDevice::open: CCP acquisition failed. "
                 "Ensure no other GigE app is open, or power-cycle the camera.");
        m_socket.close();
        return false;
    }
    uint32_t gotCcp{};
    readRegister(GVBS_CCP_OFFSET, gotCcp);
    qDebug("GigEDevice::open: CCP acquired = 0x%08X", gotCcp);

    // ── 카메라 정보 읽기 ──────────────────────────────────────────────────────
    uint32_t capVal{};
    if (readRegister(GVBS_GVCP_CAPABILITY_OFFSET, capVal))
        m_pendingAckSupported = (capVal & GVBS_GVCP_CAP_PENDING_ACK) != 0;

    QByteArray mfr(GVBS_MANUFACTURER_NAME_SIZE + 1, 0);
    if (readMemory(GVBS_MANUFACTURER_NAME_OFFSET,
                   reinterpret_cast<uint8_t*>(mfr.data()),
                   GVBS_MANUFACTURER_NAME_SIZE))
        m_info.manufacturerName = QString::fromLatin1(mfr).trimmed();

    QByteArray mdl(GVBS_MODEL_NAME_SIZE + 1, 0);
    if (readMemory(GVBS_MODEL_NAME_OFFSET,
                   reinterpret_cast<uint8_t*>(mdl.data()),
                   GVBS_MODEL_NAME_SIZE))
        m_info.modelName = QString::fromLatin1(mdl).trimmed();

    QByteArray sn(GVBS_SERIAL_NUMBER_SIZE + 1, 0);
    if (readMemory(GVBS_SERIAL_NUMBER_OFFSET,
                   reinterpret_cast<uint8_t*>(sn.data()),
                   GVBS_SERIAL_NUMBER_SIZE))
        m_info.serialNumber = QString::fromLatin1(sn).trimmed();

    m_open = true;
    m_heartbeatTimer.start(1000);
    qDebug("GigEDevice::open: connected [%s %s] sn=%s",
           qPrintable(m_info.manufacturerName),
           qPrintable(m_info.modelName),
           qPrintable(m_info.serialNumber));
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

                if (buf.size() < int(sizeof(GvcpAckHeader))) continue;
                const auto* ah =
                    reinterpret_cast<const GvcpAckHeader*>(buf.constData());

                // Pending ACK: 카메라가 처리 중 → 타임아웃 연장
                if (ntohs16(ah->command) == GVCP_ACK_PENDING) {
                    const auto* pa =
                        reinterpret_cast<const GvcpPendingAck*>(buf.constData());
                    remain += ntohs16(pa->timeout_ms);
                    continue;
                }

                if (ntohs16(ah->command) != expectedAck) continue;

                // req_id 확인 (0은 일부 카메라의 broadcast ACK)
                const auto* ch =
                    reinterpret_cast<const GvcpCmdHeader*>(cmd.constData());
                const uint16_t ackId = ntohs16(ah->req_id);
                const uint16_t cmdId = ntohs16(ch->req_id);
                if (ackId != 0 && ackId != cmdId) continue;

                // ACK status 확인: 0x0000 = SUCCESS
                const uint16_t ackStatus = ntohs16(ah->status);
                if (ackStatus != GVCP_STATUS_SUCCESS) {
                    qWarning("GigEDevice::sendCmd: ACK status error "
                             "cmd=0x%04X status=0x%04X",
                             ntohs16(ah->command), ackStatus);
                    // 에러 ACK도 수신으로 처리 (상위 레이어에서 판단)
                    // CCP ACCESS_DENIED 같은 경우 false 반환이 맞으므로 여기서 false
                    return false;
                }

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
    c->header.key_code     = GVCP_PACKET_TYPE_CMD;
    c->header.flags        = GVCP_CMD_FLAG_ACK_REQUIRED;
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
    c->header.key_code     = GVCP_PACKET_TYPE_CMD;
    c->header.flags        = GVCP_CMD_FLAG_ACK_REQUIRED;
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
        c->header.key_code     = GVCP_PACKET_TYPE_CMD;
        c->header.flags        = GVCP_CMD_FLAG_ACK_REQUIRED;
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
        c->header.key_code     = GVCP_PACKET_TYPE_CMD;
        c->header.flags        = GVCP_CMD_FLAG_ACK_REQUIRED;
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
    // GigE Vision 스펙 + IDS Peak 분석 (peak_log.pcapng):
    //
    //   SCDA0 쓰기 제약: SCP0(port) = 0인 상태에서만 SCDA0 변경 허용
    //   → SCP0에 포트가 설정된 상태(스트림 활성 중)이면 ACCESS_DENIED(0x8006)
    //
    //   올바른 순서:
    //     1. SCP0 = 0 (포트 클리어 → 스트림 채널 비활성화)
    //     2. SCDA0 = hostIp (목적지 IP)
    //     3. SCP0 = hostPort (포트 설정 → 스트림 채널 활성화)
    //
    //   IDS Peak 패킷 순서:
    //     pkt 976:  READREG SCP0 = 0x00000000 (이미 0 확인)
    //     pkt 1319: WRITEREG SCDA0 = 0xC0A82301 → Success
    //     pkt 1325: WRITEREG SCP0  = 0x0000E04F → Success

    qDebug("GigEDevice::setStreamDestination: ip=%s port=%u",
           qPrintable(hostIp.toString()), hostPort);

    // 1. SCP0 = 0 (SCDA0 쓰기 잠금 해제)
    writeRegister(GVBS_SC0_PORT_OFFSET, 0);

    // 2. SCDA0 = host IP
    if (!writeRegister(GVBS_SC0_IP_ADDRESS_OFFSET, hostIp.toIPv4Address())) {
        qWarning("GigEDevice::setStreamDestination: SCDA0 write failed");
        return false;
    }

    // 3. SCP0 = port (하위 16bit, IDS Peak 방식)
    if (!writeRegister(GVBS_SC0_PORT_OFFSET, static_cast<uint32_t>(hostPort))) {
        qWarning("GigEDevice::setStreamDestination: SCP0 write failed");
        return false;
    }

    qDebug("GigEDevice::setStreamDestination: OK (port=%u)", hostPort);
    return true;
}

// ── setStreamPacketSize ───────────────────────────────────────────────────────
bool GigEDevice::setStreamPacketSize(uint16_t packetSize)
{
    // IDS Peak pcapng 분석 결과:
    //   SCPS0 = 0x40002324
    //     bit15~0:  0x2324 = 9012 (PacketSize) → 하위 16bit
    //     bit14:    1 = DoNotFragment
    //
    // 우리 이전 코드: packetSize << 16 → 카메라가 PacketSize=0 인식
    //
    // 최종값: DoNotFragment(bit14=0x4000) | packetSize
    const uint32_t flags = 0x4000;  // bit14 = DoNotFragment
    return writeRegister(GVBS_SC0_PACKET_SIZE_OFFSET,
                         flags | static_cast<uint32_t>(packetSize));
}

// ── negotiatePacketSize ───────────────────────────────────────────────────────
uint16_t GigEDevice::negotiatePacketSize(uint16_t desired)
{
    // IDS Peak 패킷 크기 협상 과정 (peak_log.pcapng):
    //   1. SCPS0 = 0x00004000 (PacketSize=0, DoNotFragment) 써서 테스트 패킷 요청
    //   2. SCPS0 읽기 → 카메라가 지원 가능한 최대값으로 조정 (0x2324 = 9012)
    //   3. SCPS0 = 0xC0002324 (협상 비트 | 9012)
    //   4. SCPS0 = 0x40002324 (최종 확정)
    //
    // Switch hub 환경이므로 1472로 고정
    // (NIC Jumbo=9014이지만 Switch가 지원 안 할 수 있음)
    const uint16_t safe = 1472;
    qDebug("GigEDevice::negotiatePacketSize: setting %u", safe);
    setStreamPacketSize(safe);
    return safe;
}

// ── loadGenApiXml ─────────────────────────────────────────────────────────────
QByteArray GigEDevice::loadGenApiXml()
{
    QByteArray urlBuf(GVBS_XML_URL_SIZE, 0);
    if (!readMemory(GVBS_XML_URL_0_OFFSET,
                    reinterpret_cast<uint8_t*>(urlBuf.data()),
                    GVBS_XML_URL_SIZE))
        return {};

    // URL 문자열 추출 (null terminator까지만)
    const QString url = QString::fromLatin1(
        urlBuf.constData(),
        qstrnlen(urlBuf.constData(), GVBS_XML_URL_SIZE)).trimmed();
    qDebug("GigEDevice::loadGenApiXml: URL = [%s]", qPrintable(url));

    // GigE Vision Spec: "Local:filename;address;size"
    // address/size는 0x 접두사 없는 hex 또는 접두사 있는 hex 모두 허용
    // 예: "local:GV-504xFA-C.zip;70000000;112f0"
    //      → addr=0x70000000, size=0x112f0=70384

    if (url.startsWith("Local:", Qt::CaseInsensitive)) {
        // "Local:" 다음부터 ';'로 분리
        const QString body  = url.mid(6);       // "Local:" = 6 chars
        const QStringList parts = body.split(';');

        if (parts.size() >= 3) {
            bool ok1 = false, ok2 = false;

            // 주소/크기: '0x' 접두사 있으면 자동, 없으면 16진수로 강제 파싱
            QString addrStr = parts[1].trimmed();
            QString sizeStr = parts[2].trimmed();

            // toUInt(0) 은 '0x' 접두사가 있을 때만 hex로 파싱
            // → 접두사 없는 순수 hex ('70000000', '112f0')는 base=16 명시 필요
            const int base = (addrStr.startsWith("0x", Qt::CaseInsensitive) ||
                              addrStr.startsWith("0X")) ? 0 : 16;

            const uint32_t addr = addrStr.toUInt(&ok1, base);
            const uint32_t sz   = sizeStr.toUInt(&ok2, base);

            qDebug("GigEDevice::loadGenApiXml: parsed addr=0x%08X size=0x%X (%u bytes)",
                   addr, sz, sz);

            if (ok1 && ok2 && sz > 0 && sz < 8 * 1024 * 1024) {
                QByteArray xmlData(int(sz), 0);
                if (readMemory(addr,
                               reinterpret_cast<uint8_t*>(xmlData.data()),
                               sz)) {
                    qDebug("GigEDevice::loadGenApiXml: read %d bytes from 0x%08X",
                           xmlData.size(), addr);
                    // ZIP 헤더 확인 (PK\x03\x04)
                    if (xmlData.size() >= 4 &&
                        uint8_t(xmlData[0]) == 0x50 &&
                        uint8_t(xmlData[1]) == 0x4B) {
                        qDebug("GigEDevice::loadGenApiXml: decompressing ZIP");
                        return ArvGenApiXml::decompressZip(xmlData);
                    }
                    return xmlData;
                } else {
                    qWarning("GigEDevice::loadGenApiXml: readMemory failed "
                             "addr=0x%08X size=%u", addr, sz);
                }
            } else {
                qWarning("GigEDevice::loadGenApiXml: parse failed "
                         "ok1=%d ok2=%d addr=0x%08X sz=%u",
                         ok1, ok2, addr, sz);
            }
        } else {
            qWarning("GigEDevice::loadGenApiXml: not enough parts (%d)",
                     parts.size());
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
