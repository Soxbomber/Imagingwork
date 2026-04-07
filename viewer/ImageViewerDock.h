#pragma once

#include <QDockWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>
#include <QCloseEvent>

// ── ImageGraphicsView ──────────────────────────────────────────
class ImageGraphicsView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageGraphicsView(QWidget* parent = nullptr);
    void setImage(const QPixmap& pixmap);
    void setImage(const QString& filePath);
    void fitImageInView();
    void resetZoom();

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
    QString description() const;

public slots:
    void UpdateImageViewer(QImage image);

signals:
    // 창이 닫힐 때 description을 전달 → 외부에서 카메라 정지 처리
    void viewerClosed(const QString& description);

protected:
    // X 버튼 클릭 시 viewerClosed 시그널 emit 후 닫기
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onTopLevelChanged(bool floating);

private:
    ImageGraphicsView* m_view;
    QString            m_description;
    bool               m_firstFrame{ true };
};
