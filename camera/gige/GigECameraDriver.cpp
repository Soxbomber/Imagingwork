#include "GigECameraDriver.h"
#include "../genicam/GenApiController.h"
#include <QEventLoop>
#include <QNetworkInterface>
#include <QDebug>

GigECameraDriver::GigECameraDriver(QObject* parent)
    : ICameraDriver(parent)
{}

GigECameraDriver::~GigECameraDriver()
{
    StopAll();
}

// ── EnumCameras: 브로드캐스트 Discovery + 유니캐스트 폴백 ─────────────────────
QList<DeviceInfo> GigECameraDriver::EnumCameras()
{
    QList<DeviceInfo> result;

    // 캐시된 목록 반환
    if (!m_cameras.empty()) {
        for (auto& c : m_cameras)
            result.append(c->deviceinfo);
        return result;
    }

    // ── 1차: 브로드캐스트 Discovery ──────────────────────────────────────────
    auto found = GigEDevice::discover(1000);

    // ── 2차: 유니캐스트 Discovery (Switch/VLAN 환경 대응) ────────────────────
    // 브로드캐스트로 못 찾았거나, m_knownIps에 IP가 등록된 경우 유니캐스트 시도
    if ((found.isEmpty() || !m_knownIps.isEmpty()) && !m_knownIps.isEmpty()) {
        qDebug("GigECameraDriver: trying unicast discovery (%d known IPs)",
               m_knownIps.size());
        auto unicastFound = GigEDevice::discoverUnicast(m_knownIps, 1000);
        // 브로드캐스트 결과와 합치되 IP 중복 제거
        QSet<QString> existingIps;
        for (const auto& f : found)
            existingIps.insert(f.ipAddress.toString());
        for (const auto& u : unicastFound) {
            if (!existingIps.contains(u.ipAddress.toString()))
                found.append(u);
        }
    }

    if (found.isEmpty()) {
        qDebug("GigECameraDriver: no GigE cameras found");
        qDebug("  Tip: If behind a switch/hub, use addKnownIp() to add camera IPs");
        return result;
    }

    int id = 0;
    for (const auto& info : found) {
        auto ctx       = std::make_unique<GigECameraCtx>();
        ctx->info      = info;
        const QString name = QString("%1 %2").arg(
            info.manufacturerName, info.modelName).trimmed();
        const QString sn = !info.serialNumber.isEmpty() ? info.serialNumber
                         : !info.macAddress.isEmpty()   ? info.macAddress
                         : info.ipAddress.toString();
        ctx->deviceinfo = { id++, name, sn, true };
        result.append(ctx->deviceinfo);
        m_cameras.push_back(std::move(ctx));
        qDebug("GigECameraDriver: [%s] sn=%s ip=%s",
               qPrintable(name), qPrintable(sn),
               qPrintable(info.ipAddress.toString()));
    }
    return result;
}

