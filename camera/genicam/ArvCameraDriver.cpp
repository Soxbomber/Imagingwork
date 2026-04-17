#include "ArvCameraDriver.h"
#include <QDebug>

ArvCameraDriver::ArvCameraDriver(QObject* parent) : ICameraDriver(parent)
{
    if (libusb_init(&m_ctx) != LIBUSB_SUCCESS) {
        qWarning("ArvCameraDriver: libusb_init failed");
        m_ctx = nullptr;
        return;
    }
#ifdef LIBUSB_OPTION_LOG_LEVEL
    libusb_set_option(m_ctx, LIBUSB_OPTION_LOG_LEVEL,
                      LIBUSB_LOG_LEVEL_WARNING);
#endif
    qDebug("ArvCameraDriver: libusb %s ready",
           libusb_get_version()->describe);
}

ArvCameraDriver::~ArvCameraDriver()
{
    StopAll();
    if (m_ctx) { libusb_exit(m_ctx); m_ctx = nullptr; }
}

// ── EnumCameras ───────────────────────────────────────────────────────────────
QList<DeviceInfo> ArvCameraDriver::EnumCameras()
{
    QList<DeviceInfo> result;
    if (!m_ctx) return result;

    if (!m_cameras.empty()) {
        for (auto& c : m_cameras) result.append(c->deviceinfo);
        return result;
    }

    const auto infos = ArvU3vDevice::enumerate(m_ctx);
    if (infos.isEmpty()) {
        qWarning("ArvCameraDriver: no cameras found.\n"
                 "  Windows: run Zadig -> bind WinUSB to camera\n"
                 "  vcpkg install libusb:x64-windows");
        return result;
    }

    int id = 0;
    for (const auto& info : infos) {
        auto ctx = std::make_unique<ArvCameraCtx>();
        ctx->busNumber     = info.busNumber;
        ctx->deviceAddress = info.deviceAddress;

        // 정보만 읽고 즉시 닫음 (StartGrabbing 시 재열기)
        if (ctx->device.open(m_ctx, info.busNumber, info.deviceAddress)) {
            ctx->device.bootstrap();
            ctx->device.close();
        }

        const QString name =
            ctx->device.modelName().isEmpty()   ? info.modelName   : ctx->device.modelName();
        const QString serial =
            ctx->device.serialNumber().isEmpty() ? info.serialNumber : ctx->device.serialNumber();

        ctx->deviceinfo = { id++, name, serial, true };
        result.append(ctx->deviceinfo);
        m_cameras.push_back(std::move(ctx));
    }
    return result;
}

// ── StartGrabbing ─────────────────────────────────────────────────────────────
// Aravis arv_camera_start_acquisition() 순서:
//   1. open + bootstrap
//   2. loadGenApi  (XML 다운로드 + AcquisitionStart 주소 파싱)
//   3. enableStream (SIRM 설정 + data EP reset + SI_CONTROL_ENABLE)
//   4. 워커 스레드 시작
//   5. executeAcquisitionStart (카메라 센서 전송 시작) ← 이것이 없으면 데이터 없음

