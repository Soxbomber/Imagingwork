#pragma once
// ============================================================
// GenApiController.h
// GenICam 파라미터 읽기/쓰기 컨트롤러
//
// 사용 예:
//   GenApiController ctrl(device, genApiInfo);
//
//   // 해상도 변경
//   ctrl.setInteger("Width",  1920);
//   ctrl.setInteger("Height", 1080);
//
//   // ExposureTime (μs)
//   ctrl.setFloat("ExposureTime", 5000.0); // 5ms
//
//   // Gain
//   ctrl.setFloat("Gain", 2.0); // 2dB
//
//   // FrameRate
//   ctrl.setFloat("AcquisitionFrameRate", 30.0);
//
//   // Enumeration
//   ctrl.setEnum("AcquisitionMode", "Continuous");
//
// USB3 Vision (ArvU3vDevice) 와 GigE Vision (GigEDevice) 모두 지원:
//   공통 인터페이스(IRegisterDevice)를 통해 의존성 분리
// ============================================================

#include "ArvGenApiXml.h"
#include <QString>
#include <QVariant>
#include <functional>
#include <cstdint>

// ── 레지스터 장치 공통 인터페이스 ─────────────────────────────────────────────
// U3V / GigE 양쪽에서 구현
class IRegisterDevice {
public:
    virtual ~IRegisterDevice() = default;
    virtual bool readRegister (uint32_t addr, uint32_t& val)        = 0;
    virtual bool writeRegister(uint32_t addr, uint32_t  val)        = 0;
    virtual bool readMemory   (uint32_t addr, uint8_t* buf, uint32_t sz) = 0;
    virtual bool writeMemory  (uint32_t addr, const uint8_t* buf, uint32_t sz) = 0;
};

// ── GenApiController ──────────────────────────────────────────────────────────
class GenApiController
{
public:
    explicit GenApiController(IRegisterDevice* dev,
                              const ArvGenApiInfo& info);

    // ── Integer 파라미터 ──────────────────────────────────────────────────────
    // Width, Height, OffsetX, OffsetY, BinningH, BinningV 등
    bool getInteger(const QString& name, int64_t& value) const;
    bool setInteger(const QString& name, int64_t  value);

    // ── Float 파라미터 ────────────────────────────────────────────────────────
    // ExposureTime(μs), Gain(dB), AcquisitionFrameRate(Hz)
    bool getFloat(const QString& name, double& value) const;
    bool setFloat(const QString& name, double  value);

    // ── Enumeration 파라미터 ─────────────────────────────────────────────────
    // AcquisitionMode("Continuous","SingleFrame"), PixelFormat, TriggerMode
    bool getEnum(const QString& name, QString& entry) const;
    bool setEnum(const QString& name, const QString& entry);

    // ── Command 실행 ─────────────────────────────────────────────────────────
    bool execute(const QString& name);

    // ── 범위 조회 ─────────────────────────────────────────────────────────────
    bool getRange(const QString& name,
                  double& minVal, double& maxVal, double& step) const;

    // ── Enumeration 항목 목록 조회 ────────────────────────────────────────────
    QStringList enumEntries(const QString& name) const;

    // ── 노드 존재 여부 ────────────────────────────────────────────────────────
    bool hasNode(const QString& name) const;
    GenApiNodeType nodeType(const QString& name) const;

    // ── 편의: 카메라 주요 파라미터 일괄 조회 ──────────────────────────────────
    struct CameraParams {
        int64_t width{};
        int64_t height{};
        int64_t offsetX{};
        int64_t offsetY{};
        double  exposureTime{};   // μs
        double  gain{};           // dB
        double  frameRate{};      // Hz
        QString acquisitionMode;
        QString pixelFormat;
        bool    valid{false};
    };
    CameraParams readAll() const;

private:
    // 레지스터 read/write (빅/리틀엔디안 처리 포함)
    bool regRead32 (const GenApiNode& node, int64_t& val) const;
    bool regWrite32(const GenApiNode& node, int64_t  val);
    bool regRead64 (const GenApiNode& node, int64_t& val) const;
    bool regWrite64(const GenApiNode& node, int64_t  val);
    bool regReadFloat (const GenApiNode& node, double& val) const;
    bool regWriteFloat(const GenApiNode& node, double  val);

    IRegisterDevice*     m_dev;
    const ArvGenApiInfo& m_info;
};
