// Camera_submenu.h
#pragma once
#include <QDockWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QMessageBox>
#include "devicemanager.h"
#include "deviceitemwidget.h"
#include "ids_camera.h"

class CamSubWindow : public QDockWidget {
    Q_OBJECT
public:
    explicit CamSubWindow(QWidget* parent = nullptr);
    explicit CamSubWindow(DeviceManager* deviceManager, QWidget* parent = nullptr);
    ~CamSubWindow();

    void setDeviceManager(DeviceManager* deviceManager);
    IdsCamera* getIdsCamera() const { return m_idscamera; }

private:
    void setupDock();
    void setupUI();
    void refreshDeviceList();
    void refreshDeviceListFromHardware();
    void onDeviceClicked(DeviceItemWidget* selected);

    // [FIX] dock을 먼저 생성한 뒤 StartGrabbing 호출을 위해
    //       ui_Imagingwork 쪽에서 dock을 만들어 전달하는 구조로 변경.
    //       onDeviceDoubleClicked 대신 deviceReadyToLaunch 시그널로 위임.
    void onDeviceDoubleClicked(const DeviceInfo& device);
    void onTopLevelChanged(bool floating);

    DeviceManager*    m_deviceManager;
    QVBoxLayout*      m_listLayout;
    DeviceItemWidget* m_selectedWidget;
    IdsCamera*        m_idscamera;

signals:
    // [FIX] ui_Imagingwork가 dock을 만들고 StartGrabbing까지 처리하도록
    //       dock 생성 요청 시그널을 추가
    void deviceReadyToLaunch(const DeviceInfo& deviceinfo, IdsCamera* camera);
};
