#include "Camera_submenu.h"
#include "ArvCameraDriver.h"
#include "UvcCameraDriver.h"
#include "GigECameraDriver.h"
#include <QDebug>

// ── 드라이버별 섹션명 ─────────────────────────────────────────────────────────
static QString sectionName(ICameraDriver* driver,
                            ICameraDriver* u3v,
                            ICameraDriver* uvc,
                            ICameraDriver* gige)
{
    if (driver == u3v)   return "USB3 Vision Cameras";
    if (driver == uvc)   return "UVC Cameras";
    if (driver == gige)  return "GigE Vision Cameras";
    return "Unknown";
}

// ── 생성자 ────────────────────────────────────────────────────────────────────
CamSubWindow::CamSubWindow(QWidget* parent)
    : QDockWidget("Device List", parent)
    , m_deviceManager(nullptr)
    , m_listLayout(nullptr)
    , m_selectedWidget(nullptr)
    , m_u3vDriver(nullptr)
    , m_uvcDriver(nullptr)
    , m_gigeDriver(nullptr)
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
    , m_gigeDriver(nullptr)
{
    setupUi();
    refreshDeviceListFromHardware();
}

CamSubWindow::~CamSubWindow()
{
    delete m_u3vDriver;  m_u3vDriver  = nullptr;
    delete m_uvcDriver;  m_uvcDriver  = nullptr;
    delete m_gigeDriver; m_gigeDriver = nullptr;
}

// ── API ───────────────────────────────────────────────────────────────────────
ICameraDriver* CamSubWindow::getDriverFor(const QString& description) const
{
    return m_driverMap.value(description, nullptr);
}

ICameraDriver* CamSubWindow::getCamera() const
{
    if (m_u3vDriver)  return m_u3vDriver;
    if (m_gigeDriver) return m_gigeDriver;
    return m_uvcDriver;
}

void CamSubWindow::stopAllDrivers()
{
    if (m_u3vDriver)  m_u3vDriver->StopAll();
    if (m_gigeDriver) m_gigeDriver->StopAll();
    if (m_uvcDriver)  m_uvcDriver->StopAll();
}

// ── UI 구성 ───────────────────────────────────────────────────────────────────
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

// ── 목록 표시 갱신 ────────────────────────────────────────────────────────────
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
        ICameraDriver* drv = m_driverMap.value(info.serialNumber);
        const QString section = sectionName(drv, m_u3vDriver,
                                             m_uvcDriver, m_gigeDriver);

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

// ── 하드웨어 열거 ────────────────────────────────────────────────────────────
void CamSubWindow::refreshDeviceListFromHardware()
{
    if (!m_deviceManager) return;

    m_driverMap.clear();

    // ── 1. USB3 Vision (libusb + Aravis, WinUSB 드라이버 필요) ───────────
    if (!m_u3vDriver)
        m_u3vDriver = new ArvCameraDriver();
    const auto u3vList = m_u3vDriver->EnumCameras();
    for (const auto& di : u3vList)
        m_driverMap[di.serialNumber] = m_u3vDriver;

    // ── 2. GigE Vision (GVCP/GVSP over UDP, 드라이버 불필요) ─────────────
    if (!m_gigeDriver)
        m_gigeDriver = new GigECameraDriver();

    const auto gigeList = m_gigeDriver->EnumCameras();
    for (const auto& di : gigeList)
        m_driverMap[di.serialNumber] = m_gigeDriver;

    // ── 3. UVC (Qt5 Multimedia, 표준 드라이버) ────────────────────────────
    if (!m_uvcDriver)
        m_uvcDriver = new UvcCameraDriver();
    const auto uvcList = m_uvcDriver->EnumCameras();
    for (const auto& di : uvcList)
        m_driverMap[di.serialNumber] = m_uvcDriver;

    // ── DeviceManager 갱신: U3V → GigE → UVC 순서 ───────────────────────
    QList<DeviceInfo> allDevices;
    allDevices << u3vList << gigeList << uvcList;

    m_deviceManager->clearDeviceList();
    for (const auto& di : allDevices)
        m_deviceManager->addDevice(di);

    refreshDeviceList();

    qDebug("CamSubWindow: %d U3V + %d GigE + %d UVC = %d total",
           u3vList.size(), gigeList.size(),
           uvcList.size(), allDevices.size());
}

// ── 이벤트 핸들러 ─────────────────────────────────────────────────────────────
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

    ICameraDriver* driver = m_driverMap.value(deviceinfo.serialNumber, nullptr);
    if (!driver) {
        qWarning("CamSubWindow: no driver found for [%s]",
                 qPrintable(deviceinfo.serialNumber));
        return;
    }

    emit deviceReadyToLaunch(deviceinfo, driver);
}
