#include "ui_Imagingwork.h"
#include <QtCore/QCoreApplication>

// ── UI 초기화 ────────────────────────────────────────────────────────────────
void Ui_ImagingworkClass::setupUi(QMainWindow* parent)
{
    peak::Library::Initialize();
    peak::icv::library::Init();

    if (parent->objectName().isEmpty())
        parent->setObjectName(QString::fromUtf8("ImagingworkClass"));
    parent->resize(1280, 960);
    parent->setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AnimatedDocks);

    // MenuBar
    menuBar = new QMenuBar(parent);
    menuBar->setObjectName(QString::fromUtf8("menuBar"));
    parent->setMenuBar(menuBar);

    // ToolBar
    mainToolBar = new QToolBar(parent);
    mainToolBar->setMovable(false);
    mainToolBar->setObjectName(QString::fromUtf8("mainToolBar"));

    // DeviceManager / CamSubWindow
    m_deviceManager = new DeviceManager(parent);
    m_subWindow     = new CamSubWindow(m_deviceManager);
    parent->addDockWidget(Qt::LeftDockWidgetArea, m_subWindow);
    m_subWindow->hide();

    // CamButton
    camButton = new IconLabel(parent);
    QPixmap pixmap(":/Imagingwork/icons/camera.png");
    camButton->setPixmap(
        pixmap.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    mainToolBar->addWidget(camButton);
    parent->addToolBar(mainToolBar);

    // CentralWidget
    centralWidget = new QWidget(parent);
    centralWidget->setObjectName(QString::fromUtf8("centralWidget"));
    centralWidget->setStyleSheet("background: #1e1e1e;");
    parent->setCentralWidget(centralWidget);

    // StatusBar
    statusBar = new QStatusBar(parent);
    statusBar->setObjectName(QString::fromUtf8("statusBar"));
    parent->setStatusBar(statusBar);

    retranslateUi(parent);
    QMetaObject::connectSlotsByName(parent);
}

// ── 텍스트 갱신 ──────────────────────────────────────────────────────────────
void Ui_ImagingworkClass::retranslateUi(QMainWindow* parent)
{
    parent->setWindowTitle(
        QCoreApplication::translate("ImagingworkClass", "Imagingwork", nullptr));
}

// ── 뷰어 관리 ────────────────────────────────────────────────────────────────
void Ui_ImagingworkClass::addImageViewer(QMainWindow* mainWindow, const QString& title)
{
    QString viewerTitle = title.isEmpty()
        ? QString("Viewer %1").arg(++m_viewerCount)
        : title;

    for (auto* viewer : m_imageViewers) {
        if (viewer && viewer->windowTitle() == viewerTitle) {
            viewer->show(); viewer->raise(); viewer->activateWindow();
            return;
        }
    }
    createDock(mainWindow, viewerTitle, "");
}

void Ui_ImagingworkClass::setImage(int index, const QPixmap& pixmap) {
    if (index >= 0 && index < m_imageViewers.size())
        m_imageViewers[index]->setImage(pixmap);
}

void Ui_ImagingworkClass::setImage(int index, const QString& filePath) {
    if (index >= 0 && index < m_imageViewers.size())
        m_imageViewers[index]->setImage(filePath);
}

void Ui_ImagingworkClass::setImage(int index, QImage image) {
    if (index >= 0 && index < m_imageViewers.size())
        m_imageViewers[index]->UpdateImageViewer(std::move(image));
}

void Ui_ImagingworkClass::closeAllSubWindows() {
    if (m_subWindow) m_subWindow->close();
    for (auto* viewer : m_imageViewers)
        if (viewer) viewer->close();
}

// ── 내부 헬퍼 ────────────────────────────────────────────────────────────────
ImageViewerDock* Ui_ImagingworkClass::getOrCreateViewer(QMainWindow* mainWindow,
                                                         const DeviceInfo& deviceinfo)
{
    for (auto* viewer : m_imageViewers) {
        if (viewer
            && viewer->windowTitle() == deviceinfo.name
            && viewer->description() == deviceinfo.description) {
            viewer->show(); viewer->raise(); viewer->activateWindow();
            return viewer;
        }
    }
    return createDock(mainWindow, deviceinfo.name, deviceinfo.description);
}

ImageViewerDock* Ui_ImagingworkClass::createDock(QMainWindow* mainWindow,
                                                   const QString& name,
                                                   const QString& description)
{
    auto* dock = new ImageViewerDock(name, description, mainWindow);

    if (m_imageViewers.isEmpty()) {
        mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);

        int camWidth = (m_subWindow
                        && !m_subWindow->isHidden()
                        && !m_subWindow->isFloating())
                       ? m_subWindow->width() : 0;

        int availableWidth  = mainWindow->width() - camWidth;
        int availableHeight = mainWindow->height()
            - (mainWindow->menuBar()   ? mainWindow->menuBar()->height()   : 0)
            - (mainWindow->statusBar() ? mainWindow->statusBar()->height() : 0)
            - (mainWindow->findChild<QToolBar*>()
               ? mainWindow->findChild<QToolBar*>()->height() : 0);

        mainWindow->resizeDocks({ dock }, { availableWidth },  Qt::Horizontal);
        mainWindow->resizeDocks({ dock }, { availableHeight }, Qt::Vertical);
    }
    else {
        mainWindow->tabifyDockWidget(m_imageViewers.first(), dock);
    }

    QObject::connect(dock, &QObject::destroyed,
        mainWindow, [this, dock]() {
            m_imageViewers.removeOne(dock);
        });

    dock->show();
    dock->raise();
    m_imageViewers.append(dock);
    return dock;
}
