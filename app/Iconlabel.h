// IconLabel.h
#pragma once
#include <QLabel>
#include <QMouseEvent>

class IconLabel : public QLabel {
    Q_OBJECT
public:
    explicit IconLabel(QWidget* parent = nullptr);
    ~IconLabel() = default;

signals:
    void clicked();

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
};