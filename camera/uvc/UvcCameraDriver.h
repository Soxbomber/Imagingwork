#pragma once
// ============================================================
// UvcCameraDriver — Qt5 Multimedia QCamera 기반 UVC 드라이버
//
// 멀티카메라 독립 스레드 구조:
//   각 UVC 카메라마다 전용 QThread + QEventLoop
//   → QCamera 생성/제어/소멸이 해당 스레드에서만 실행
//   → 카메라 간 완전 독립 (한 카메라 지연이 다른 카메라에 무영향)
//
//   [카메라 스레드 N]
//     QCamera + UvcVideoSurface 소유
//     present() (픽셀 변환) 실행
//     emit frameReady → QueuedConnection → UI 스레드
//
//   [UI 스레드]
//     StartGrabbing / StopGrabbing 호출
//     → invokeMethod(Qt::BlockingQueuedConnection)으로
//       카메라 스레드에 안전하게 위임
// ============================================================

#include "ICameraDriver.h"
#include <QCamera>
#include <QAbstractVideoSurface>
#include <QVideoFrame>
#include <QCameraInfo>
#include <QThread>
#include <atomic>
#include <memory>
#include <vector>

// ── UVC 프레임 수신 Surface ───────────────────────────────────────────────────
class UvcVideoSurface : public QAbstractVideoSurface
{
    Q_OBJECT
public:
    explicit UvcVideoSurface(QObject* parent = nullptr);

    QList<QVideoFrame::PixelFormat> supportedPixelFormats(
        QAbstractVideoBuffer::HandleType type) const override;

    bool present(const QVideoFrame& frame) override;

    void deactivate() { m_active.store(false, std::memory_order_release); }

signals:
    void frameReady(QImage image);

private:
    QImage m_bufA, m_bufB;
    bool   m_useA{true};

    std::atomic<bool> m_active{true};
    std::atomic<bool> m_busy  {false};

    bool writeToBuffer(const QVideoFrame& f, QImage& buf);
    void ensureBuffer(QImage& buf, int w, int h);
};

// ── 카메라 워커: 카메라 스레드에서 실행 ──────────────────────────────────────
// QCamera는 생성된 스레드의 이벤트 루프에서만 안전하게 동작
// → UvcCameraWorker를 카메라 전용 QThread로 이동시켜 사용
class UvcCameraWorker : public QObject
{
    Q_OBJECT
public:
    explicit UvcCameraWorker(const QCameraInfo& info,
                             ImageViewerDock*   dock,
                             QObject* parent = nullptr);
    ~UvcCameraWorker() override;

public slots:
    // 카메라 스레드에서 실행됨
    void startCamera();
    void stopCamera();

signals:
    void started(bool ok);   // startCamera 완료 통보
    void stopped();          // stopCamera 완료 통보

private:
    QCameraInfo               m_info;
    ImageViewerDock*          m_dock;
    std::unique_ptr<QCamera>         m_camera;
    std::unique_ptr<UvcVideoSurface> m_surface;
};

// ── UVC 카메라 컨텍스트 ────────────────────────────────────────────────────────
struct UvcCameraCtx {
    QCameraInfo       info;
    DeviceInfo        deviceinfo;
    ImageViewerDock*  dock{};

    // 카메라 전용 스레드 + 워커
    std::unique_ptr<QThread>         thread;
    std::unique_ptr<UvcCameraWorker> worker;
};

// ── UvcCameraDriver ───────────────────────────────────────────────────────────
class UvcCameraDriver : public ICameraDriver
{
    Q_OBJECT
public:
    explicit UvcCameraDriver(QObject* parent = nullptr);
    ~UvcCameraDriver() override;

    QList<DeviceInfo> EnumCameras()                                    override;
    bool StartGrabbing(const DeviceInfo& di, ImageViewerDock* dock)    override;
    void StopGrabbing(const QString& serialnumber)                      override;
    void StopAll()                                                     override;

private:
    void stopCtx(UvcCameraCtx& ctx);

    std::vector<std::unique_ptr<UvcCameraCtx>> m_cameras;
};
