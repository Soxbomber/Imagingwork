#include "ids_camera.h"

IdsCamera::IdsCamera()
    : m_vecDevices{}
{
}

IdsCamera::~IdsCamera()
{
    StopAll();
}

void IdsCamera::StopAll()
{
    for (auto& device : m_vecDevices) {
        if (device)
            stopDevice(*device);
    }
    m_vecDevices.clear();
}

void IdsCamera::stopDevice(DeviceContext& ctx)
{
    if (ctx.acquisitionWorker) {
        ctx.acquisitionWorker->Stop();
        ctx.acquisitionThread.quit();
        ctx.acquisitionThread.wait();
    }

    try {
        if (ctx.dataStream && ctx.dataStream->IsGrabbing()) {
            ctx.nodemapRemoteDevice
                ->FindNode<peak::core::nodes::CommandNode>("AcquisitionStop")
                ->Execute();
            ctx.nodemapRemoteDevice
                ->FindNode<peak::core::nodes::CommandNode>("AcquisitionStop")
                ->WaitUntilDone();
        }
    }
    catch (const std::exception&) {}

    try {
        if (ctx.dataStream) {
            if (ctx.dataStream->IsGrabbing()) {
                ctx.dataStream->KillWait();
                ctx.dataStream->StopAcquisition(peak::core::AcquisitionStopMode::Default);
            }
            ctx.dataStream->Flush(peak::core::DataStreamFlushMode::DiscardAll);
            for (const auto& buffer : ctx.dataStream->AnnouncedBuffers())
                ctx.dataStream->RevokeBuffer(buffer);
            ctx.nodemapRemoteDevice
                ->FindNode<peak::core::nodes::IntegerNode>("TLParamsLocked")
                ->SetValue(0);
        }
    }
    catch (const std::exception&) {}
}

QList<DeviceInfo> IdsCamera::EnumCameras()
{
    QList<DeviceInfo> deviceinfolist;
    try
    {
        auto& deviceManager = peak::DeviceManager::Instance();
        deviceManager.Update();

        if (deviceManager.Devices().empty()) {
            m_vecDevices.clear();
            return {};
        }

        int devicecount = 0;
        for (const auto& deviceDescriptor : deviceManager.Devices())
        {
            if (deviceDescriptor->IsOpenable())
            {
                auto device = deviceDescriptor->OpenDevice(
                    peak::core::DeviceAccessType::Control);
                if (!device) continue;

                auto deviceElem = std::make_unique<DeviceContext>();
                deviceElem->device                   = device;
                deviceElem->m_deviceinfo.id          = devicecount;
                deviceElem->m_deviceinfo.isOpenable  = true;
                deviceElem->m_deviceinfo.name        =
                    QString::fromStdString(device->ModelName());
                deviceElem->m_deviceinfo.description =
                    QString::fromStdString(device->SerialNumber());

                auto dataStreams = device->DataStreams();
                if (dataStreams.empty()) { device.reset(); continue; }

                try {
                    deviceElem->dataStream = dataStreams.at(0)->OpenDataStream();
                }
                catch (const std::exception&) { device.reset(); continue; }

                deviceElem->nodemapRemoteDevice =
                    device->RemoteDevice()->NodeMaps().at(0);

                try {
                    deviceElem->nodemapRemoteDevice
                        ->FindNode<peak::core::nodes::EnumerationNode>("UserSetSelector")
                        ->SetCurrentEntry("Default");
                    deviceElem->nodemapRemoteDevice
                        ->FindNode<peak::core::nodes::CommandNode>("UserSetLoad")
                        ->Execute();
                    deviceElem->nodemapRemoteDevice
                        ->FindNode<peak::core::nodes::CommandNode>("UserSetLoad")
                        ->WaitUntilDone();
                }
                catch (peak::core::Exception const&) {}

                auto payloadSize = deviceElem->nodemapRemoteDevice
                    ->FindNode<peak::core::nodes::IntegerNode>("PayloadSize")
                    ->Value();

                auto bufferCountMin =
                    deviceElem->dataStream->NumBuffersAnnouncedMinRequired();

                for (size_t i = 0; i < bufferCountMin; ++i) {
                    auto buf = deviceElem->dataStream->AllocAndAnnounceBuffer(
                        static_cast<size_t>(payloadSize), nullptr);
                    deviceElem->dataStream->QueueBuffer(buf);
                }

                deviceElem->imageSize.setWidth(static_cast<int>(
                    deviceElem->nodemapRemoteDevice
                        ->FindNode<peak::core::nodes::IntegerNode>("Width")
                        ->Value()));
                deviceElem->imageSize.setHeight(static_cast<int>(
                    deviceElem->nodemapRemoteDevice
                        ->FindNode<peak::core::nodes::IntegerNode>("Height")
                        ->Value()));

                deviceinfolist.append(deviceElem->m_deviceinfo);
                m_vecDevices.push_back(std::move(deviceElem));
                devicecount++;
            }
            else
            {
                for (auto& dev : m_vecDevices) {
                    if (QString::fromStdString(deviceDescriptor->SerialNumber())
                        == dev->m_deviceinfo.description) {
                        deviceinfolist.append(dev->m_deviceinfo);
                    }
                }
            }
        }
    }
    catch (const std::exception&) {}

    return deviceinfolist;
}

// [FIX] dock을 인자로 받아 시그널 연결 후 그랩 시작
bool IdsCamera::StartGrabbing(const DeviceInfo& deviceinfo, ImageViewerDock* dock)
{
    if (!dock) {
        qWarning("IdsCamera::StartGrabbing — dock is null");
        return false;
    }

    for (auto& device : m_vecDevices)
    {
        if (device->m_deviceinfo.description != deviceinfo.description)
            continue;

        // 이미 그랩 중이면 스킵
        if (device->dataStream && device->dataStream->IsGrabbing())
            return true;

        device->displayWindow = dock;
        device->pixelFormat   = peak::common::PixelFormat::BGRa8;

        device->acquisitionWorker = new AcquisitionWorker(
            device->dataStream, device->pixelFormat, device->imageSize);

        device->acquisitionWorker->moveToThread(&device->acquisitionThread);

        // 워커 스레드 → GUI 스레드: QueuedConnection으로 안전하게 전달
        QObject::connect(
            device->acquisitionWorker, &AcquisitionWorker::ImageReceived,
            device->displayWindow,     &ImageViewerDock::UpdateImageViewer,
            Qt::QueuedConnection);

        QObject::connect(
            &device->acquisitionThread, &QThread::started,
            device->acquisitionWorker,  &AcquisitionWorker::Start);

        device->acquisitionThread.start();

        try {
            device->dataStream->StartAcquisition();
            device->nodemapRemoteDevice
                ->FindNode<peak::core::nodes::CommandNode>("AcquisitionStart")
                ->Execute();
            device->nodemapRemoteDevice
                ->FindNode<peak::core::nodes::CommandNode>("AcquisitionStart")
                ->WaitUntilDone();
        }
        catch (const std::exception& e) {
            qWarning("AcquisitionStart failed: %s", e.what());
            return false;
        }

        return device->dataStream->IsGrabbing();
    }

    qWarning("IdsCamera::StartGrabbing — device not found. "
             "EnumCameras()가 먼저 호출되었는지 확인하세요.");
    return false;
}
