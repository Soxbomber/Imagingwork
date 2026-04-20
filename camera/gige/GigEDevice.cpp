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

// ── Known IPs for unicast discovery ──────────────────────────────────────────
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

// ── Byte order helpers ────────────────────────────────────────────────────────
uint16_t GigEDevice::htons16(uint16_t v) {
    return uint16_t((v & 0xFF) << 8) | uint16_t(v >> 8);
}
uint32_t GigEDevice::htonl32(uint32_t v) {
    return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) |
           (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
}
uint16_t GigEDevice::ntohs16(uint16_t v) { return htons16(v); }
uint32_t GigEDevice::ntohl32(uint32_t v) { return htonl32(v); }

// ── GVCP status → human-readable string ──────────────────────────────────────
const char* GigEDevice::gvcpStatusString(uint16_t status)
{
    switch (status) {
    case GVCP_STATUS_SUCCESS:                          return "SUCCESS";
    case GVCP_STATUS_PACKET_RESEND:                    return "PACKET_RESEND";
    case GVCP_STATUS_NOT_IMPLEMENTED:                  return "NOT_IMPLEMENTED";
    case GVCP_STATUS_INVALID_PARAMETER:                return "INVALID_PARAMETER";
    case GVCP_STATUS_INVALID_ADDRESS:                  return "INVALID_ADDRESS";
    case GVCP_STATUS_WRITE_PROTECT:                    return "WRITE_PROTECT";
    case GVCP_STATUS_BAD_ALIGNMENT:                    return "BAD_ALIGNMENT";
    case GVCP_STATUS_ACCESS_DENIED:                    return "ACCESS_DENIED";
    case GVCP_STATUS_BUSY:                             return "BUSY";
    case GVCP_STATUS_LOCAL_PROBLEM:                    return "LOCAL_PROBLEM";
    case GVCP_STATUS_MSG_MISMATCH:                     return "MSG_MISMATCH";
    case GVCP_STATUS_INVALID_PROTOCOL:                 return "INVALID_PROTOCOL";
    case GVCP_STATUS_NO_MSG:                           return "NO_MSG";
    case GVCP_STATUS_PACKET_UNAVAILABLE:               return "PACKET_UNAVAILABLE";
    case GVCP_STATUS_DATA_OVERRUN:                     return "DATA_OVERRUN";
    case GVCP_STATUS_INVALID_HEADER:                   return "INVALID_HEADER";
    case GVCP_STATUS_WRONG_CONFIG:                     return "WRONG_CONFIG";
    case GVCP_STATUS_PACKET_NOT_YET_AVAILABLE:         return "PACKET_NOT_YET_AVAILABLE";
    case GVCP_STATUS_PACKET_AND_PREV_REMOVED_FROM_MEM: return "PACKET_AND_PREV_REMOVED_FROM_MEM";
    case GVCP_STATUS_PACKET_REMOVED_FROM_MEM:          return "PACKET_REMOVED_FROM_MEM";
    case GVCP_STATUS_NO_REF_TIME:                      return "NO_REF_TIME";
    case GVCP_STATUS_PACKET_TEMPORARILY_UNAVAILABLE:   return "PACKET_TEMPORARILY_UNAVAILABLE";
    case GVCP_STATUS_OVERFLOW:                         return "OVERFLOW";
    case GVCP_STATUS_ACTION_LATE:                      return "ACTION_LATE";
    case GVCP_STATUS_ERROR:                            return "ERROR";
    default:                                           return "UNKNOWN";
    }
}

// ── Constructor / Destructor ──────────────────────────────────────────────────
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

