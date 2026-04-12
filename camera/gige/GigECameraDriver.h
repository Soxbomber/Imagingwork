#pragma once
// ============================================================
// GigECameraDriver
// GigE Vision ICameraDriver 구현체
//
// USB3 Vision(ArvCameraDriver)과 동일한 ICameraDriver 인터페이스
// 내부 프로토콜: GVCP(제어) + GVSP(스트림)
//
// 사전 조건:
//   - 카메라와 같은 서브넷에 연결된 네트워크 인터페이스 필요
//   - 방화벽: UDP 포트 3956(GVCP), 스트림 포트 개방
//   - Jumbo frame 권장 (MTU 9000)
// ============================================================

#include "ICameraDriver.h"
#include "GigEDevice.h"
#include "GigEStream.h"
#include "../genicam/ArvGenApiXml.h"
#include <QThread>
#include <memory>
#include <vector>

struct GigECameraCtx {
    GigECameraInfo   info;
    DeviceInfo       deviceinfo;
    ImageViewerDock* dock{};

    // 제어 채널 (GVCP)
    std::unique_ptr<GigEDevice> device;

    // 스트림 채널 (GVSP)
    std::unique_ptr<GigEStream> stream;
    QThread                     streamThread;

    // GenApi (AcquisitionStart/Stop 주소)
    ArvCommandNode acquisitionStart;
    ArvCommandNode acquisitionStop;
};

class GigECameraDriver : public ICameraDriver
{
    Q_OBJECT
public:
    explicit GigECameraDriver(QObject* parent = nullptr);
    ~GigECameraDriver() override;

    QList<DeviceInfo> EnumCameras()                                    override;
    bool StartGrabbing(const DeviceInfo& di, ImageViewerDock* dock)    override;
    void StopGrabbing(const QString& serialnumber)                      override;
    void StopAll()                                                     override;

private:
    void stopCtx(GigECameraCtx& ctx);

    // 호스트 IP 자동 탐색 (카메라와 같은 서브넷)
    static QHostAddress findLocalIp(const QHostAddress& cameraIp);

    std::vector<std::unique_ptr<GigECameraCtx>> m_cameras;
};
