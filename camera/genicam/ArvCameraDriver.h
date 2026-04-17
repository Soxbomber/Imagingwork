#pragma once
// ============================================================
// ArvCameraDriver
// USB3 Vision ICameraDriver 구현체
// GenApiController를 통한 파라미터 읽기/쓰기 지원
// ============================================================

#include "ICameraDriver.h"
#include "ArvU3vDevice.h"
#include "ArvU3vStream.h"
#include "ArvGenApiXml.h"
#include "GenApiController.h"
#include <libusb.h>
#include <QThread>
#include <memory>
#include <vector>

struct ArvCameraCtx {
    ArvU3vDevice     device;
    ArvStreamParams  streamParams;
    uint8_t          busNumber{};
    uint8_t          deviceAddress{};
    ImageViewerDock* dock{};
    ArvU3vStream*    stream{};
    QThread          thread{};
    DeviceInfo       deviceinfo;

    // GenApi 파라미터 제어
    ArvGenApiInfo                        genApiInfo;
    std::unique_ptr<ArvU3vDeviceRegAdapter> regAdapter;
    std::unique_ptr<GenApiController>    controller;
};

class ArvCameraDriver : public ICameraDriver
{
    Q_OBJECT
public:
    explicit ArvCameraDriver(QObject* parent = nullptr);
    ~ArvCameraDriver() override;

    QList<DeviceInfo> EnumCameras()                                   override;
    bool StartGrabbing(const DeviceInfo& di, ImageViewerDock* dock)   override;
    void StopGrabbing(const QString& description)                     override;
    void StopAll()                                                    override;

    // ── GenApi 파라미터 제어 API ─────────────────────────────────────────────
    // description으로 카메라 식별

    // 해상도 변경 (Acquisition Stop → 변경 → Start 자동 처리)
    bool setResolution(const QString& description, int w, int h,
                       int offsetX = 0, int offsetY = 0);

    // ExposureTime (μs): 카메라 단위 자동 변환
    bool setExposureTime(const QString& description, double us);
    bool getExposureTime(const QString& description, double& us);

    // Gain (dB)
    bool setGain(const QString& description, double dB);
    bool getGain(const QString& description, double& dB);

    // AcquisitionFrameRate (Hz)
    bool setFrameRate(const QString& description, double fps);
    bool getFrameRate(const QString& description, double& fps);

    // Generic: Integer / Float / Enum
    bool setInteger(const QString& description, const QString& node, int64_t val);
    bool setFloat  (const QString& description, const QString& node, double val);
    bool setEnum   (const QString& description, const QString& node,
                    const QString& entry);



private:
    ArvCameraCtx* findCtx(const QString& description);
    void stopCtx(ArvCameraCtx& ctx);

    libusb_context* m_ctx{};
    std::vector<std::unique_ptr<ArvCameraCtx>> m_cameras;
};
