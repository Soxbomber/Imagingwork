// deviceitemwidget.h
#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include "deviceinfo.h"

class DeviceItemWidget : public QWidget {
    Q_OBJECT
public:
    explicit DeviceItemWidget(const DeviceInfo& deviceinfo, QWidget* parent = nullptr);
    DeviceInfo device() const { return m_deviceinfo; }
    void setSelected(bool selected);

signals:
    void clicked(DeviceItemWidget* self);   // 클릭 시 자신을 전달
    void doubleClicked(const DeviceInfo& deviceinfo);

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent * event) override;
#endif
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void updateStyle();

    DeviceInfo  m_deviceinfo;
    bool    m_selected;
    };