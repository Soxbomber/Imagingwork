// ids_camera.h
#pragma once
#include <QObject>
#include <QImage>
#include <peak/peak.hpp>
#include <QThread>
#include <peak_icv/library/peak_icv_library.hpp>
#include <peak_icv/pipeline/peak_icv_default_pipeline.hpp>
#include "deviceinfo.h"
#include "acquisitionWorker.h"
#include "ImageViewerDock.h"

struct DeviceContext
{
    std::shared_ptr<peak::core::Device>      device{};
    std::shared_ptr<peak::core::DataStream>  dataStream{};
    std::shared_ptr<peak::core::NodeMap>     nodemapRemoteDevice{};
    peak::common::PixelFormat                pixelFormat{};
    ImageViewerDock*                         displayWindow{};  // 소유권은 QMainWindow
    QSize                                    imageSize{};
    AcquisitionWorker*                       acquisitionWorker{};
    QThread                                  acquisitionThread{};
    DeviceInfo                               m_deviceinfo;
};

class IdsCamera : public QObject
{
    Q_OBJECT
public:
    IdsCamera();
    ~IdsCamera() override;

    QList<DeviceInfo> EnumCameras();

    // [FIX] dock을 인자로 받아 시그널 연결 후 그랩 시작
    bool StartGrabbing(const DeviceInfo& deviceinfo, ImageViewerDock* dock);

    void StopAll();

private:
    void stopDevice(DeviceContext& ctx);
    std::vector<std::unique_ptr<DeviceContext>> m_vecDevices{};
};
