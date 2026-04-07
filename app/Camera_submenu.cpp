#include "Camera_submenu.h"
#include "ArvCameraDriver.h"

CamSubWindow::CamSubWindow(QWidget* parent)
    : QDockWidget("Device List", parent)
    , m_deviceManager(nullptr)
    , m_listLayout(nullptr)
    , m_selectedWidget(nullptr)
    , m_idscamera(nullptr)
{
    setupUi();
    refreshDeviceListFromHardware();
}

CamSubWindow::CamSubWindow(DeviceManager* deviceManager, QWidget* parent)
    : QDockWidget("Device List", parent)
    , m_deviceManager(deviceManager)
    , m_listLayout(nullptr)
    , m_selectedWidget(nullptr)
    , m_idscamera(nullptr)
{
    setupUi();
    refreshDeviceListFromHardware();
}

CamSubWindow::~CamSubWindow()
{
    delete m_idscamera;
    m_idscamera = nullptr;
}

void CamSubWindow::setupUi()
{
    QWidget*     content    = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // 도킹 패널 최소/기본 폭 설정
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

    for (const DeviceInfo& info : m_deviceManager->getDeviceList()) {
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

    // ArvCameraDriver: libusb + Aravis protocol
    // No kernel driver install needed - just WinUSB via Zadig
    if (!m_idscamera)
        m_idscamera = new ArvCameraDriver();

    m_deviceManager->enumerateDevices(m_idscamera);
    refreshDeviceList();
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
    if (!m_idscamera) return;
    if (!deviceinfo.isOpenable) return;
    emit deviceReadyToLaunch(deviceinfo, m_idscamera);
}
