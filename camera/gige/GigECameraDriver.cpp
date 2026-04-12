#include "GigECameraDriver.h"
#include <QNetworkInterface>
#include <QDebug>

GigECameraDriver::GigECameraDriver(QObject* parent)
    : ICameraDriver(parent)
{}

GigECameraDriver::~GigECameraDriver()
{
    StopAll();
}

// ── EnumCameras: UDP broadcast Discovery ─────────────────────────────────────
QList<DeviceInfo> GigECameraDriver::EnumCameras()
{
    QList<DeviceInfo> result;

    // 캐시된 목록 반환
    if (!m_cameras.empty()) {
        for (auto& c : m_cameras)
            result.append(c->deviceinfo);
        return result;
    }

    const auto found = GigEDevice::discover(1000);
    if (found.isEmpty()) {
        qDebug("GigECameraDriver: no GigE cameras found");
        return result;
    }

    int id = 0;
    for (const auto& info : found) {
        auto ctx         = std::make_unique<GigECameraCtx>();
        ctx->info        = info;
        const QString name   = QString("%1 %2").arg(
            info.manufacturerName, info.modelName).trimmed();
        const QString descr  = info.ipAddress.toString(); // IP를 고유 ID로 사용

        ctx->deviceinfo = { id++, name, descr, true };
        result.append(ctx->deviceinfo);
        m_cameras.push_back(std::move(ctx));

        qDebug("GigECameraDriver: [%s] @ %s",
               qPrintable(name), qPrintable(descr));
    }

    return result;
}

// ── StartGrabbing ─────────────────────────────────────────────────────────────
bool GigECameraDriver::StartGrabbing(const DeviceInfo& di,
                                      ImageViewerDock* dock)
{
    if (!dock) return false;

    for (auto& ctx : m_cameras) {
        if (ctx->deviceinfo.serialnumber != di.serialnumber) continue;
        if (!ctx->deviceinfo.isOpenable) return true;

        ctx->dock = dock;

        // ── 1. GVCP 연결 ─────────────────────────────────────────────────────
        ctx->device = std::make_unique<GigEDevice>();
        if (!ctx->device->open(ctx->info.ipAddress)) {
            qWarning("GigECameraDriver: GVCP open failed [%s]",
                     qPrintable(di.serialnumber));
            ctx->device.reset();
            return false;
        }

        // ── 2. 스트림 소켓 바인딩 ────────────────────────────────────────────
        ctx->stream = std::make_unique<GigEStream>();
        if (!ctx->stream->bind(0)) {  // 0 = 임의 포트
            qWarning("GigECameraDriver: stream bind failed");
            ctx->device->close();
            ctx->device.reset();
            ctx->stream.reset();
            return false;
        }
        const uint16_t streamPort = ctx->stream->boundPort();

        // ── 3. 카메라에 스트림 목적지 설정 ──────────────────────────────────
        const QHostAddress localIp =
            findLocalIp(ctx->info.ipAddress);
        if (localIp.isNull()) {
            qWarning("GigECameraDriver: cannot find local IP for camera subnet");
            ctx->device->close();
            ctx->device.reset();
            ctx->stream.reset();
            return false;
        }

        if (!ctx->device->setStreamDestination(localIp, streamPort)) {
            qWarning("GigECameraDriver: setStreamDestination failed");
            ctx->device->close();
            ctx->device.reset();
            ctx->stream.reset();
            return false;
        }

        // ── 4. 패킷 크기 협상 ────────────────────────────────────────────────
        ctx->device->negotiatePacketSize(8192);

        // ── 5. GenApi XML 로드 → AcquisitionStart/Stop 주소 획득 ─────────────
        const QByteArray xmlData = ctx->device->loadGenApiXml();
        if (!xmlData.isEmpty()) {
            ArvGenApiInfo genApi = ArvGenApiXml::parse(xmlData);
            if (genApi.parsed) {
                ctx->acquisitionStart = genApi.acquisitionStart;
                ctx->acquisitionStop  = genApi.acquisitionStop;
                qDebug("GigECameraDriver: GenApi parsed OK "
                       "AcqStart=0x%llX AcqStop=0x%llX",
                       (unsigned long long)ctx->acquisitionStart.address,
                       (unsigned long long)ctx->acquisitionStop.address);
            }
        } else {
            qWarning("GigECameraDriver: GenApi XML load failed — "
                     "AcquisitionStart may not work");
        }

        // ── 6. 스트림 스레드 시작 ────────────────────────────────────────────
        ctx->stream->moveToThread(&ctx->streamThread);
        QObject::connect(&ctx->streamThread, &QThread::started,
                         ctx->stream.get(), &GigEStream::Start);
        QObject::connect(ctx->stream.get(), &GigEStream::ImageReceived,
                         dock, &ImageViewerDock::UpdateImageViewer,
                         Qt::QueuedConnection);
        ctx->streamThread.setObjectName(
            QString("GigEStream-%1").arg(di.serialnumber));
        ctx->streamThread.start();

        // ── 7. AcquisitionStart ──────────────────────────────────────────────
        if (ctx->acquisitionStart.valid) {
            // GenApi Command: writeRegister to pValue address with cmdValue
            if (!ctx->device->writeRegister(
                    static_cast<uint32_t>(ctx->acquisitionStart.address),
                    ctx->acquisitionStart.commandValue)) {
                qWarning("GigECameraDriver: AcquisitionStart failed");
            } else {
                qDebug("GigECameraDriver: AcquisitionStart OK");
            }
        } else {
            // Fallback: 표준 AcquisitionMode/Start 레지스터 시도
            qWarning("GigECameraDriver: no AcquisitionStart node — "
                     "camera may not stream");
        }

        ctx->deviceinfo.isOpenable = false;
        qDebug("GigECameraDriver: StartGrabbing OK [%s] -> %s:%u",
               qPrintable(di.name),
               qPrintable(localIp.toString()), streamPort);
        return true;
    }

    qWarning("GigECameraDriver: device not found [%s]",
             qPrintable(di.serialnumber));
    return false;
}

