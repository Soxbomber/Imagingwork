#pragma once
// ============================================================
// ArvU3vDevice.h
// Aravis arvuvdevice.c 구현 방식을 C++/Qt로 포팅
//
// 핵심 차이점 (Aravis 소스 분석 결과):
//   - 인터페이스/엔드포인트를 config descriptor 스캔으로 자동 탐지
//     (하드코딩 EP 0x01/0x81/0x82 X)
//   - Device class 0xEF + Interface subclass 0x05 으로 USB3 Vision 탐지
//   - 스트림 시작 전 data endpoint를 halt/clear로 reset
//   - SIRM에 payload_size, count, transfer1_size 를 직접 계산해서 씀
// ============================================================

#include <libusb.h>
#include <QString>
#include <QList>
#include <QByteArray>
#include <vector>
#include <cstdint>
#include "ArvU3vProtocol.h"
#include "ArvGenApiXml.h"

struct ArvU3vDeviceInfo {
    uint8_t  busNumber{};
    uint8_t  deviceAddress{};
    QString  manufacturer;
    QString  modelName;
    QString  serialNumber;
};

// SIRM에 써야 할 계산된 스트림 파라미터
struct ArvStreamParams {
    uint64_t reqPayloadSize{};
    uint32_t payloadSize{};    // SIRM에 쓸 값 (transfer 단위)
    uint32_t payloadCount{};
    uint32_t transfer1Size{};  // 나머지 크기 (aligned)
    uint32_t leaderSize{};
    uint32_t trailerSize{};
    uint32_t maximumTransferSize{};
};

// IRegisterDevice 어댑터 (GenApiController용)
#include "GenApiController.h"

class ArvU3vDeviceRegAdapter : public IRegisterDevice {
public:
    explicit ArvU3vDeviceRegAdapter(class ArvU3vDevice* d) : m_dev(d) {}
    bool readRegister (uint32_t addr, uint32_t& val) override;
    bool writeRegister(uint32_t addr, uint32_t  val) override;
    bool readMemory   (uint32_t addr, uint8_t* buf, uint32_t sz) override;
    bool writeMemory  (uint32_t addr, const uint8_t* buf, uint32_t sz) override;
private:
    class ArvU3vDevice* m_dev;
};

class ArvU3vDevice
{
public:
    ArvU3vDevice();
    ~ArvU3vDevice();

    // USB3 Vision 카메라 열거 (device class 0xEF + interface subclass 0x05)
    static QList<ArvU3vDeviceInfo> enumerate(libusb_context* ctx);

    // 디바이스 열기 (인터페이스/엔드포인트 자동 탐지)
    bool open(libusb_context* ctx,
              uint8_t busNumber, uint8_t deviceAddress);
    void close();
    bool isOpen() const { return m_handle != nullptr; }

    // GenCP ReadMem / WriteMem
    bool readMemory (uint64_t address, void* data, uint32_t size, int retries = 5);
    bool writeMemory(uint64_t address, const void* data, uint32_t size, int retries = 5);

    bool readUInt32(uint64_t address, uint32_t& out);
    bool readUInt64(uint64_t address, uint64_t& out);
    bool writeUInt32(uint64_t address, uint32_t value);
    bool readString (uint64_t address, char* buf, size_t maxLen);

    // ABRM/SBRM/SIRM bootstrap (aravis arv_uv_device_bootstrap)
    bool bootstrap();

    // GenICam XML 다운로드 (manifest table 경유)
    QByteArray downloadXml();

    // 스트림 파라미터 계산 + SIRM 설정 + data endpoint reset + enable
    bool enableStream(ArvStreamParams& outParams);
    bool disableStream();

    // GenICam XML 다운로드 + 파싱
    // AcquisitionStart/Stop 레지스터 주소 확보
    bool loadGenApi();

    // AcquisitionStart / AcquisitionStop 실행
    // (GenApi XML에서 파싱된 주소와 값으로 writeMemory)
    bool executeAcquisitionStart();
    bool executeAcquisitionStop();

    // data endpoint bulk read
    bool bulkReadData(void* buf, int length, int& transferred,
                      unsigned int timeoutMs = 5000);

    // data endpoint 리셋 (aravis reset_endpoint 포팅)
    void resetDataEndpoint();

    QString modelName()    const { return m_modelName; }
    QString serialNumber() const { return m_serialNumber; }
    QString lastError()    const { return m_lastError; }

    uint32_t maxCmdTransfer() const { return m_maxCmdTransfer; }
    uint32_t maxAckTransfer() const { return m_maxAckTransfer; }

private:
    // GenCP bulk send/recv (aravis arv_uv_device_bulk_transfer)
    bool bulkTransfer(uint8_t endpoint, void* buf, int length,
                      int& transferred, unsigned int timeoutMs);

    // GenCP command engine
    bool sendCmdRecvAck(uint16_t cmdId,
                        const void* cmdPayload, uint16_t cmdPayloadLen,
                        void* ackPayload, uint16_t ackPayloadMaxLen,
                        uint16_t& ackPayloadLen);

    uint16_t nextId() {
        uint16_t id = m_requestId++;
        if (m_requestId == 0) m_requestId = 1;
        return id;
    }

    libusb_device_handle* m_handle{};

    // 자동 탐지된 인터페이스/엔드포인트 (Aravis: priv->control/data_interface/endpoint)
    int     m_controlInterface{0};
    int     m_dataInterface{1};
    uint8_t m_controlEpOut{0x01};
    uint8_t m_controlEpIn {0x81};
    uint8_t m_dataEp      {0x82};

    // SBRM 기반 정보
    uint64_t m_sbrmAddr{};
    uint64_t m_sirmAddr{};
    uint32_t m_maxCmdTransfer{512};
    uint32_t m_maxAckTransfer{512};

    QString m_modelName;
    QString m_serialNumber;
    QString m_lastError;
    uint16_t m_requestId{1};
    ArvGenApiInfo m_genApi;
};
