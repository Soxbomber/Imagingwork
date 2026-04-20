#pragma once

#include <QObject>
#include <QList>
#include <optional>
#include "deviceinfo.h"
#include "ImageViewerDock.h"

// Pre-acquisition NodeMap parameters applied before AcquisitionStart.
// Use std::nullopt to leave a parameter at the camera's current/default value.
struct NodeMapInitParams {
    std::optional<double> exposureTime_us;  // ExposureTime in microseconds
    std::optional<double> gain_dB;          // Gain in dB (falls back to GainRaw)
};

// ICameraDriver - camera backend abstract interface
// GenICamDriver implements this interface.
class ICameraDriver : public QObject
{
    Q_OBJECT

public:
    explicit ICameraDriver(QObject* parent = nullptr) : QObject(parent) {}
    ~ICameraDriver() override = default;

    // 연결된 카메라 목록 열거
    virtual QList<DeviceInfo> EnumCameras() = 0;

    // 특정 카메라 그랩 시작 — dock에 ImageReceived 시그널 연결 포함
    virtual bool StartGrabbing(const DeviceInfo& deviceinfo,
                               ImageViewerDock*  dock) = 0;

    // 특정 카메라만 정지 (뷰어 창 닫힐 때)
    virtual void StopGrabbing(const QString& description) = 0;

    // 모든 카메라 정지 (앱 종료 시)
    virtual void StopAll() = 0;
};
