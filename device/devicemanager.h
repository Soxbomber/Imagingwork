// devicemanager.h
#pragma once
#include <QObject>
#include <QList>
#include "deviceinfo.h"

class DeviceManager : public QObject {
    Q_OBJECT
public:
    explicit DeviceManager(QObject* parent = nullptr) : QObject(parent) {}

    // 최초 1회만 열거하고 이후에는 캐시된 목록 반환
    template <typename T>
    QList<DeviceInfo> enumerateDevices(T devices) {
        m_deviceinfoList = devices->EnumCameras(); // 항상 새로 열거
        return m_deviceinfoList;
    }

    // 캐시된 목록 반환 (재열거 없이)
    QList<DeviceInfo> getDeviceList() const {
        return m_deviceinfoList;
    }

    void setDeviceConnected(const QString& description, bool openable) {
        for (auto& deviceinfo : m_deviceinfoList) {
            if (deviceinfo.description == description) {
                deviceinfo.isOpenable = openable;
                //qDebug() << "setDeviceConnected:"
                //    << deviceinfo.name
                //    << "isOpenable ->" << openable;
                emit deviceConnectionChanged(deviceinfo);
                break;
            }
        }
    }

signals:
    void deviceLaunched(const DeviceInfo& deviceinfo);
    void deviceConnectionChanged(const DeviceInfo& deviceinfo);

private:
    QList<DeviceInfo> m_deviceinfoList;
};