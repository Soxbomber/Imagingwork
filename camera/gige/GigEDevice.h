#pragma once
// ============================================================
// GigEDevice.h
// GigE Vision GVCP 제어 채널 (Aravis arvgvdevice.c 포팅)
//
// GVCP over UDP (포트 3956):
//   readRegister / writeRegister / readMemory / writeMemory
//   Discovery (broadcast UDP)
//   Heartbeat (1초 주기 keep-alive)
// ============================================================

#include "GigEProtocol.h"
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <cstdint>

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

    // ── 열거 ─────────────────────────────────────────────────────────────────
    // 네트워크에 있는 GigE Vision 카메라를 브로드캐스트로 탐색
    static QList<GigECameraInfo> discover(int timeoutMs = 1000);

    // ── 연결 ─────────────────────────────────────────────────────────────────
    bool open(const QHostAddress& cameraIp);
    void close();
    bool isOpen() const { return m_open; }

    // ── 레지스터 읽기/쓰기 (GVCP ReadReg / WriteReg) ─────────────────────────
    bool readRegister (uint32_t address, uint32_t& value);
    bool writeRegister(uint32_t address, uint32_t  value);

    // ── 메모리 읽기/쓰기 (GVCP ReadMem / WriteMem) ───────────────────────────
    // 최대 GVCP_MAX_DATA_SIZE(512) bytes 단위로 자동 분할
    bool readMemory (uint32_t address, uint8_t* data, uint32_t size);
    bool writeMemory(uint32_t address, const uint8_t* data, uint32_t size);

    // ── 스트림 설정 ───────────────────────────────────────────────────────────
    bool setStreamDestination(const QHostAddress& hostIp, uint16_t hostPort);
    bool setStreamPacketSize(uint16_t packetSize);
    uint16_t negotiatePacketSize(uint16_t desired = 9000); // Jumbo frame

    // ── GenApi XML 획득 ───────────────────────────────────────────────────────
    // XML URL 레지스터에서 URL 읽기 → HTTP/local 스킴 처리
    QByteArray loadGenApiXml();

    const GigECameraInfo& cameraInfo() const { return m_info; }
    QHostAddress ip() const { return m_cameraIp; }

private slots:
    void onHeartbeat();

private:
    bool sendCmd(const QByteArray& cmd, QByteArray& ack,
                 uint16_t expectedAck, int timeoutMs = GVCP_TIMEOUT_MS);
    uint16_t nextReqId() { return ++m_reqId; }

    // 바이트 순서 변환 (GVCP는 big-endian)
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
    bool          m_pendingAckSupported{false};
};
