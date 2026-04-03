#pragma once

#include <QDockWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>

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

private slots:
    void onTopLevelChanged(bool floating);

private:
    ImageGraphicsView* m_view;
    QString            m_description;
    bool               m_firstFrame{ true };
};
