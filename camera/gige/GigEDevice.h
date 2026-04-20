#pragma once
// ============================================================
// GigEDevice.h — GigE Vision 2.2 GVCP control channel
//
// GVCP over UDP (port 3956):
//   readRegister / writeRegister  (single and multiple)
//   readMemory / writeMemory
//   Discovery (broadcast + unicast)
//   Heartbeat (1-second keep-alive)
//   PacketResend (standard and extended 64-bit block ID)
//   ForceIP (broadcast IP reassignment)
// ============================================================

#include "GigEProtocol.h"
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <cstdint>
#include <utility>
#include <vector>

struct GigECameraInfo {
    QHostAddress ipAddress;
    QString      manufacturerName;
    QString      modelName;
    QString      serialNumber;
    QString      macAddress;
    uint32_t     subnetMask{};
    bool         valid{false};
};

class GigEDevice : public QObject
{
    Q_OBJECT
public:
    explicit GigEDevice(QObject* parent = nullptr);
    ~GigEDevice() override;

    // ── Discovery ─────────────────────────────────────────────────────────────
    static QList<GigECameraInfo> discover(int timeoutMs = 1000);
    static QList<GigECameraInfo> discoverUnicast(
        const QList<QHostAddress>& knownIps, int timeoutMs = 1000);
    static void addKnownIp(const QHostAddress& ip);
    static void clearKnownIps();

    // ── ForceIP (GV 2.2 §14.3.2) ─────────────────────────────────────────────
    // Broadcast packet to reassign a camera's IP (camera identified by MAC).
    // mac: 6-byte MAC address (big-endian byte order).
    static bool forceIp(const uint8_t mac[6],
                        const QHostAddress& newIp,
                        const QHostAddress& subnet,
                        const QHostAddress& gateway);

    // ── Connection ────────────────────────────────────────────────────────────
    bool open(const QHostAddress& cameraIp);
    void close();
    bool isOpen() const { return m_open; }

    // ── Single register access ────────────────────────────────────────────────
    bool readRegister (uint32_t address, uint32_t& value);
    bool writeRegister(uint32_t address, uint32_t  value);

    // ── Multiple register access (GV 2.2 §14.3.4 / §14.3.5) ─────────────────
    // Falls back to individual accesses if camera doesn't advertise capability.
    bool readRegisters (const std::vector<uint32_t>& addrs,
                        std::vector<uint32_t>& values);
    bool writeRegisters(const std::vector<std::pair<uint32_t, uint32_t>>& regs);

    // ── Memory access ─────────────────────────────────────────────────────────
    bool readMemory (uint32_t address, uint8_t* data, uint32_t size);
    bool writeMemory(uint32_t address, const uint8_t* data, uint32_t size);

    // ── Stream configuration ──────────────────────────────────────────────────
    bool setStreamDestination(const QHostAddress& hostIp, uint16_t hostPort);
    bool setStreamPacketSize(uint16_t packetSize);
    uint16_t negotiatePacketSize(uint16_t desired = 9000);

    // ── Packet resend (GV 2.2 §14.3.7) ───────────────────────────────────────
    // streamChannel: index of stream channel (usually 0).
    // extendedId: true for 64-bit block IDs (GV 2.0+ ext mode).
    bool sendPacketResend(uint16_t streamChannel,
                          uint64_t blockId,
                          uint32_t firstPacketId,
                          uint32_t lastPacketId,
                          bool extendedId = false);

    // ── GenApi XML ────────────────────────────────────────────────────────────
    QByteArray loadGenApiXml();

    const GigECameraInfo& cameraInfo() const { return m_info; }
    QHostAddress ip() const { return m_cameraIp; }
    uint32_t gvcpCapability() const { return m_gvcpCapability; }

private slots:
    void onHeartbeat();

private:
    bool sendCmd(const QByteArray& cmd, QByteArray& ack,
                 uint16_t expectedAck, int timeoutMs = GVCP_TIMEOUT_MS);
    uint16_t nextReqId() { return ++m_reqId; }

    static const char* gvcpStatusString(uint16_t status);

    static uint16_t htons16(uint16_t v);
    static uint32_t htonl32(uint32_t v);
    static uint16_t ntohs16(uint16_t v);
    static uint32_t ntohl32(uint32_t v);

    QUdpSocket    m_socket;
    QHostAddress  m_cameraIp;
    QTimer        m_heartbeatTimer;
    GigECameraInfo m_info;
    uint16_t      m_reqId{0};
    bool          m_open{false};

    // Capability flags set from GVCP_CAPABILITY register on open()
    uint32_t m_gvcpCapability{0};
    bool     m_pendingAckSupported{false};
    bool     m_readRegMultipleSupported{false};
    bool     m_writeRegMultipleSupported{false};
    bool     m_packetResendSupported{false};
    bool     m_extStatusCodesSupported{false};
};
