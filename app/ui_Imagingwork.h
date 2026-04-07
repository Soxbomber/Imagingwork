/********************************************************************************
** Form generated from reading UI file 'Imagingwork.ui'
**
** Created by: Qt User Interface Compiler version 5.15.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_IMAGINGWORK_H
#define UI_IMAGINGWORK_H

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QWidget>
#include "Iconlabel.h"
#include "devicemanager.h"
#include "Camera_submenu.h"
#include "ImageViewerDock.h"

QT_BEGIN_NAMESPACE

class Ui_ImagingworkClass
{
public:
    // ── 위젯 멤버 ────────────────────────────────────────────────
    QMenuBar*      menuBar;
    QToolBar*      mainToolBar;
    QWidget*       centralWidget;
    QStatusBar*    statusBar;
    IconLabel*     camButton;
    DeviceManager* m_deviceManager;
    CamSubWindow*  m_subWindow;

    // ── 뷰어 목록 ────────────────────────────────────────────────
    QList<ImageViewerDock*> m_imageViewers;
    int m_viewerCount = 0;

    // ── UI 초기화 (위젯 생성 및 배치만 담당) ─────────────────────
    void setupUi(QMainWindow* parent);

    // ── 뷰어 관리 API ────────────────────────────────────────────
    void addImageViewer(QMainWindow* mainWindow, const QString& title = "");
    void setImage(int index, const QPixmap& pixmap);
    void setImage(int index, const QString& filePath);
    void setImage(int index, QImage image);
    void closeAllSubWindows();

    // ── 텍스트 갱신 ──────────────────────────────────────────────
    void retranslateUi(QMainWindow* parent);

    // ── 내부 헬퍼 (Imagingwork에서도 접근) ───────────────────────
    ImageViewerDock* getOrCreateViewer(QMainWindow* mainWindow,
                                       const DeviceInfo& deviceinfo);

private:
    ImageViewerDock* createDock(QMainWindow* mainWindow,
                                const QString& name,
                                const QString& description);
};

namespace Ui {
    class ImagingworkClass : public Ui_ImagingworkClass {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_IMAGINGWORK_H
