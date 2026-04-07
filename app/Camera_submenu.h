#pragma once
#include <QDockWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include "devicemanager.h"
#include "deviceitemwidget.h"
#include "ICameraDriver.h"

class CamSubWindow : public QDockWidget {
    Q_OBJECT
public:
    explicit CamSubWindow(QWidget* parent = nullptr);
    explicit CamSubWindow(DeviceManager* deviceManager, QWidget* parent = nullptr);
    ~CamSubWindow();

    ICameraDriver* getCamera() const { return m_idscamera; }

signals:
    void deviceReadyToLaunch(const DeviceInfo& deviceinfo,
                             ICameraDriver*    camera);

private:
    void setupUi();
    void refreshDeviceList();
    void refreshDeviceListFromHardware();
    void onDeviceClicked(DeviceItemWidget* selected);
    void onDeviceDoubleClicked(const DeviceInfo& deviceinfo);

    DeviceManager*    m_deviceManager;
    QVBoxLayout*      m_listLayout;
    DeviceItemWidget* m_selectedWidget;
    ICameraDriver*    m_idscamera;
};
