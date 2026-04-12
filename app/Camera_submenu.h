#pragma once
#include <QDockWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QLabel>
#include "devicemanager.h"
#include "deviceitemwidget.h"
#include "ICameraDriver.h"

class CamSubWindow : public QDockWidget {
    Q_OBJECT
public:
    explicit CamSubWindow(QWidget* parent = nullptr);
    explicit CamSubWindow(DeviceManager* deviceManager, QWidget* parent = nullptr);
    ~CamSubWindow();

    // 특정 카메라를 담당하는 드라이버 반환 (description 기준)
    ICameraDriver* getDriverFor(const QString& description) const;
    // 하위 호환: 첫 번째 드라이버 반환
    ICameraDriver* getCamera() const;
    // 모든 드라이버의 모든 카메라 정지 (앱 종료 시)
    void stopAllDrivers();

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

    // USB3 Vision (libusb + Aravis)
    ICameraDriver* m_u3vDriver;
    // UVC (Qt5 Multimedia QCamera)
    ICameraDriver* m_uvcDriver;
    // GigE Vision (GVCP/GVSP over UDP)
    ICameraDriver* m_gigeDriver;

    // description → driver 매핑 (열거 시 채워짐)
    QMap<QString, ICameraDriver*> m_driverMap;
};