bool ArvCameraDriver::StartGrabbing(const DeviceInfo& di,
                                     ImageViewerDock* dock)
{
    if (!dock || !m_ctx) return false;

    for (auto& ctx : m_cameras) {
        if (ctx->deviceinfo.serialNumber != di.serialNumber) continue;
        if (!ctx->deviceinfo.isOpenable) return true;

        ctx->dock = dock;

        // ── 1. 디바이스 열기 ─────────────────────────────────────────────
        if (!ctx->device.open(m_ctx, ctx->busNumber, ctx->deviceAddress)) {
            qWarning("ArvCameraDriver: open failed: %s",
                     qPrintable(ctx->device.lastError()));
            return false;
        }

        // ── 2. Bootstrap (ABRM→SBRM→SIRM 주소 확보) ─────────────────────
        if (!ctx->device.bootstrap()) {
            qWarning("ArvCameraDriver: bootstrap failed");
            ctx->device.close();
            return false;
        }

        // ── 3. GenApi XML 다운로드 + 파싱 ────────────────────────────────
        // AcquisitionStart/Stop 레지스터 주소를 XML에서 읽어야 함
        if (!ctx->device.loadGenApi()) {
            // XML 파싱 실패 시 경고만 내고 계속 진행
            // (AcquisitionStart 없이도 카메라가 자동으로 전송하는 경우 있음)
            qWarning("ArvCameraDriver: GenApi XML parse failed - "
                     "AcquisitionStart may not work");
        }

        // ── 4. Stream enable (SIRM 설정 + data EP reset) ──────────────────
        if (!ctx->device.enableStream(ctx->streamParams)) {
            qWarning("ArvCameraDriver: enableStream failed");
            ctx->device.close();
            return false;
        }

        // ── 5. 워커 스레드 시작 ───────────────────────────────────────────
        ctx->stream = new ArvU3vStream(&ctx->device, ctx->streamParams);
        ctx->stream->moveToThread(&ctx->thread);

        QObject::connect(&ctx->thread, &QThread::started,
                         ctx->stream, &ArvU3vStream::Start);
        QObject::connect(ctx->stream, &ArvU3vStream::ImageReceived,
                         dock, &ImageViewerDock::UpdateImageViewer,
                         Qt::QueuedConnection);
        QObject::connect(ctx->stream, &ArvU3vStream::errorOccurred,
                         ctx->stream, [](const QString& m) {
                             qWarning("ArvU3vStream: %s", qPrintable(m));
                         });

        ctx->thread.start();

        // ── 6. AcquisitionStart (카메라 센서 전송 시작) ───────────────────
        // Aravis: arv_camera_execute_command("AcquisitionStart")
        // 스트림 스레드가 시작된 후에 실행 (Aravis와 동일 순서)
        if (!ctx->device.executeAcquisitionStart()) {
            qWarning("ArvCameraDriver: AcquisitionStart failed - "
                     "stream may not produce frames");
            // 치명적이지 않음: 일부 카메라는 자동 전송
        }

        ctx->deviceinfo.isOpenable = false;
        qDebug("ArvCameraDriver: StartGrabbing OK [%s]",
               qPrintable(di.name));
        return true;
    }
    return false;
}

// ── StopGrabbing ──────────────────────────────────────────────────────────────
void ArvCameraDriver::StopGrabbing(const QString& desc)
{
    for (auto& ctx : m_cameras) {
        if (ctx->deviceinfo.serialNumber != desc) continue;
        stopCtx(*ctx);
        ctx->dock   = nullptr;
        ctx->stream = nullptr;
        ctx->deviceinfo.isOpenable = true;
        return;
    }
}

void ArvCameraDriver::StopAll()
{
    for (auto& ctx : m_cameras) stopCtx(*ctx);
    m_cameras.clear();
}

// ── stopCtx ───────────────────────────────────────────────────────────────────
// Aravis arv_camera_stop_acquisition() 순서:
//   1. AcquisitionStop (카메라 센서 전송 중지)
//   2. stream thread 종료
//   3. disableStream (SI_CONTROL = 0)
//   4. close

void ArvCameraDriver::stopCtx(ArvCameraCtx& ctx)
{
    // 1. AcquisitionStop 먼저 (카메라에 전송 중지 지시)
    if (ctx.device.isOpen())
        ctx.device.executeAcquisitionStop();

    // 2. 워커 스레드 종료
    if (ctx.stream) {
        ctx.stream->Stop();
        ctx.thread.quit();
        ctx.thread.wait(3000);
    }

    // 3. Stream disable + 디바이스 닫기
    if (ctx.device.isOpen()) {
        ctx.device.disableStream();
        ctx.device.close();
    }
}

// ── GenApi 파라미터 제어 구현 ─────────────────────────────────────────────────

ArvCameraCtx* ArvCameraDriver::findCtx(const QString& description)
{
    for (auto& c : m_cameras)
        if (c->deviceinfo.serialNumber == description) return c.get();
    return nullptr;
}

bool ArvCameraDriver::setResolution(const QString& desc,
                                     int w, int h, int offsetX, int offsetY)
{
    ArvCameraCtx* ctx = findCtx(desc);
    if (!ctx || !ctx->controller) return false;
    auto& ctrl = *ctx->controller;

    // 해상도 변경 순서: AcquisitionStop → 변경 → AcquisitionStart
    ctrl.execute("AcquisitionStop");

    // Offset을 먼저 0으로 (크기 변경 시 오프셋 범위 초과 방지)
    ctrl.setInteger("OffsetX", 0);
    ctrl.setInteger("OffsetY", 0);

    bool ok = true;
    ok &= ctrl.setInteger("Width",   static_cast<int64_t>(w));
    ok &= ctrl.setInteger("Height",  static_cast<int64_t>(h));
    ok &= ctrl.setInteger("OffsetX", static_cast<int64_t>(offsetX));
    ok &= ctrl.setInteger("OffsetY", static_cast<int64_t>(offsetY));

    ctrl.execute("AcquisitionStart");
    return ok;
}

