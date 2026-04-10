#include "UvcCameraDriver.h"
#include "UvcConvertAVX2.h"
#include <QDebug>
#include <QVideoSurfaceFormat>
#include <QMetaObject>
#include <QEventLoop>
#include <cstring>

// ============================================================
// UvcVideoSurface
// ============================================================

UvcVideoSurface::UvcVideoSurface(QObject* parent)
    : QAbstractVideoSurface(parent)
{}

QList<QVideoFrame::PixelFormat>
UvcVideoSurface::supportedPixelFormats(
    QAbstractVideoBuffer::HandleType type) const
{
    if (type == QAbstractVideoBuffer::NoHandle) {
        return {
            QVideoFrame::Format_ARGB32,
            QVideoFrame::Format_ARGB32_Premultiplied,
            QVideoFrame::Format_RGB32,
            QVideoFrame::Format_RGB24,
            QVideoFrame::Format_BGR32,
            QVideoFrame::Format_BGR24,
            QVideoFrame::Format_RGB565,
            QVideoFrame::Format_YUV420P,
            QVideoFrame::Format_NV12,
            QVideoFrame::Format_NV21,
            QVideoFrame::Format_UYVY,
            QVideoFrame::Format_YUYV,
            QVideoFrame::Format_Jpeg,
        };
    }
    return {};
}

