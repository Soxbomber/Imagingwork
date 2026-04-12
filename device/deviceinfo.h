// deviceinfo.h
#pragma once
#include <QString>
#include <QMetaType>

struct DeviceInfo {
    int         id;
    QString     name;
    QString     serialnumber;
    bool        isOpenable;
};

Q_DECLARE_METATYPE(DeviceInfo)