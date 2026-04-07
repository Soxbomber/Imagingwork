#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_Imagingwork.h"

class Imagingwork : public QMainWindow
{
    Q_OBJECT

public:
    explicit Imagingwork(QWidget* parent = nullptr);
    ~Imagingwork();

    void addImageViewer(const QString& title = "");
    void loadImage(int viewerIndex, const QPixmap& pixmap);
    void loadImage(int viewerIndex, const QString& filePath);
    void loadImage(int viewerIndex, QImage image);

protected:
    void closeEvent(QCloseEvent* event)   override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    // CamSubWindow::deviceReadyToLaunch 수신
    void onDeviceReadyToLaunch(const DeviceInfo& deviceinfo, ICameraDriver* camera);

    // ImageViewerDock::viewerClosed 수신 → 카메라 정지
    void onViewerClosed(const QString& description);

    // IconLabel(camButton) 클릭
    void onCamButtonClicked();

private:
    Ui::ImagingworkClass ui;
};