// ── present() ─────────────────────────────────────────────────────────────────
// 카메라 내부 스레드(Media Foundation)에서 호출됨
// 각 카메라의 내부 스레드가 독립적으로 여기를 실행 → 상호 간섭 없음
bool UvcVideoSurface::present(const QVideoFrame& frame)
{
    if (!m_active.load(std::memory_order_acquire)) return false;
    if (!frame.isValid()) return false;

    // UI가 이전 프레임 처리 중이면 drop (QueuedConnection 큐 누적 방지)
    bool expected = false;
    if (!m_busy.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return true;

    QImage& writeBuf = m_useA ? m_bufA : m_bufB;

    if (!writeToBuffer(frame, writeBuf)) {
        m_busy.store(false, std::memory_order_release);
        return false;
    }

    // copy() 없이 emit: QImage implicit sharing 활용
    // UI가 A를 처리하는 동안 B에 write → 충돌 없음
    emit frameReady(writeBuf);
    m_useA = !m_useA;
    m_busy.store(false, std::memory_order_release);

    return true;
}

void UvcVideoSurface::ensureBuffer(QImage& buf, int w, int h)
{
    if (buf.width() != w || buf.height() != h ||
        buf.format() != QImage::Format_ARGB32)
        buf = QImage(w, h, QImage::Format_ARGB32);
}

// ── writeToBuffer: QVideoFrame → ARGB32 (AVX2) ───────────────────────────────
bool UvcVideoSurface::writeToBuffer(const QVideoFrame& frame, QImage& buf)
{
    QVideoFrame f(frame);
    if (!f.map(QAbstractVideoBuffer::ReadOnly))
        return false;

    const int w = f.width();
    const int h = f.height();
    const int n = w * h;
    ensureBuffer(buf, w, h);

    uint32_t* dst = reinterpret_cast<uint32_t*>(buf.bits());
    bool ok = true;

    static const bool useAVX2 = uvcAVX2Supported();

    switch (f.pixelFormat()) {

    case QVideoFrame::Format_ARGB32:
    case QVideoFrame::Format_ARGB32_Premultiplied:
        if (useAVX2) avx2_argb32_to_argb32(f.bits(), dst, n);
        else         std::memcpy(dst, f.bits(), static_cast<size_t>(n*4));
        break;

    case QVideoFrame::Format_RGB32:
        if (useAVX2)
            avx2_rgb32_to_argb32(
                reinterpret_cast<const uint32_t*>(f.bits()), dst, n);
        else
            for (int i=0;i<n;++i)
                dst[i]=reinterpret_cast<const uint32_t*>(f.bits())[i]|0xFF000000u;
        break;

    case QVideoFrame::Format_BGR32:
        if (useAVX2) avx2_bgr32_to_argb32(f.bits(), dst, n);
        else {
            const uint8_t* src = f.bits();
            for (int i=0;i<n;++i)
                dst[i]=0xFF000000u
                     |(static_cast<uint32_t>(src[i*4+2])<<16)
                     |(static_cast<uint32_t>(src[i*4+1])<<8)
                     | static_cast<uint32_t>(src[i*4+0]);
        }
        break;

    case QVideoFrame::Format_RGB24:
        if (useAVX2) avx2_rgb24_to_argb32(f.bits(), f.bytesPerLine(), dst, w, h);
        else {
            const uint8_t* src=f.bits(); const int st=f.bytesPerLine();
            for (int y=0;y<h;++y){
                const uint8_t* r=src+y*st; uint32_t* d=dst+y*w;
                for (int x=0;x<w;++x)
                    d[x]=0xFF000000u|(uint32_t(r[x*3])<<16)|(uint32_t(r[x*3+1])<<8)|r[x*3+2];
            }
        }
        break;

    case QVideoFrame::Format_BGR24:
        if (useAVX2) avx2_bgr24_to_argb32(f.bits(), f.bytesPerLine(), dst, w, h);
        else {
            const uint8_t* src=f.bits(); const int st=f.bytesPerLine();
            for (int y=0;y<h;++y){
                const uint8_t* r=src+y*st; uint32_t* d=dst+y*w;
                for (int x=0;x<w;++x)
                    d[x]=0xFF000000u|(uint32_t(r[x*3+2])<<16)|(uint32_t(r[x*3+1])<<8)|r[x*3];
            }
        }
        break;

    case QVideoFrame::Format_RGB565:
        if (useAVX2)
            avx2_rgb565_to_argb32(
                reinterpret_cast<const uint16_t*>(f.bits()), dst, n);
        else {
            const uint16_t* src=reinterpret_cast<const uint16_t*>(f.bits());
            for (int i=0;i<n;++i){
                const uint16_t px=src[i];
                dst[i]=0xFF000000u
                     |(uint32_t((px>>11)&0x1F)*255/31<<16)
                     |(uint32_t((px>>5)&0x3F)*255/63<<8)
                     |(uint32_t(px&0x1F)*255/31);
            }
        }
        break;

    case QVideoFrame::Format_YUYV:
        if (useAVX2) avx2_yuyv_to_argb32(f.bits(), f.bytesPerLine(), dst, w, h);
        else {
            const uint8_t* src=f.bits(); const int st=f.bytesPerLine();
            auto cl=[](int v)->uint8_t{return uint8_t(v<0?0:v>255?255:v);};
            for (int y=0;y<h;++y){
                const uint8_t* row=src+y*st; uint32_t* d=dst+y*w;
                for (int x=0;x<w;x+=2,row+=4){
                    const int y0=row[0],u=row[1]-128,y1=row[2],v=row[3]-128;
                    auto mk=[&](int yy)->uint32_t{return 0xFF000000u
                        |(uint32_t(cl(yy+1402*v/1000))<<16)
                        |(uint32_t(cl(yy-344*u/1000-714*v/1000))<<8)
                        | uint32_t(cl(yy+1772*u/1000));};
                    d[x]=mk(y0); if(x+1<w) d[x+1]=mk(y1);
                }
            }
        }
        break;

    case QVideoFrame::Format_UYVY:
        if (useAVX2) avx2_uyvy_to_argb32(f.bits(), f.bytesPerLine(), dst, w, h);
        else {
            const uint8_t* src=f.bits(); const int st=f.bytesPerLine();
            auto cl=[](int v)->uint8_t{return uint8_t(v<0?0:v>255?255:v);};
            for (int y=0;y<h;++y){
                const uint8_t* row=src+y*st; uint32_t* d=dst+y*w;
                for (int x=0;x<w;x+=2,row+=4){
                    const int u=row[0]-128,y0=row[1],v=row[2]-128,y1=row[3];
                    auto mk=[&](int yy)->uint32_t{return 0xFF000000u
                        |(uint32_t(cl(yy+1402*v/1000))<<16)
                        |(uint32_t(cl(yy-344*u/1000-714*v/1000))<<8)
                        | uint32_t(cl(yy+1772*u/1000));};
                    d[x]=mk(y0); if(x+1<w) d[x+1]=mk(y1);
                }
            }
        }
        break;

    case QVideoFrame::Format_NV12:
    case QVideoFrame::Format_NV21: {
        const uint8_t* yp=f.bits(0), *uvp=f.bits(1);
        const int ys=f.bytesPerLine(0), us=f.bytesPerLine(1);
        if (yp && uvp && useAVX2) {
            if (f.pixelFormat()==QVideoFrame::Format_NV12)
                avx2_nv12_to_argb32(yp,ys,uvp,us,dst,w,h);
            else
                avx2_nv21_to_argb32(yp,ys,uvp,us,dst,w,h);
        } else {
            QImage tmp=f.image();
            if (tmp.isNull()){ok=false;break;}
            if (tmp.format()!=QImage::Format_ARGB32)
                tmp=tmp.convertToFormat(QImage::Format_ARGB32);
            ensureBuffer(buf,tmp.width(),tmp.height());
            std::memcpy(buf.bits(),tmp.constBits(),size_t(tmp.sizeInBytes()));
        }
        break;
    }

    case QVideoFrame::Format_YUV420P: {
        const uint8_t* yp=f.bits(0),*up=f.bits(1),*vp=f.bits(2);
        if (yp&&up&&vp&&useAVX2)
            avx2_yuv420p_to_argb32(yp,f.bytesPerLine(0),
                                    up,f.bytesPerLine(1),
                                    vp,f.bytesPerLine(2),dst,w,h);
        else {
            QImage tmp=f.image();
            if (tmp.isNull()){ok=false;break;}
            if (tmp.format()!=QImage::Format_ARGB32)
                tmp=tmp.convertToFormat(QImage::Format_ARGB32);
            ensureBuffer(buf,tmp.width(),tmp.height());
            std::memcpy(buf.bits(),tmp.constBits(),size_t(tmp.sizeInBytes()));
        }
        break;
    }

    case QVideoFrame::Format_Jpeg: {
        QImage tmp=QImage::fromData(f.bits(),f.mappedBytes(),"JPEG");
        if (tmp.isNull()){ok=false;break;}
        if (tmp.format()!=QImage::Format_ARGB32)
            tmp=tmp.convertToFormat(QImage::Format_ARGB32);
        ensureBuffer(buf,tmp.width(),tmp.height());
        if (useAVX2)
            avx2_argb32_to_argb32(tmp.constBits(),
                reinterpret_cast<uint32_t*>(buf.bits()),
                tmp.width()*tmp.height());
        else
            std::memcpy(buf.bits(),tmp.constBits(),size_t(tmp.sizeInBytes()));
        break;
    }

    default:
        qWarning("UvcVideoSurface: unsupported format %d",
                 int(f.pixelFormat()));
        ok=false;
        break;
    }

    f.unmap();
    return ok;
}

// ============================================================
// UvcCameraWorker — 카메라 전용 스레드에서 실행
// ============================================================

UvcCameraWorker::UvcCameraWorker(const QCameraInfo& info,
                                  ImageViewerDock*   dock,
                                  QObject* parent)
    : QObject(parent), m_info(info), m_dock(dock)
{}

UvcCameraWorker::~UvcCameraWorker()
{
    // 스레드 종료 전 stopCamera가 호출됐어야 함
    // 안전망: 아직 살아있으면 정지
    if (m_camera) {
        if (m_surface) m_surface->deactivate();
        m_camera->stop();
        if (m_surface && m_dock)
            QObject::disconnect(m_surface.get(), nullptr, m_dock, nullptr);
        m_surface.reset();
        m_camera.reset();
    }
}

void UvcCameraWorker::startCamera()
{
    // 이 함수는 카메라 전용 스레드의 이벤트 루프에서 실행됨
    m_camera  = std::make_unique<QCamera>(m_info);
    m_surface = std::make_unique<UvcVideoSurface>();

    // frameReady → dock (QueuedConnection: 카메라 스레드 → UI 스레드)
    QObject::connect(m_surface.get(), &UvcVideoSurface::frameReady,
                     m_dock, &ImageViewerDock::UpdateImageViewer,
                     Qt::QueuedConnection);

    m_camera->setViewfinder(m_surface.get());
    m_camera->start();

    const bool ok = (m_camera->status() != QCamera::UnavailableStatus);
    if (!ok) {
        qWarning("UvcCameraWorker: camera unavailable");
        if (m_surface) m_surface->deactivate();
        m_camera->stop();
        m_surface.reset();
        m_camera.reset();
    } else {
        qDebug("UvcCameraWorker: started [%s]",
               qPrintable(m_info.description()));
    }

    emit started(ok);
}

void UvcCameraWorker::stopCamera()
{
    // 이 함수는 카메라 전용 스레드의 이벤트 루프에서 실행됨
    if (!m_camera) { emit stopped(); return; }

    // 1. surface 먼저 비활성화 → present() emit 차단
    if (m_surface) m_surface->deactivate();

    // 2. camera stop (Media Foundation 파이프라인 flush)
    m_camera->stop();

    // 3. 시그널 연결 해제
    if (m_surface && m_dock)
        QObject::disconnect(m_surface.get(), nullptr, m_dock, nullptr);

    // 4. 객체 소멸 (이 스레드에서 생성됐으므로 여기서 소멸해야 안전)
    m_surface.reset();
    m_camera.reset();

    qDebug("UvcCameraWorker: stopped [%s]",
           qPrintable(m_info.description()));

    emit stopped();
}

// ============================================================
// UvcCameraDriver
// ============================================================

UvcCameraDriver::UvcCameraDriver(QObject* parent)
    : ICameraDriver(parent)
{}

UvcCameraDriver::~UvcCameraDriver()
{
    StopAll();
}

QList<DeviceInfo> UvcCameraDriver::EnumCameras()
{
    QList<DeviceInfo> result;

    if (!m_cameras.empty()) {
        for (auto& c : m_cameras)
            result.append(c->deviceinfo);
        return result;
    }

    const auto camInfos = QCameraInfo::availableCameras();
    if (camInfos.isEmpty()) {
        qDebug("UvcCameraDriver: no UVC cameras found");
        return result;
    }

    int id = 0;
    for (const QCameraInfo& info : camInfos) {
        auto ctx = std::make_unique<UvcCameraCtx>();
        ctx->info = info;

        const QString name  = info.description().isEmpty()
                              ? info.deviceName() : info.description();
        const QString descr = info.deviceName();
        ctx->deviceinfo = { id++, name, descr, true };
        ctx->deviceinfo.description = descr;

        qDebug("UvcCameraDriver: found [%s]", qPrintable(name));
        result.append(ctx->deviceinfo);
        m_cameras.push_back(std::move(ctx));
    }

    return result;
}

bool UvcCameraDriver::StartGrabbing(const DeviceInfo& di,
                                     ImageViewerDock* dock)
{
    if (!dock) return false;

    for (auto& ctx : m_cameras) {
        if (ctx->deviceinfo.description != di.description) continue;
        if (!ctx->deviceinfo.isOpenable) return true;

        ctx->dock = dock;

        // ── 카메라 전용 스레드 생성 ─────────────────────────────────────
        ctx->thread = std::make_unique<QThread>();
        ctx->thread->setObjectName(
            QString("UvcThread-%1").arg(di.name));

        // ── 워커 생성 (UI 스레드에서 생성 후 이동) ───────────────────────
        ctx->worker = std::make_unique<UvcCameraWorker>(ctx->info, dock);
        ctx->worker->moveToThread(ctx->thread.get());

        // 스레드 시작 시 startCamera() 자동 호출
        QObject::connect(ctx->thread.get(), &QThread::started,
                         ctx->worker.get(), &UvcCameraWorker::startCamera);

        // startCamera 완료 동기 대기: QEventLoop + started signal
        bool camOk = false;
        {
            QEventLoop loop;
            QObject::connect(ctx->worker.get(), &UvcCameraWorker::started,
                             &loop, [&loop, &camOk](bool ok) {
                                 camOk = ok;
                                 loop.quit();
                             }, Qt::QueuedConnection);
            ctx->thread->start();   // → QThread::started → startCamera()
            loop.exec();            // startCamera 완료(emit started)까지 대기
        }

        if (!camOk) {
            qWarning("UvcCameraDriver: failed to start [%s]",
                     qPrintable(di.name));
            ctx->thread->quit();
            ctx->thread->wait(2000);
            ctx->worker.reset();
            ctx->thread.reset();
            return false;
        }

        ctx->deviceinfo.isOpenable = false;
        qDebug("UvcCameraDriver: StartGrabbing OK [%s] thread=%s",
               qPrintable(di.name),
               qPrintable(ctx->thread->objectName()));
        return true;
    }

    qWarning("UvcCameraDriver: device not found [%s]",
             qPrintable(di.description));
    return false;
}

void UvcCameraDriver::StopGrabbing(const QString& description)
{
    for (auto& ctx : m_cameras) {
        if (ctx->deviceinfo.description != description) continue;
        stopCtx(*ctx);
        ctx->dock = nullptr;
        ctx->deviceinfo.isOpenable = true;
        return;
    }
}

void UvcCameraDriver::StopAll()
{
    for (auto& ctx : m_cameras) stopCtx(*ctx);
    m_cameras.clear();
}

void UvcCameraDriver::stopCtx(UvcCameraCtx& ctx)
{
    if (!ctx.thread || !ctx.worker) return;

    // ── 카메라 스레드에서 stopCamera() 실행 후 완료까지 대기 ──────────
    // BlockingQueuedConnection: 카메라 스레드 이벤트 루프에서 실행,
    // 완료될 때까지 현재 스레드(UI) 블로킹
    QMetaObject::invokeMethod(ctx.worker.get(),
                              "stopCamera",
                              Qt::BlockingQueuedConnection);

    // ── 스레드 종료 ──────────────────────────────────────────────────────
    ctx.thread->quit();
    if (!ctx.thread->wait(3000)) {
        qWarning("UvcCameraDriver: thread did not finish in time, forcing");
        ctx.thread->terminate();
        ctx.thread->wait(1000);
    }

    ctx.worker.reset();
    ctx.thread.reset();

    qDebug("UvcCameraDriver: stopCtx done [%s]",
           qPrintable(ctx.deviceinfo.name));
}