// ── StartGrabbing ─────────────────────────────────────────────────────────────
bool GigECameraDriver::StartGrabbing(const DeviceInfo& di,
                                      ImageViewerDock* dock)
{
    if (!dock) return false;

    for (auto& ctx : m_cameras) {
        if (ctx->deviceinfo.serialNumber != di.serialNumber) continue;
        if (!ctx->deviceinfo.isOpenable) return true;

        ctx->dock = dock;

        // ── 1. GVCP 연결 ─────────────────────────────────────────────────────
        ctx->device = std::make_unique<GigEDevice>();
        if (!ctx->device->open(ctx->info.ipAddress)) {
            qWarning("GigECameraDriver: GVCP open failed [%s]",
                     qPrintable(di.serialNumber));
            ctx->device.reset();
            return false;
        }

        // ── 2. 로컬 IP ───────────────────────────────────────────────────────
        const QHostAddress localIp = findLocalIp(ctx->info.ipAddress);
        if (localIp.isNull()) {
            qWarning("GigECameraDriver: cannot find local IP for camera subnet");
            ctx->device->close(); ctx->device.reset();
            return false;
        }
        qDebug("GigECameraDriver: using local IP %s for camera %s",
               qPrintable(localIp.toString()),
               qPrintable(ctx->info.ipAddress.toString()));

        // ── 3. GenApi XML 로드 (GenICam SDK) ─────────────────────────────────
        const QByteArray xmlData = ctx->device->loadGenApiXml();
        if (!xmlData.isEmpty()) {
            // GigEDevice R/W를 GenApi IPort로 브리지
            auto readFn = [&ctx](void* buf, int64_t addr, int64_t len) {
                ctx->device->readMemory(static_cast<uint32_t>(addr),
                                        static_cast<uint8_t*>(buf),
                                        static_cast<uint32_t>(len));
            };
            auto writeFn = [&ctx](const void* buf, int64_t addr, int64_t len) {
                ctx->device->writeMemory(static_cast<uint32_t>(addr),
                                         static_cast<const uint8_t*>(buf),
                                         static_cast<uint32_t>(len));
            };
            // ZIP 여부 자동 감지 (앞 4 bytes = PK = ZIP magic)
            bool isZipped = (xmlData.size() >= 4 &&
                             (uint8_t)xmlData[0] == 0x50 &&
                             (uint8_t)xmlData[1] == 0x4B);
            std::vector<uint8_t> xmlVec(
                reinterpret_cast<const uint8_t*>(xmlData.constData()),
                reinterpret_cast<const uint8_t*>(xmlData.constData()) + xmlData.size());

            if (ctx->genApi.loadXml(xmlVec, isZipped, readFn, writeFn)) {
                qDebug("GigECameraDriver: GenApi SDK OK (%zu nodes)",
                       ctx->genApi.getNodeNames().size());
            } else {
                qWarning("GigECameraDriver: GenApi XML load failed");
            }
        } else {
            qWarning("GigECameraDriver: GenApi XML load failed");
        }

        // ── 4. 스트림 객체 생성 ───────────────────────────────────────────────
        // bind()는 Start() 내부(스트림 스레드)에서 수행 → QSocketNotifier 경고 없음
        ctx->stream = std::make_unique<GigEStream>();
        ctx->stream->setBindPort(0);  // 0 = 임의 포트

        // ── 5. 스트림 스레드 시작 + bind 완료 대기 ───────────────────────────
        ctx->stream->moveToThread(&ctx->streamThread);

        uint16_t streamPort = 0;
        {
            QEventLoop waitLoop;
            // context(&waitLoop) 명시 → MSVC 람다 오버로드 추론 문제 해결
            QObject::connect(
                ctx->stream.get(), &GigEStream::bindCompleted,
                &waitLoop,
                [&streamPort, &waitLoop](uint16_t port, bool ok) {
                    streamPort = ok ? port : 0;
                    waitLoop.quit();
                },
                Qt::QueuedConnection);
            QObject::connect(&ctx->streamThread, &QThread::started,
                             ctx->stream.get(), &GigEStream::Start);
            QObject::connect(ctx->stream.get(), &GigEStream::ImageReceived,
                             dock, &ImageViewerDock::UpdateImageViewer,
                             Qt::QueuedConnection);
            ctx->streamThread.setObjectName(
                QString("GigEStream-%1").arg(di.serialNumber));
            ctx->streamThread.start();
            waitLoop.exec();  // bindCompleted 시그널 올 때까지 대기
        }

        if (streamPort == 0) {
            qWarning("GigECameraDriver: stream bind failed");
            ctx->device->close(); ctx->device.reset();
            ctx->stream.reset();
            return false;
        }
        qDebug("GigECameraDriver: stream bound port=%u", streamPort);

        // ── 6. 카메라에 스트림 목적지 설정 ──────────────────────────────────
        if (!ctx->device->setStreamDestination(localIp, streamPort)) {
            qWarning("GigECameraDriver: setStreamDestination failed");
            ctx->device->close(); ctx->device.reset();
            ctx->stream.reset();
            return false;
        }

        // ── 7. 패킷 크기 협상 ────────────────────────────────────────────────
        ctx->device->negotiatePacketSize(8192);

        // ── 8. AcquisitionStart (GenICam SDK) ────────────────────────────────
        if (ctx->genApi.isLoaded()) {
            ctx->genApi.setEnum("AcquisitionMode", "Continuous");
            ctx->genApi.setEnum("TriggerMode", "Off");

            // Pre-acquisition NodeMap parameters
            if (ctx->initParams.exposureTime_us.has_value()) {
                const double us = *ctx->initParams.exposureTime_us;
                if (ctx->genApi.nodeType("ExposureTime") == GenApiController::NodeType::Integer)
                    ctx->genApi.setInteger("ExposureTime", static_cast<int64_t>(us * 1000.0));
                else
                    ctx->genApi.setFloat("ExposureTime", us);
                qDebug("GigECameraDriver: pre-acq ExposureTime=%.1f us", us);
            }
            if (ctx->initParams.gain_dB.has_value()) {
                const double dB = *ctx->initParams.gain_dB;
                if (ctx->genApi.hasNode("Gain"))
                    ctx->genApi.setFloat("Gain", dB);
                else
                    ctx->genApi.setFloat("GainRaw", dB);
                qDebug("GigECameraDriver: pre-acq Gain=%.2f dB", dB);
            }

            if (!ctx->genApi.execute("AcquisitionStart"))
                qWarning("GigECameraDriver: AcquisitionStart failed");
            else
                qDebug("GigECameraDriver: AcquisitionStart OK");
        } else {
            qWarning("GigECameraDriver: GenApi not loaded, skipping AcquisitionStart");
        }

        ctx->deviceinfo.isOpenable = false;
        qDebug("GigECameraDriver: StartGrabbing OK [%s] -> %s:%u",
               qPrintable(di.name), qPrintable(localIp.toString()), streamPort);
        return true;
    }

    qWarning("GigECameraDriver: device not found [%s]",
             qPrintable(di.serialNumber));
    return false;
}

