#include "Iconlabel.h"

IconLabel::IconLabel(QWidget* parent) : QLabel(parent) {
    setCursor(Qt::PointingHandCursor);
    setAlignment(Qt::AlignCenter);
    setStyleSheet(R"(
            QLabel {
                background: transparent;
                border-radius: 4px;
                padding: 4px;
            }
        )");
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void IconLabel::enterEvent(QEnterEvent* event) {
#else
void IconLabel::enterEvent(QEvent * event) {
#endif
    setStyleSheet(R"(
            QLabel {
                background: rgba(0,0,0,0.12);
                border-radius: 4px;
                padding: 4px;
            }
        )");
    QLabel::enterEvent(event);
}

void IconLabel::leaveEvent(QEvent * event) {
    setStyleSheet(R"(
            QLabel {
                background: transparent;
                border-radius: 4px;
                padding: 4px;
            }
        )");
    QLabel::leaveEvent(event);
}

void IconLabel::mousePressEvent(QMouseEvent * event) {
    if (event->button() == Qt::LeftButton) {
        setStyleSheet(R"(
                QLabel {
                    background: rgba(0,0,0,0.22);
                    border-radius: 4px;
                    padding: 4px;
                }
            )");
        emit clicked();
    }
}

void IconLabel::mouseReleaseEvent(QMouseEvent * event) {
    if (event->button() == Qt::LeftButton) {
        setStyleSheet(R"(
                QLabel {
                    background: rgba(0,0,0,0.12);
                    border-radius: 4px;
                    padding: 4px;
                }
            )");
    }
    QLabel::mouseReleaseEvent(event);
}