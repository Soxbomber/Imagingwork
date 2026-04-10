#include "Imagingwork.h"

Imagingwork::Imagingwork(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    connect(ui.m_subWindow, &CamSubWindow::deviceReadyToLaunch,
            this,           &Imagingwork::onDeviceReadyToLaunch);

    connect(ui.camButton, &IconLabel::clicked,
            this,         &Imagingwork::onCamButtonClicked);
}

Imagingwork::~Imagingwork()
{
    // closeEvent가 호출되지 않는 경우(delete window 등)를 위한 안전망
    // m_stopped 플래그로 중복 정리 방지
    stopAllCameras();
}

// ── 내부 정리 헬퍼 ────────────────────────────────────────────────────────────
void Imagingwork::stopAllCameras()
{
    if (m_stopped) return;
    m_stopped = true;

    if (ui.m_subWindow) {
        // U3V + UVC 드라이버 모두 정지
        if (auto* u3v = ui.m_subWindow->getCamera())
            u3v->StopAll();
        // UVC는 getDriverFor로 접근 불가하므로 CamSubWindow 소멸자에서 처리
    }

    for (auto* viewer : ui.m_imageViewers)
        if (viewer) viewer->clearImage();
}

// ── 슬롯 ─────────────────────────────────────────────────────────────────────

void Imagingwork::onDeviceReadyToLaunch(const DeviceInfo& deviceinfo,
                                         ICameraDriver* camera)
{
    // ① 뷰어 dock 생성 (중복이면 기존 dock 반환 + clearImage)
    ImageViewerDock* dock = ui.getOrCreateViewer(this, deviceinfo);
    if (!dock) return;

    // ② 프레임 수신 재개 (getOrCreateViewer 내부 clearImage 후 차단됨)
    dock->acceptFrames();

    // ③ 스트리밍 시작
    bool grabbing = camera->StartGrabbing(deviceinfo, dock);

    // ④ 뷰어가 닫힐 때 → 카메라 정지
    // connect 중복 방지: 동일 dock에 이미 연결됐으면 재연결 안 함
    connect(dock, &ImageViewerDock::viewerClosed,
            this, &Imagingwork::onViewerClosed,
            Qt::UniqueConnection);

    // ⑤ DeviceManager 상태 갱신
    ui.m_deviceManager->setDeviceConnected(deviceinfo.description, !grabbing);
    emit ui.m_deviceManager->deviceLaunched(deviceinfo);
}

void Imagingwork::onViewerClosed(const QString& description)
{
    // description으로 해당 드라이버를 찾아 정지
    // (U3V와 UVC가 각각 다른 드라이버를 사용)
    ICameraDriver* camera = ui.m_subWindow
                            ? ui.m_subWindow->getDriverFor(description)
                            : nullptr;
    if (!camera)
        camera = ui.m_subWindow ? ui.m_subWindow->getCamera() : nullptr;
    if (camera)
        camera->StopGrabbing(description);

    ui.m_deviceManager->setDeviceConnected(description, true);
}

void Imagingwork::onCamButtonClicked()
{
    if (ui.m_subWindow->isHidden())
        ui.m_subWindow->show();
    else {
        ui.m_subWindow->raise();
        ui.m_subWindow->activateWindow();
    }
}

// ── 공개 API ─────────────────────────────────────────────────────────────────

void Imagingwork::addImageViewer(const QString& title)
{
    ui.addImageViewer(this, title);
}

void Imagingwork::loadImage(int viewerIndex, const QPixmap& pixmap)
{
    ui.setImage(viewerIndex, pixmap);
}

void Imagingwork::loadImage(int viewerIndex, const QString& filePath)
{
    ui.setImage(viewerIndex, filePath);
}

void Imagingwork::loadImage(int viewerIndex, QImage image)
{
    ui.setImage(viewerIndex, std::move(image));
}

// ── 이벤트 ───────────────────────────────────────────────────────────────────

void Imagingwork::closeEvent(QCloseEvent* event)
{
    // 메인 윈도우 닫기:
    //  1. 카메라 스트림/USB 즉시 정리 (스레드 join 포함)
    //  2. dock들이 WA_DeleteOnClose로 소멸하기 전에 완료되어야 함
    stopAllCameras();

    // 부모 클래스 처리 → 자식 위젯(dock 등) 닫기 → app.exec() 종료
    event->accept();
    QMainWindow::closeEvent(event);
}

void Imagingwork::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (ui.m_imageViewers.isEmpty()) return;

    QList<QDockWidget*> dockedViewers;
    for (auto* viewer : ui.m_imageViewers)
        if (viewer && !viewer->isFloating())
            dockedViewers.append(viewer);

    if (dockedViewers.isEmpty()) return;

    const int camWidth = (ui.m_subWindow
                          && !ui.m_subWindow->isHidden()
                          && !ui.m_subWindow->isFloating())
                         ? ui.m_subWindow->width() : 0;

    const int availableWidth  = width() - camWidth;
    const int availableHeight = height()
        - (menuBar()   ? menuBar()->height()   : 0)
        - (statusBar() ? statusBar()->height() : 0)
        - (ui.mainToolBar ? ui.mainToolBar->height() : 0);

    QList<int> widths, heights;
    for (int i = 0; i < dockedViewers.size(); ++i) {
        widths.append(availableWidth);
        heights.append(availableHeight);
    }

    resizeDocks(dockedViewers, widths,  Qt::Horizontal);
    resizeDocks(dockedViewers, heights, Qt::Vertical);
}
