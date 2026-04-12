#include "deviceitemwidget.h"

DeviceItemWidget::DeviceItemWidget(const DeviceInfo& deviceinfo, QWidget* parent)
    : QWidget(parent), m_deviceinfo(deviceinfo), m_selected(false)
{
    setFixedHeight(50);
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("DeviceItemWidget");
    setStyleSheet(R"(
        QWidget#DeviceItemWidget {
            background: transparent;
            border: none;
        }
    )");

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);

    QLabel* statusIcon = new QLabel(this);
    statusIcon->setFixedSize(10, 10);
    statusIcon->setAttribute(Qt::WA_StyledBackground, true);
    statusIcon->setAutoFillBackground(true);

    // isConnected 값에 따라 명시적으로 색상 적용
    if (deviceinfo.isOpenable) {
        statusIcon->setStyleSheet(
            "background-color: #2ecc71;"
            "border-radius: 5px;"
            "border: none;"
        );
    }
    else {
        statusIcon->setStyleSheet(
            "background-color: #ba442e;"
            "border-radius: 5px;"
            "border: none;"
        );
    }
    statusIcon->setAttribute(Qt::WA_TransparentForMouseEvents);

    QLabel* nameLabel = new QLabel(deviceinfo.name, this);
    nameLabel->setStyleSheet(
        "background: transparent;"
        "font-size: 13px; font-weight: bold; color: #222;"
    );
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    QLabel* descLabel = new QLabel(deviceinfo.serialnumber, this);
    descLabel->setStyleSheet(
        "background: transparent;"
        "font-size: 11px; color: #888;"
    );
    descLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    layout->addWidget(statusIcon);
    layout->addWidget(nameLabel);
    layout->addStretch();
    layout->addWidget(descLabel);

    setLayout(layout);
}

void DeviceItemWidget::setSelected(bool selected) {
    m_selected = selected;
    updateStyle();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void DeviceItemWidget::enterEvent(QEnterEvent* event) {
#else
void DeviceItemWidget::enterEvent(QEvent* event) {
#endif
    if (!m_selected)  // 선택 상태가 아닐 때만 hover 적용
        setStyleSheet(R"(
                QWidget#DeviceItemWidget {
                    background: rgba(0,0,0,0.06);
                    border: none;
                    border-radius: 4px;
                }
            )");
    QWidget::enterEvent(event);
}

void DeviceItemWidget::leaveEvent(QEvent * event) {
    if (!m_selected)  // 선택 상태가 아닐 때만 초기화
        setStyleSheet(R"(
                QWidget#DeviceItemWidget {
                    background: transparent;
                    border: none;
                }
            )");
    QWidget::leaveEvent(event);
}

void DeviceItemWidget::mousePressEvent(QMouseEvent * event) {
    if (event->button() == Qt::LeftButton) {
        m_selected = true;
        updateStyle();
        emit clicked(this);  // 자신을 전달해서 다른 위젯 선택 해제에 사용
    }
    QWidget::mousePressEvent(event);
}

void DeviceItemWidget::mouseDoubleClickEvent(QMouseEvent * event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked(m_deviceinfo);
    }
    QWidget::mouseDoubleClickEvent(event);
}

void DeviceItemWidget::updateStyle() {
    if (m_selected) {
        setStyleSheet(R"(
                QWidget#DeviceItemWidget {
                    background: rgba(0,0,0,0.15);
                    border: none;
                    border-radius: 4px;
                }
            )");
    }
    else {
        setStyleSheet(R"(
                QWidget#DeviceItemWidget {
                    background: transparent;
                    border: none;
                }
            )");
    }
}