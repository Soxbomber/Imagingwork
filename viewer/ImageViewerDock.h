#pragma once

#include <QDockWidget>
#include <atomic>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>
#include <QCloseEvent>
#include <QPixmapCache>

// ── ImageGraphicsView ──────────────────────────────────────────
class ImageGraphicsView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageGraphicsView(QWidget* parent = nullptr);
    void setImage(const QPixmap& pixmap);
    void setImage(const QString& filePath);
    void fitImageInView();
    void resetZoom();
    void clearImage();  // scene 초기화 + pixmap 메모리 해제

public slots:
    void updateImage(QImage image);

protected:
    void wheelEvent(QWheelEvent* event)        override;
    void mousePressEvent(QMouseEvent* event)   override;
    void mouseMoveEvent(QMouseEvent* event)    override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event)      override;

private:
    QGraphicsScene*      m_scene;
    QGraphicsPixmapItem* m_pixmapItem;
    bool                 m_isPanning;
    QPoint               m_lastPanPoint;
};

// ── ImageViewerDock ────────────────────────────────────────────
class ImageViewerDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ImageViewerDock(const QString& title,
                             const QString& description,
                             QWidget* parent = nullptr);
    void    setImage(const QPixmap& pixmap);
    void    setImage(const QString& filePath);
    void    fitImageInView();
    void    resetZoom();
    void    clearImage();       // 이전 이미지 즉시 제거 및 메모리 해제
    void    acceptFrames();     // clearImage() 후 새 프레임 수신 재개
    QString serialnumber() const;

public slots:
    void UpdateImageViewer(QImage image);

signals:
    void viewerClosed(const QString& description);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onTopLevelChanged(bool floating);

private:
    ImageGraphicsView* m_view;
    QString            m_description;
    bool               m_firstFrame{ true };
    bool               m_accepting { true };  // false이면 UpdateImageViewer 무시
    std::atomic<bool>  m_rendering { false }; // QPixmap 렌더링 중 drop flag
};
