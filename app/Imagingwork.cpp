#include "Imagingwork.h"

Imagingwork::Imagingwork(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    // ── 시그널 연결 ───────────────────────────────────────────────
    // CamSubWindow 더블클릭 → 뷰어 생성 후 카메라 그랩 시작
    connect(ui.m_subWindow, &CamSubWindow::deviceReadyToLaunch,
            this,           &Imagingwork::onDeviceReadyToLaunch);

    // 툴바 카메라 버튼 → CamSubWindow 토글
    connect(ui.camButton, &IconLabel::clicked,
            this,         &Imagingwork::onCamButtonClicked);
}

Imagingwork::~Imagingwork() {}

// ── 슬롯 구현 ────────────────────────────────────────────────────────────────

void Imagingwork::onDeviceReadyToLaunch(const DeviceInfo& deviceinfo, IdsCamera* camera)
{
    // ① 뷰어 dock을 먼저 생성 (중복이면 기존 dock 반환)
    ImageViewerDock* dock = ui.getOrCreateViewer(this, deviceinfo);
    if (!dock) return;

    // ② dock이 준비된 상태에서 StartGrabbing 호출
    //    → AcquisitionWorker::ImageReceived 가 dock에 QueuedConnection으로 연결됨
    bool grabbing = camera->StartGrabbing(deviceinfo, dock);

    // ③ DeviceManager 상태 갱신
    ui.m_deviceManager->setDeviceConnected(deviceinfo.description, !grabbing);

    // ④ deviceLaunched emit (CamSubWindow UI 갱신용)
    emit ui.m_deviceManager->deviceLaunched(deviceinfo);
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

    int camWidth = (ui.m_subWindow
                    && !ui.m_subWindow->isHidden()
                    && !ui.m_subWindow->isFloating())
                   ? ui.m_subWindow->width() : 0;

    int availableWidth  = width() - camWidth;
    int availableHeight = height()
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
