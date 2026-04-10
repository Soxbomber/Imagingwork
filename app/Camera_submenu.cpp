#include "Camera_submenu.h"
#include "ArvCameraDriver.h"
#include "UvcCameraDriver.h"

CamSubWindow::CamSubWindow(QWidget* parent)
    : QDockWidget("Device List", parent)
    , m_deviceManager(nullptr)
    , m_listLayout(nullptr)
    , m_selectedWidget(nullptr)
    , m_u3vDriver(nullptr)
    , m_uvcDriver(nullptr)
{
    setupUi();
    refreshDeviceListFromHardware();
}

CamSubWindow::CamSubWindow(DeviceManager* deviceManager, QWidget* parent)
    : QDockWidget("Device List", parent)
    , m_deviceManager(deviceManager)
    , m_listLayout(nullptr)
    , m_selectedWidget(nullptr)
    , m_u3vDriver(nullptr)
    , m_uvcDriver(nullptr)
{
    setupUi();
    refreshDeviceListFromHardware();
}

CamSubWindow::~CamSubWindow()
{
    delete m_u3vDriver; m_u3vDriver = nullptr;
    delete m_uvcDriver; m_uvcDriver = nullptr;
}

ICameraDriver* CamSubWindow::getDriverFor(const QString& description) const
{
    return m_driverMap.value(description, nullptr);
}

ICameraDriver* CamSubWindow::getCamera() const
{
    // 하위 호환: U3V 드라이버 우선 반환
    return m_u3vDriver ? m_u3vDriver : m_uvcDriver;
}

void CamSubWindow::setupUi()
{
    QWidget*     content    = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    setMinimumWidth(220);
    resize(260, height());

    QHBoxLayout* btnLayout  = new QHBoxLayout();
    QPushButton* refreshBtn = new QPushButton("Refresh", content);
    btnLayout->addWidget(refreshBtn);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    QScrollArea* scroll = new QScrollArea(content);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* listWidget = new QWidget();
    m_listLayout = new QVBoxLayout(listWidget);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(2);
    m_listLayout->addStretch();

    scroll->setWidget(listWidget);
    mainLayout->addWidget(scroll);
    setWidget(content);

    connect(refreshBtn, &QPushButton::clicked,
            this, &CamSubWindow::refreshDeviceListFromHardware);
}

void CamSubWindow::refreshDeviceList()
{
    while (m_listLayout->count() > 1) {
        QLayoutItem* item = m_listLayout->takeAt(0);
        if (item && item->widget()) delete item->widget();
        delete item;
    }
    m_selectedWidget = nullptr;
    if (!m_deviceManager) return;

    QString curSection;
    for (const DeviceInfo& info : m_deviceManager->getDeviceList()) {
        // 카메라 타입별 섹션 헤더 삽입
        const bool isUvc = m_driverMap.value(info.description) == m_uvcDriver;
        const QString section = isUvc ? "UVC Cameras" : "USB3 Vision Cameras";
        if (section != curSection) {
            curSection = section;
            auto* lbl = new QLabel(section);
            lbl->setStyleSheet(
                "font-weight:bold; color:#888; padding:4px 2px 2px;");
            m_listLayout->insertWidget(m_listLayout->count() - 1, lbl);
        }

        auto* widget = new DeviceItemWidget(info, this);
        m_listLayout->insertWidget(m_listLayout->count() - 1, widget);
        connect(widget, &DeviceItemWidget::clicked,
                this, &CamSubWindow::onDeviceClicked);
        connect(widget, &DeviceItemWidget::doubleClicked,
                this, &CamSubWindow::onDeviceDoubleClicked);
    }
}

void CamSubWindow::refreshDeviceListFromHardware()
{
    if (!m_deviceManager) return;

    m_driverMap.clear();

    // ── 1. USB3 Vision (libusb + Aravis) ─────────────────────────────────
    if (!m_u3vDriver)
        m_u3vDriver = new ArvCameraDriver();

    const auto u3vList = m_u3vDriver->EnumCameras();
    for (const auto& di : u3vList)
        m_driverMap[di.description] = m_u3vDriver;

    // ── 2. UVC (Qt5 Multimedia) ───────────────────────────────────────────
    if (!m_uvcDriver)
        m_uvcDriver = new UvcCameraDriver();

    const auto uvcList = m_uvcDriver->EnumCameras();
    for (const auto& di : uvcList)
        m_driverMap[di.description] = m_uvcDriver;

    // ── DeviceManager에 합산 등록 ─────────────────────────────────────────
    // U3V 먼저, UVC 다음 순서
    QList<DeviceInfo> allDevices;
    allDevices << u3vList << uvcList;

    // DeviceManager를 직접 채움
    // (enumerateDevices는 단일 드라이버용이라 여기서 직접 설정)
    m_deviceManager->clearDeviceList();
    for (const auto& di : allDevices)
        m_deviceManager->addDevice(di);

    refreshDeviceList();

    qDebug("CamSubWindow: %d U3V + %d UVC = %d total cameras",
           u3vList.size(), uvcList.size(), allDevices.size());
}

void CamSubWindow::onDeviceClicked(DeviceItemWidget* selected)
{
    if (m_selectedWidget && m_selectedWidget != selected) {
        bool exists = false;
        for (int i = 0; i < m_listLayout->count(); i++) {
            QLayoutItem* item = m_listLayout->itemAt(i);
            if (item && item->widget() == m_selectedWidget) {
                exists = true; break;
            }
        }
        if (exists) m_selectedWidget->setSelected(false);
    }
    m_selectedWidget = selected;
}

void CamSubWindow::onDeviceDoubleClicked(const DeviceInfo& deviceinfo)
{
    if (!deviceinfo.isOpenable) return;

    // description으로 해당 드라이버 찾기
    ICameraDriver* driver = m_driverMap.value(deviceinfo.description, nullptr);
    if (!driver) {
        qWarning("CamSubWindow: no driver found for [%s]",
                 qPrintable(deviceinfo.description));
        return;
    }

    emit deviceReadyToLaunch(deviceinfo, driver);
}
