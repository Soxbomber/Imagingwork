#pragma once
// ============================================================
// ArvCameraDriver
// Aravis 방식 libusb USB3 Vision 드라이버의 ICameraDriver 구현체
//
// 사전 조건 (Windows):
//   Zadig -> 카메라 선택 -> WinUSB -> Replace Driver
//   vcpkg install libusb:x64-windows
// ============================================================

#include "ICameraDriver.h"
#include "ArvU3vDevice.h"
#include "ArvU3vStream.h"
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
};

class ArvCameraDriver : public ICameraDriver
{
    Q_OBJECT
public:
    explicit ArvCameraDriver(QObject* parent = nullptr);
    ~ArvCameraDriver() override;

    QList<DeviceInfo> EnumCameras()                          override;
    bool StartGrabbing(const DeviceInfo& di, ImageViewerDock* dock) override;
    void StopGrabbing(const QString& description)            override;
    void StopAll()                                           override;

private:
    void stopCtx(ArvCameraCtx& ctx);

    libusb_context* m_ctx{};
    std::vector<std::unique_ptr<ArvCameraCtx>> m_cameras;
};
