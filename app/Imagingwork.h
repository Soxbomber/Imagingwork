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
    void onDeviceReadyToLaunch(const DeviceInfo& deviceinfo, ICameraDriver* camera);
    void onViewerClosed(const QString& description);
    void onCamButtonClicked();

private:
    // 카메라 스트림 정지 + viewer 메모리 해제
    // closeEvent와 소멸자 모두에서 호출 가능 (m_stopped로 중복 방지)
    void stopAllCameras();

    Ui::ImagingworkClass ui;
    bool m_stopped{ false };  // stopAllCameras 중복 실행 방지
};
