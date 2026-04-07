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

Imagingwork::~Imagingwork() {}

// ── 슬롯 구현 ────────────────────────────────────────────────────────────────

void Imagingwork::onDeviceReadyToLaunch(const DeviceInfo& deviceinfo, ICameraDriver* camera)
{
    // ① 뷰어 dock 생성 (중복이면 기존 dock 반환)
    ImageViewerDock* dock = ui.getOrCreateViewer(this, deviceinfo);
    if (!dock) return;

    // ② dock이 준비된 상태에서 StartGrabbing 호출
    bool grabbing = camera->StartGrabbing(deviceinfo, dock);

    // ③ 뷰어 창 닫힘 → dock의 closeEvent → viewerClosed 시그널
    //    → onViewerClosed 슬롯에서 카메라 정지 처리
    connect(dock,  &ImageViewerDock::viewerClosed,
            this,  &Imagingwork::onViewerClosed);

    // ④ DeviceManager 상태 갱신 및 UI 갱신
    ui.m_deviceManager->setDeviceConnected(deviceinfo.description, !grabbing);
    emit ui.m_deviceManager->deviceLaunched(deviceinfo);
}

void Imagingwork::onViewerClosed(const QString& description)
{
    // dock의 closeEvent에서 호출됨
    ICameraDriver* camera = ui.m_subWindow->getCamera();
    if (camera)
        camera->StopGrabbing(description);

    // DeviceManager에 카메라가 다시 열릴 수 있는 상태로 갱신
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
    // 앱 종료 시 열린 모든 카메라 정리
    if (ui.m_subWindow && ui.m_subWindow->getCamera())
        ui.m_subWindow->getCamera()->StopAll();

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
