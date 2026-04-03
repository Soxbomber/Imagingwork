#include "Camera_submenu.h"

CamSubWindow::CamSubWindow(QWidget* parent)
    : QDockWidget("Device List", parent)
    , m_deviceManager(nullptr)
    , m_listLayout(nullptr)
    , m_selectedWidget(nullptr)
    , m_idscamera(nullptr)
{
    setupDock();
    setupUI();
}

CamSubWindow::CamSubWindow(DeviceManager* deviceManager, QWidget* parent)
    : QDockWidget("Device List", parent)
    , m_deviceManager(deviceManager)
    , m_listLayout(nullptr)
    , m_selectedWidget(nullptr)
    , m_idscamera(nullptr)
{
    setupDock();
    setupUI();
    refreshDeviceListFromHardware();

    connect(m_deviceManager, &DeviceManager::deviceConnectionChanged,
        this, [this](const DeviceInfo&) {
            m_selectedWidget = nullptr;
            refreshDeviceList();
        });
}

CamSubWindow::~CamSubWindow() {
    delete m_idscamera;
    m_idscamera = nullptr;
}

void CamSubWindow::setDeviceManager(DeviceManager* deviceManager) {
    m_deviceManager = deviceManager;
    refreshDeviceList();
}

void CamSubWindow::setupDock() {
    setAllowedAreas(
        Qt::LeftDockWidgetArea |
        Qt::RightDockWidgetArea |
        Qt::BottomDockWidgetArea
    );
    setFeatures(
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable |
        QDockWidget::DockWidgetClosable
    );
    setMinimumWidth(280);

    connect(this, &QDockWidget::topLevelChanged,
        this, &CamSubWindow::onTopLevelChanged);
}

void CamSubWindow::setupUI() {
    QWidget* container = new QWidget(this);
    container->setStyleSheet("QWidget { background: #cbcbcb; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* titleLabel = new QLabel("Available devices", container);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");

    QPushButton* refreshBtn = new QPushButton("refresh", container);
    refreshBtn->setFixedWidth(80);
    refreshBtn->setStyleSheet(R"(
        QPushButton {
            border: 1px solid #ddd;
            border-radius: 4px;
            padding: 6px;
            font-size: 12px;
            background: white;
        }
        QPushButton:hover { background: #f0f0f0; }
    )");
    connect(refreshBtn, &QPushButton::clicked,
        this, &CamSubWindow::refreshDeviceListFromHardware);

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(refreshBtn);

    QLabel* hintLabel = new QLabel("double click on a device to open.", container);
    hintLabel->setStyleSheet("font-size: 11px; color: #44a;");

    QScrollArea* scrollArea = new QScrollArea(container);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(R"(
        QScrollArea {
            border: 1px solid #eee;
            border-radius: 6px;
            background: #fafafa;
        }
    )");

    QWidget* listContainer = new QWidget();
    listContainer->setStyleSheet("QWidget { background: #fafafa; border: none; }");

    m_listLayout = new QVBoxLayout(listContainer);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(0);
    m_listLayout->addStretch();

    listContainer->setLayout(m_listLayout);
    scrollArea->setWidget(listContainer);

    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(hintLabel);
    mainLayout->addWidget(scrollArea);

    container->setLayout(mainLayout);
    setWidget(container);
}

void CamSubWindow::refreshDeviceList() {
    if (!m_deviceManager) return;

    m_selectedWidget = nullptr;

    QLayoutItem* child;
    while ((child = m_listLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->hide();
            child->widget()->deleteLater();
        }
        delete child;
    }

    QList<DeviceInfo> devicelist = m_deviceManager->getDeviceList();

    if (devicelist.isEmpty()) {
        QLabel* emptyLabel = new QLabel("No device detected.");
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("color: #aaa; font-size: 13px;");
        emptyLabel->setFixedHeight(60);
        m_listLayout->addWidget(emptyLabel);
    }
    else {
        for (const DeviceInfo& device : devicelist) {
            DeviceItemWidget* widget = new DeviceItemWidget(device);

            connect(widget, &DeviceItemWidget::clicked,
                this, &CamSubWindow::onDeviceClicked);
            connect(widget, &DeviceItemWidget::doubleClicked,
                this, &CamSubWindow::onDeviceDoubleClicked);

            m_listLayout->addWidget(widget);
        }
    }

    m_listLayout->addStretch();
}

void CamSubWindow::refreshDeviceListFromHardware() {
    if (!m_deviceManager) return;

    if (!m_idscamera)
        m_idscamera = new IdsCamera();

    m_deviceManager->enumerateDevices(m_idscamera);
    refreshDeviceList();
}

void CamSubWindow::onDeviceClicked(DeviceItemWidget* selected) {
    if (m_selectedWidget && m_selectedWidget != selected) {
        bool exists = false;
        for (int i = 0; i < m_listLayout->count(); i++) {
            QLayoutItem* item = m_listLayout->itemAt(i);
            if (item && item->widget() == m_selectedWidget) {
                exists = true;
                break;
            }
        }
        if (exists)
            m_selectedWidget->setSelected(false);
    }
    m_selectedWidget = selected;
}

void CamSubWindow::onDeviceDoubleClicked(const DeviceInfo& deviceinfo) {
    if (!deviceinfo.isOpenable) {
        QMessageBox::warning(this, "Connection Error",
            QString("Can't access device"));
        return;
    }
    if (!m_idscamera) {
        QMessageBox::warning(this, "Error",
            QString("Camera not initialized."));
        return;
    }

    // [FIX] StartGrabbing을 여기서 직접 호출하지 않는다.
    //       dock이 아직 존재하지 않아 시그널 연결이 불가능하기 때문.
    //       대신 ui_Imagingwork 쪽으로 dock 생성 + StartGrabbing 위임.
    emit deviceReadyToLaunch(deviceinfo, m_idscamera);
}

void CamSubWindow::onTopLevelChanged(bool floating) {
    if (floating) {
        setFixedSize(420, 400);
    }
    else {
        setMinimumSize(200, 100);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }
}