bool ArvCameraDriver::setExposureTime(const QString& desc, double us)
{
    ArvCameraCtx* ctx = findCtx(desc);
    if (!ctx || !ctx->controller) return false;
    auto& ctrl = *ctx->controller;

    // ExposureTime 노드가 Integer(ns)인 경우 자동 변환
    if (ctrl.nodeType("ExposureTime") == GenApiController::NodeType::Integer)
        return ctrl.setInteger("ExposureTime",
                               static_cast<int64_t>(us * 1000.0)); // us→ns
    return ctrl.setFloat("ExposureTime", us);
}

bool ArvCameraDriver::getExposureTime(const QString& desc, double& us)
{
    ArvCameraCtx* ctx = findCtx(desc);
    if (!ctx || !ctx->controller) return false;
    auto& ctrl = *ctx->controller;

    if (ctrl.nodeType("ExposureTime") == GenApiController::NodeType::Integer) {
        int64_t ns{}; bool ok = ctrl.getInteger("ExposureTime", ns);
        if (ok) us = ns / 1000.0; return ok;
    }
    return ctrl.getFloat("ExposureTime", us);
}

bool ArvCameraDriver::setGain(const QString& desc, double dB)
{
    ArvCameraCtx* ctx = findCtx(desc);
    if (!ctx || !ctx->controller) return false;
    // Gain이 없으면 GainRaw 시도
    if (ctx->controller->hasNode("Gain"))
        return ctx->controller->setFloat("Gain", dB);
    return ctx->controller->setFloat("GainRaw", dB);
}

bool ArvCameraDriver::getGain(const QString& desc, double& dB)
{
    ArvCameraCtx* ctx = findCtx(desc);
    if (!ctx || !ctx->controller) return false;
    if (ctx->controller->hasNode("Gain"))
        return ctx->controller->getFloat("Gain", dB);
    return ctx->controller->getFloat("GainRaw", dB);
}

bool ArvCameraDriver::setFrameRate(const QString& desc, double fps)
{
    ArvCameraCtx* ctx = findCtx(desc);
    if (!ctx || !ctx->controller) return false;
    auto& ctrl = *ctx->controller;

    // FrameRate 활성화 노드가 있으면 먼저 활성화
    if (ctrl.hasNode("AcquisitionFrameRateEnable"))
        ctrl.setEnum("AcquisitionFrameRateEnable", "true");
    if (ctrl.hasNode("AcquisitionFrameRateEnabled"))
        ctrl.setEnum("AcquisitionFrameRateEnabled", "true");

    if (ctrl.hasNode("AcquisitionFrameRate"))
        return ctrl.setFloat("AcquisitionFrameRate", fps);
    if (ctrl.hasNode("ResultingFrameRate"))
        return ctrl.setFloat("ResultingFrameRate", fps);
    return ctrl.setFloat("FrameRate", fps);
}

bool ArvCameraDriver::getFrameRate(const QString& desc, double& fps)
{
    ArvCameraCtx* ctx = findCtx(desc);
    if (!ctx || !ctx->controller) return false;
    auto& ctrl = *ctx->controller;
    if (ctrl.hasNode("AcquisitionFrameRate"))
        return ctrl.getFloat("AcquisitionFrameRate", fps);
    if (ctrl.hasNode("ResultingFrameRate"))
        return ctrl.getFloat("ResultingFrameRate", fps);
    return ctrl.getFloat("FrameRate", fps);
}

bool ArvCameraDriver::setInteger(const QString& desc,
                                  const QString& node, int64_t val)
{
    ArvCameraCtx* ctx = findCtx(desc);
    return ctx && ctx->controller &&
           ctx->controller->setInteger(node.toUtf8().constData(), val);
}

bool ArvCameraDriver::setFloat(const QString& desc,
                                const QString& node, double val)
{
    ArvCameraCtx* ctx = findCtx(desc);
    return ctx && ctx->controller &&
           ctx->controller->setFloat(node.toUtf8().constData(), val);
}

bool ArvCameraDriver::setEnum(const QString& desc,
                               const QString& node, const QString& entry)
{
    ArvCameraCtx* ctx = findCtx(desc);
    return ctx && ctx->controller &&
           ctx->controller->setEnum(node.toUtf8().constData(),
                                    entry.toUtf8().constData());
}

