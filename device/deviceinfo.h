// deviceinfo.h
#pragma once
#include <QString>
#include <QMetaType>

struct DeviceInfo {
    int         id;
    QString     name;
    QString     serialNumber;   // 카메라 고유 식별자 (U3V:시리얼, GigE:IP, UVC:deviceName)
    bool        isOpenable;
};

Q_DECLARE_METATYPE(DeviceInfo)