// ── StopGrabbing ──────────────────────────────────────────────────────────────
void GigECameraDriver::StopGrabbing(const QString& serialnumber)
{
    for (auto& ctx : m_cameras) {
        if (ctx->deviceinfo.serialnumber != serialnumber) continue;
        stopCtx(*ctx);
        ctx->dock = nullptr;
        ctx->deviceinfo.isOpenable = true;
        return;
    }
}

void GigECameraDriver::StopAll()
{
    for (auto& ctx : m_cameras) stopCtx(*ctx);
    m_cameras.clear();
}

void GigECameraDriver::stopCtx(GigECameraCtx& ctx)
{
    // ── 1. AcquisitionStop ───────────────────────────────────────────────────
    if (ctx.device && ctx.acquisitionStop.valid) {
        ctx.device->writeRegister(
            static_cast<uint32_t>(ctx.acquisitionStop.address),
            ctx.acquisitionStop.commandValue);
    }

    // ── 2. 스트림 종료 ────────────────────────────────────────────────────────
    if (ctx.stream) {
        ctx.stream->Stop();
        // 스트림 스레드가 ctx.streamThread에 있으므로 thread quit
    }
    if (ctx.streamThread.isRunning()) {
        ctx.streamThread.quit();
        if (!ctx.streamThread.wait(3000)) {
            qWarning("GigECameraDriver: stream thread timeout, forcing");
            ctx.streamThread.terminate();
            ctx.streamThread.wait(1000);
        }
    }
    ctx.stream.reset();

    // ── 3. GVCP 연결 해제 ────────────────────────────────────────────────────
    if (ctx.device) {
        ctx.device->close();
        ctx.device.reset();
    }

    qDebug("GigECameraDriver: stopCtx done [%s]",
           qPrintable(ctx.deviceinfo.name));
}

// ── findLocalIp: 카메라와 같은 서브넷의 호스트 IP 탐색 ───────────────────────
QHostAddress GigECameraDriver::findLocalIp(const QHostAddress& cameraIp)
{
    const quint32 camIpInt = cameraIp.toIPv4Address();

    for (const QNetworkInterface& iface :
         QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;

        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            const quint32 localIpInt = entry.ip().toIPv4Address();
            const quint32 mask       = entry.netmask().toIPv4Address();

            // 같은 서브넷인지 확인
            if ((localIpInt & mask) == (camIpInt & mask)) {
                qDebug("GigECameraDriver: using local IP %s for camera %s",
                       qPrintable(entry.ip().toString()),
                       qPrintable(cameraIp.toString()));
                return entry.ip();
            }
        }
    }

    return QHostAddress(); // not found
}