// ── StopGrabbing ──────────────────────────────────────────────────────────────
void GigECameraDriver::StopGrabbing(const QString& description)
{
    for (auto& ctx : m_cameras) {
        if (ctx->deviceinfo.serialNumber != description) continue;
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
    if (ctx.device && ctx.deviceinfo.isOpenable == false) {
        if (ctx.genApi.isLoaded()) {
            ctx.genApi.execute("AcquisitionStop");
        }
        // SCP0 = 0으로 스트림 채널 비활성화
        ctx.device->writeRegister(GVBS_SC0_PORT_OFFSET, 0);
    }

    // ── 2. 스트림 종료 ────────────────────────────────────────────────────────
    // GigEStream::Start()는 Qt 이벤트 루프가 아닌 waitForReadyRead 블로킹 루프
    // → QThread::quit()은 동작 안 함
    // → Stop()으로 m_running=false 설정 후 wait()
    if (ctx.stream)
        ctx.stream->Stop();

    if (ctx.streamThread.isRunning()) {
        // 최대 1.5초 대기 (waitForReadyRead 타임아웃 200ms × 여유)
        if (!ctx.streamThread.wait(1500)) {
            qWarning("GigECameraDriver: stream thread timeout, forcing");
            ctx.streamThread.terminate();
            ctx.streamThread.wait(500);
        }
    }
    ctx.stream.reset();

    // ── 3. GVCP 연결 해제 ────────────────────────────────────────────────────
    if (ctx.device) {
        ctx.device->close();
        ctx.device.reset();
    }

    ctx.deviceinfo.isOpenable = true;
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

// ── setInitParams ─────────────────────────────────────────────────────────────
void GigECameraDriver::setInitParams(const QString& description,
                                      const NodeMapInitParams& p)
{
    for (auto& ctx : m_cameras) {
        if (ctx->deviceinfo.serialNumber == description) {
            ctx->initParams = p;
            return;
        }
    }
}

// ── addKnownIp / clearKnownIps ────────────────────────────────────────────────
// GigEDevice::s_knownIps (정적)에 위임 → discover()에서 유니캐스트 자동 전송
void GigECameraDriver::addKnownIp(const QHostAddress& ip)
{
    m_knownIps.append(ip);          // 인스턴스 목록 (EnumCameras용)
    GigEDevice::addKnownIp(ip);     // 정적 목록 (discover() 내부 유니캐스트용)
}

void GigECameraDriver::clearKnownIps()
{
    m_knownIps.clear();
    GigEDevice::clearKnownIps();
}