// ── discover: broadcast + subnet-directed broadcast ───────────────────────────
QList<GigECameraInfo> GigEDevice::discover(int timeoutMs)
{
    QList<GigECameraInfo> result;
    QMap<QString, bool>   seen;

    QByteArray pkt(sizeof(GvcpCmdHeader), 0);
    {
        auto* h     = reinterpret_cast<GvcpCmdHeader*>(pkt.data());
        h->key_code = GVCP_PACKET_TYPE_CMD;
        h->flags    = GVCP_DISCOVERY_FLAG_ALLOW_BROADCAST_ACK;
        h->command  = htons16(GVCP_CMD_DISCOVERY);
        h->length   = htons16(0);
        h->req_id   = htons16(1);
    }

    QUdpSocket sock;
    if (!sock.bind(QHostAddress(QHostAddress::AnyIPv4), 0,
                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning("GigEDevice::discover: bind failed: %s",
                 qPrintable(sock.errorString()));
        return result;
    }

    QSet<quint32> sentSet;
    auto sendTo = [&](const QHostAddress& dst) {
        quint32 a = dst.toIPv4Address();
        if (sentSet.contains(a)) return;
        sentSet.insert(a);
        sock.writeDatagram(pkt, dst, GVCP_PORT);
    };

    sendTo(QHostAddress("255.255.255.255"));

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

    // also send unicast to known IPs
    for (const QHostAddress& ip : s_knownIps)
        sendTo(ip);

    qDebug("GigEDevice::discover: sent to %d targets", (int)sentSet.size());

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!sock.waitForReadyRead(50)) continue;
        while (sock.hasPendingDatagrams()) {
            const qint64 psz = sock.pendingDatagramSize();
            if (psz <= 0) { sock.readDatagram(nullptr, 0); continue; }
            QByteArray ack(static_cast<int>(psz), 0);
            QHostAddress sender; quint16 sport{};
            sock.readDatagram(ack.data(), ack.size(), &sender, &sport);

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
    qDebug("GigEDevice::discover: total %d camera(s)", result.size());
    return result;
}

// ── discoverUnicast ───────────────────────────────────────────────────────────
QList<GigECameraInfo> GigEDevice::discoverUnicast(
    const QList<QHostAddress>& knownIps, int timeoutMs)
{
    QList<GigECameraInfo> result;
    if (knownIps.isEmpty()) return result;

    QByteArray pkt(sizeof(GvcpCmdHeader), 0);
    {
        auto* h     = reinterpret_cast<GvcpCmdHeader*>(pkt.data());
        h->key_code = GVCP_PACKET_TYPE_CMD;
        h->flags    = GVCP_CMD_FLAG_NONE;
        h->command  = htons16(GVCP_CMD_DISCOVERY);
        h->length   = htons16(0);
        h->req_id   = htons16(1);
    }

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

    for (const QHostAddress& camIp : knownIps) {
        const quint32 camAddr = camIp.toIPv4Address();
        bool sent = false;
        for (auto& s : sockets) {
            if ((camAddr & s->netmask) ==
                (s->ifaceIp.toIPv4Address() & s->netmask)) {
                s->sock.writeDatagram(pkt, camIp, GVCP_PORT);
                sent = true; break;
            }
        }
        if (!sent)
            sockets[0]->sock.writeDatagram(pkt, camIp, GVCP_PORT);
    }

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

                if (ack.size() < int(sizeof(GvcpAckHeader) + GVBS_DISCOVERY_DATA_SIZE))
                    continue;
                const auto* ah = reinterpret_cast<const GvcpAckHeader*>(ack.constData());
                if (ntohs16(ah->status)  != GVCP_STATUS_SUCCESS) continue;
                if (ntohs16(ah->command) != GVCP_ACK_DISCOVERY)  continue;

                const uint8_t* data = reinterpret_cast<const uint8_t*>(
                    ack.constData() + sizeof(GvcpAckHeader));
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

// ── forceIp (GV 2.2 §14.3.2) ─────────────────────────────────────────────────
bool GigEDevice::forceIp(const uint8_t mac[6],
                          const QHostAddress& newIp,
                          const QHostAddress& subnet,
                          const QHostAddress& gateway)
{
    QByteArray pkt(int(sizeof(GvcpForceIpCmd)), 0);
    auto* c = reinterpret_cast<GvcpForceIpCmd*>(pkt.data());
    c->header.key_code     = GVCP_PACKET_TYPE_CMD;
    c->header.flags        = GVCP_CMD_FLAG_ACK_REQUIRED;
    c->header.command      = htons16(GVCP_CMD_FORCEIP);
    c->header.length       = htons16(uint16_t(sizeof(GvcpForceIpCmd) - sizeof(GvcpCmdHeader)));
    c->header.req_id       = htons16(1);
    c->reserved0           = 0;
    std::memcpy(c->mac, mac, 6);
    c->reserved1           = 0;
    c->static_ip           = htonl32(newIp.toIPv4Address());
    c->reserved2           = 0;
    c->static_subnet       = htonl32(subnet.toIPv4Address());
    c->reserved3           = 0;
    c->static_gw           = htonl32(gateway.toIPv4Address());

    QUdpSocket sock;
    if (!sock.bind(QHostAddress(QHostAddress::AnyIPv4), 0,
                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
        return false;

    sock.writeDatagram(pkt, QHostAddress("255.255.255.255"), GVCP_PORT);

    // ACK is optional for ForceIP — wait briefly
    QByteArray ack;
    if (sock.waitForReadyRead(500) && sock.hasPendingDatagrams()) {
        ack.resize(int(sock.pendingDatagramSize()));
        sock.readDatagram(ack.data(), ack.size());
        if (ack.size() >= int(sizeof(GvcpAckHeader))) {
            const auto* ah = reinterpret_cast<const GvcpAckHeader*>(ack.constData());
            qDebug("GigEDevice::forceIp: ACK status=0x%04X (%s)",
                   ntohs16(ah->status), gvcpStatusString(ntohs16(ah->status)));
        }
    }

    qDebug("GigEDevice::forceIp: sent to %s → %s",
           newIp.toString().toUtf8().constData(),
           newIp.toString().toUtf8().constData());
    return true;
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

    // Verify communication via Version register (no CCP required)
    uint32_t version{};
    if (!readRegister(GVBS_VERSION_OFFSET, version)) {
        qWarning("GigEDevice::open: GVCP communication failed "
                 "(check firewall/route/IP)");
        m_socket.close();
        return false;
    }
    qDebug("GigEDevice::open: GigE Vision version %u.%u",
           (version >> 16) & 0xFFFF, version & 0xFFFF);

    // Read GVCP capability register and cache all flags
    if (readRegister(GVBS_GVCP_CAPABILITY_OFFSET, m_gvcpCapability)) {
        m_pendingAckSupported      = (m_gvcpCapability & GVBS_GVCP_CAP_PENDING_ACK)       != 0;
        m_readRegMultipleSupported = (m_gvcpCapability & GVBS_GVCP_CAP_READREG_MULTIPLE)  != 0;
        m_writeRegMultipleSupported= (m_gvcpCapability & GVBS_GVCP_CAP_WRITEREG_MULTIPLE) != 0;
        m_packetResendSupported    = (m_gvcpCapability & GVBS_GVCP_CAP_PACKET_RESEND)     != 0;
        m_extStatusCodesSupported  = (m_gvcpCapability & GVBS_GVCP_CAP_EXT_STATUS_CODES)  != 0;
        qDebug("GigEDevice::open: capability=0x%08X "
               "(pendingAck=%d readMult=%d writeMult=%d resend=%d)",
               m_gvcpCapability,
               m_pendingAckSupported, m_readRegMultipleSupported,
               m_writeRegMultipleSupported, m_packetResendSupported);
    }

    // Acquire CCP (Control Channel Privilege)
    uint32_t currentCcp{};
    readRegister(GVBS_CCP_OFFSET, currentCcp);

    bool ccpOk = writeRegister(GVBS_CCP_OFFSET, GVBS_CCP_CONTROL_ACCESS);
    if (!ccpOk) {
        qDebug("GigEDevice::open: Control access failed, trying Exclusive");
        ccpOk = writeRegister(GVBS_CCP_OFFSET, GVBS_CCP_EXCLUSIVE_ACCESS);
    }
    if (!ccpOk && currentCcp != 0) {
        qWarning("GigEDevice::open: CCP busy (0x%08X), waiting 3.5s...", currentCcp);
        QThread::msleep(3500);
        ccpOk = writeRegister(GVBS_CCP_OFFSET, GVBS_CCP_CONTROL_ACCESS);
    }
    if (!ccpOk) {
        qWarning("GigEDevice::open: CCP acquisition failed");
        m_socket.close();
        return false;
    }

    // If camera supports heartbeat disable via GVCP_CONFIGURATION, keep heartbeat enabled
    // (our software sends regular CCP reads as keep-alive, which is compliant)

    // Read camera identity fields
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
    writeRegister(GVBS_CCP_OFFSET, 0x00000000);
    m_socket.close();
    m_open = false;
    qDebug("GigEDevice::close: disconnected");
}

// ── sendCmd: transmit GVCP command and receive ACK with retry ─────────────────
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

                const uint16_t ackCmd = ntohs16(ah->command);

                // Pending ACK: camera is still processing, extend timeout
                if (ackCmd == GVCP_ACK_PENDING) {
                    if (buf.size() >= int(sizeof(GvcpPendingAck))) {
                        const auto* pa =
                            reinterpret_cast<const GvcpPendingAck*>(buf.constData());
                        remain += ntohs16(pa->timeout_ms);
                        qDebug("GigEDevice::sendCmd: PENDING_ACK, extending by %u ms",
                               ntohs16(pa->timeout_ms));
                    }
                    continue;
                }

                if (ackCmd != expectedAck) continue;

                // Validate req_id (0 = broadcast ACK from some cameras)
                const auto* ch =
                    reinterpret_cast<const GvcpCmdHeader*>(cmd.constData());
                const uint16_t ackId = ntohs16(ah->req_id);
                const uint16_t cmdId = ntohs16(ch->req_id);
                if (ackId != 0 && ackId != cmdId) continue;

                // Check status
                const uint16_t st = ntohs16(ah->status);
                if (st != GVCP_STATUS_SUCCESS) {
                    qWarning("GigEDevice::sendCmd: ACK error "
                             "cmd=0x%04X status=0x%04X (%s)",
                             ackCmd, st, gvcpStatusString(st));
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
    auto* c = reinterpret_cast<GvcpReadRegCmd*>(pkt.data());
    c->header.key_code = GVCP_PACKET_TYPE_CMD;
    c->header.flags    = GVCP_CMD_FLAG_ACK_REQUIRED;
    c->header.command  = htons16(GVCP_CMD_READ_REGISTER);
    c->header.length   = htons16(4);
    c->header.req_id   = htons16(nextReqId());
    c->address         = htonl32(address);

    QByteArray ack;
    if (!sendCmd(pkt, ack, GVCP_ACK_READ_REGISTER)) return false;
    if (ack.size() < int(sizeof(GvcpReadRegAck))) return false;

    const auto* a = reinterpret_cast<const GvcpReadRegAck*>(ack.constData());
    value = ntohl32(a->value);
    return true;
}

// ── writeRegister ─────────────────────────────────────────────────────────────
bool GigEDevice::writeRegister(uint32_t address, uint32_t value)
{
    QByteArray pkt(sizeof(GvcpWriteRegCmd), 0);
    auto* c = reinterpret_cast<GvcpWriteRegCmd*>(pkt.data());
    c->header.key_code = GVCP_PACKET_TYPE_CMD;
    c->header.flags    = GVCP_CMD_FLAG_ACK_REQUIRED;
    c->header.command  = htons16(GVCP_CMD_WRITE_REGISTER);
    c->header.length   = htons16(8);
    c->header.req_id   = htons16(nextReqId());
    c->address         = htonl32(address);
    c->value           = htonl32(value);

    QByteArray ack;
    return sendCmd(pkt, ack, GVCP_ACK_WRITE_REGISTER);
}

// ── readRegisters: multiple registers in one packet (GV 2.2 §14.3.4) ─────────
bool GigEDevice::readRegisters(const std::vector<uint32_t>& addrs,
                                std::vector<uint32_t>& values)
{
    if (addrs.empty()) return true;

    // Fall back to single-register reads if camera doesn't support multiple
    if (!m_readRegMultipleSupported || addrs.size() == 1) {
        values.resize(addrs.size());
        for (size_t i = 0; i < addrs.size(); ++i)
            if (!readRegister(addrs[i], values[i])) return false;
        return true;
    }

    const uint16_t payloadLen = uint16_t(addrs.size() * 4);
    QByteArray pkt(int(sizeof(GvcpCmdHeader)) + payloadLen, 0);
    auto* h = reinterpret_cast<GvcpCmdHeader*>(pkt.data());
    h->key_code = GVCP_PACKET_TYPE_CMD;
    h->flags    = GVCP_CMD_FLAG_ACK_REQUIRED;
    h->command  = htons16(GVCP_CMD_READ_REGISTER);
    h->length   = htons16(payloadLen);
    h->req_id   = htons16(nextReqId());

    uint8_t* p = reinterpret_cast<uint8_t*>(pkt.data()) + sizeof(GvcpCmdHeader);
    for (uint32_t addr : addrs) {
        *reinterpret_cast<uint32_t*>(p) = htonl32(addr);
        p += 4;
    }

    QByteArray ack;
    if (!sendCmd(pkt, ack, GVCP_ACK_READ_REGISTER)) return false;

    const int minAckSize = int(sizeof(GvcpAckHeader)) + int(addrs.size() * 4);
    if (ack.size() < minAckSize) return false;

    values.resize(addrs.size());
    const uint8_t* ap =
        reinterpret_cast<const uint8_t*>(ack.constData()) + sizeof(GvcpAckHeader);
    for (size_t i = 0; i < addrs.size(); ++i) {
        values[i] = ntohl32(*reinterpret_cast<const uint32_t*>(ap));
        ap += 4;
    }
    return true;
}

// ── writeRegisters: multiple registers in one packet (GV 2.2 §14.3.5) ────────
bool GigEDevice::writeRegisters(
    const std::vector<std::pair<uint32_t, uint32_t>>& regs)
{
    if (regs.empty()) return true;

    // Fall back to single writes if camera doesn't support multiple
    if (!m_writeRegMultipleSupported || regs.size() == 1)
    {
        for (auto& [addr, val] : regs)
            if (!writeRegister(addr, val)) return false;
        return true;
    }

    const uint16_t payloadLen = uint16_t(regs.size() * 8);
    QByteArray pkt(int(sizeof(GvcpCmdHeader)) + payloadLen, 0);
    auto* h = reinterpret_cast<GvcpCmdHeader*>(pkt.data());
    h->key_code = GVCP_PACKET_TYPE_CMD;
    h->flags    = GVCP_CMD_FLAG_ACK_REQUIRED;
    h->command  = htons16(GVCP_CMD_WRITE_REGISTER);
    h->length   = htons16(payloadLen);
    h->req_id   = htons16(nextReqId());

    uint8_t* p = reinterpret_cast<uint8_t*>(pkt.data()) + sizeof(GvcpCmdHeader);
    for (auto& [addr, val] : regs) {
        *reinterpret_cast<uint32_t*>(p)   = htonl32(addr);
        *reinterpret_cast<uint32_t*>(p+4) = htonl32(val);
        p += 8;
    }

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
        auto* c = reinterpret_cast<GvcpReadMemCmd*>(pkt.data());
        c->header.key_code = GVCP_PACKET_TYPE_CMD;
        c->header.flags    = GVCP_CMD_FLAG_ACK_REQUIRED;
        c->header.command  = htons16(GVCP_CMD_READ_MEMORY);
        c->header.length   = htons16(8);
        c->header.req_id   = htons16(nextReqId());
        c->address         = htonl32(address + offset);
        c->reserved        = 0;
        c->count           = htons16(static_cast<uint16_t>(chunk));

        QByteArray ack;
        if (!sendCmd(pkt, ack, GVCP_ACK_READ_MEMORY)) return false;

        const int dataOffset = int(sizeof(GvcpReadMemAck));
        if (ack.size() < dataOffset + int(chunk)) return false;

        std::memcpy(data + offset, ack.constData() + dataOffset, chunk);
        offset += chunk;
    }
    return true;
}

// ── writeMemory ───────────────────────────────────────────────────────────────
bool GigEDevice::writeMemory(uint32_t address, const uint8_t* data, uint32_t size)
{
    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t chunk = std::min(size - offset,
                                        static_cast<uint32_t>(GVCP_MAX_DATA_SIZE));

        QByteArray pkt(int(sizeof(GvcpWriteMemCmd)) + int(chunk), 0);
        auto* c = reinterpret_cast<GvcpWriteMemCmd*>(pkt.data());
        c->header.key_code = GVCP_PACKET_TYPE_CMD;
        c->header.flags    = GVCP_CMD_FLAG_ACK_REQUIRED;
        c->header.command  = htons16(GVCP_CMD_WRITE_MEMORY);
        c->header.length   = htons16(static_cast<uint16_t>(4 + chunk));
        c->header.req_id   = htons16(nextReqId());
        c->address         = htonl32(address + offset);
        std::memcpy(pkt.data() + sizeof(GvcpWriteMemCmd), data + offset, chunk);

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
    // GV 2.2 §16.3 — SCDA may only be written when SCP destination port = 0
    qDebug("GigEDevice::setStreamDestination: ip=%s port=%u",
           qPrintable(hostIp.toString()), hostPort);

    writeRegister(GVBS_SC0_PORT_OFFSET, 0);  // clear port first

    if (!writeRegister(GVBS_SC0_IP_ADDRESS_OFFSET, hostIp.toIPv4Address())) {
        qWarning("GigEDevice::setStreamDestination: SCDA write failed");
        return false;
    }
    if (!writeRegister(GVBS_SC0_PORT_OFFSET, static_cast<uint32_t>(hostPort))) {
        qWarning("GigEDevice::setStreamDestination: SCP port write failed");
        return false;
    }

    qDebug("GigEDevice::setStreamDestination: OK");
    return true;
}

// ── setStreamPacketSize ───────────────────────────────────────────────────────
bool GigEDevice::setStreamPacketSize(uint16_t packetSize)
{
    // SCPS: bit 14 = DoNotFragment, bits 15:0 = packet size
    const uint32_t flags = 0x4000u;
    return writeRegister(GVBS_SC0_PACKET_SIZE_OFFSET,
                         flags | static_cast<uint32_t>(packetSize));
}

// ── negotiatePacketSize ───────────────────────────────────────────────────────
uint16_t GigEDevice::negotiatePacketSize(uint16_t desired)
{
    // Use a conservative default that works through most switches.
    // Jumbo frames (9000) require switch support for 9000-byte MTU.
    (void)desired;
    const uint16_t safe = 1472;
    qDebug("GigEDevice::negotiatePacketSize: setting %u", safe);
    setStreamPacketSize(safe);
    return safe;
}

// ── sendPacketResend (GV 2.2 §14.3.7) ────────────────────────────────────────
bool GigEDevice::sendPacketResend(uint16_t streamChannel,
                                   uint64_t blockId,
                                   uint32_t firstPacketId,
                                   uint32_t lastPacketId,
                                   bool extendedId)
{
    if (!m_packetResendSupported) return false;

    QByteArray pkt;
    if (extendedId) {
        pkt.resize(int(sizeof(GvcpPacketResendExtCmd)));
        auto* c = reinterpret_cast<GvcpPacketResendExtCmd*>(pkt.data());
        c->header.key_code    = GVCP_PACKET_TYPE_CMD;
        c->header.flags       = GVCP_CMD_FLAG_NONE;  // no ACK
        c->header.command     = htons16(GVCP_CMD_PACKETRESEND);
        c->header.length      = htons16(20);
        c->header.req_id      = htons16(nextReqId());
        c->stream_channel     = htons16(streamChannel);
        c->reserved           = 0;
        c->block_id_high      = htonl32(static_cast<uint32_t>(blockId >> 32));
        c->block_id_low       = htonl32(static_cast<uint32_t>(blockId & 0xFFFFFFFFu));
        c->first_packet_id    = htonl32(firstPacketId);
        c->last_packet_id     = htonl32(lastPacketId);
    } else {
        pkt.resize(int(sizeof(GvcpPacketResendCmd)));
        auto* c = reinterpret_cast<GvcpPacketResendCmd*>(pkt.data());
        c->header.key_code    = GVCP_PACKET_TYPE_CMD;
        c->header.flags       = GVCP_CMD_FLAG_NONE;
        c->header.command     = htons16(GVCP_CMD_PACKETRESEND);
        c->header.length      = htons16(12);
        c->header.req_id      = htons16(nextReqId());
        c->stream_channel     = htons16(streamChannel);
        c->block_id           = htons16(static_cast<uint16_t>(blockId & 0xFFFFu));
        c->first_packet_id    = htonl32(firstPacketId);
        c->last_packet_id     = htonl32(lastPacketId);
    }

    // PacketResend has no ACK — fire-and-forget
    m_socket.writeDatagram(pkt, m_cameraIp, GVCP_PORT);
    return true;
}

// ── loadGenApiXml ─────────────────────────────────────────────────────────────
QByteArray GigEDevice::loadGenApiXml()
{
    QByteArray urlBuf(GVBS_XML_URL_SIZE, 0);
    if (!readMemory(GVBS_FIRST_URL_OFFSET,
                    reinterpret_cast<uint8_t*>(urlBuf.data()),
                    GVBS_XML_URL_SIZE))
        return {};

    const QString url = QString::fromLatin1(
        urlBuf.constData(),
        qstrnlen(urlBuf.constData(), GVBS_XML_URL_SIZE)).trimmed();
    qDebug("GigEDevice::loadGenApiXml: URL = [%s]", qPrintable(url));

    // GV 2.2 §10.2.3: "Local:filename;address;size" — address and size are hex
    if (url.startsWith("Local:", Qt::CaseInsensitive)) {
        const QString body = url.mid(6);
        const QStringList parts = body.split(';');

        if (parts.size() >= 3) {
            bool ok1 = false, ok2 = false;
            QString addrStr = parts[1].trimmed();
            QString sizeStr = parts[2].trimmed();

            // Address/size may or may not have a 0x prefix
            const int base = (addrStr.startsWith("0x", Qt::CaseInsensitive)) ? 0 : 16;
            const uint32_t addr = addrStr.toUInt(&ok1, base);
            const uint32_t sz   = sizeStr.toUInt(&ok2, base);

            qDebug("GigEDevice::loadGenApiXml: addr=0x%08X size=0x%X", addr, sz);

            if (ok1 && ok2 && sz > 0 && sz < 8 * 1024 * 1024) {
                QByteArray xmlData(int(sz), 0);
                if (readMemory(addr,
                               reinterpret_cast<uint8_t*>(xmlData.data()), sz)) {
                    // ZIP magic: PK\x03\x04
                    if (xmlData.size() >= 4 &&
                        uint8_t(xmlData[0]) == 0x50 &&
                        uint8_t(xmlData[1]) == 0x4B) {
                        qDebug("GigEDevice::loadGenApiXml: decompressing ZIP");
                        return ArvGenApiXml::decompressZip(xmlData);
                    }
                    return xmlData;
                }
                qWarning("GigEDevice::loadGenApiXml: readMemory failed "
                         "addr=0x%08X size=%u", addr, sz);
            } else {
                qWarning("GigEDevice::loadGenApiXml: parse failed "
                         "ok1=%d ok2=%d addr=0x%08X sz=%u",
                         ok1, ok2, addr, sz);
            }
        }
    }

    qWarning("GigEDevice::loadGenApiXml: unsupported URL [%s]", qPrintable(url));
    return {};
}

// ── Heartbeat ─────────────────────────────────────────────────────────────────
void GigEDevice::onHeartbeat()
{
    // GV 2.2 §14.2: host must send a GVCP command within the heartbeat timeout
    // to maintain the control channel. Reading CCP is the minimal overhead option.
    uint32_t val{};
    if (!readRegister(GVBS_CCP_OFFSET, val))
        qWarning("GigEDevice: heartbeat failed — camera may have disconnected");
}
