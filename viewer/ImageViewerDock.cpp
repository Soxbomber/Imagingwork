#include "ImageViewerDock.h"

ImageGraphicsView::ImageGraphicsView(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_pixmapItem(nullptr)
    , m_isPanning(false)
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(QBrush(QColor(30, 30, 30)));
    setStyleSheet("border: none;");
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
}

void ImageGraphicsView::setImage(const QPixmap& pixmap)
{
    if (pixmap.isNull()) return;
    if (!m_pixmapItem) {
        m_pixmapItem = m_scene->addPixmap(pixmap);
        m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
        m_scene->setSceneRect(m_pixmapItem->boundingRect());
        fitImageInView();
    } else {
        m_pixmapItem->setPixmap(pixmap);
        if (m_scene->sceneRect() != m_pixmapItem->boundingRect())
            m_scene->setSceneRect(m_pixmapItem->boundingRect());
    }
}

void ImageGraphicsView::setImage(const QString& filePath)
{
    QPixmap pixmap(filePath);
    if (!pixmap.isNull()) setImage(pixmap);
}

void ImageGraphicsView::updateImage(QImage image)
{
    if (image.isNull()) return;
    setImage(QPixmap::fromImage(std::move(image)));
}

void ImageGraphicsView::fitImageInView()
{
    if (m_pixmapItem)
        fitInView(m_pixmapItem, Qt::KeepAspectRatio);
}

void ImageGraphicsView::resetZoom()
{
    resetTransform();
    fitImageInView();
}

void ImageGraphicsView::wheelEvent(QWheelEvent* event)
{
    const double factor = 1.15;
    if (event->angleDelta().y() > 0) scale(factor, factor);
    else                              scale(1.0 / factor, 1.0 / factor);
}

void ImageGraphicsView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        m_isPanning    = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning) {
        QPoint delta   = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageGraphicsView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    fitImageInView();
}

ImageViewerDock::ImageViewerDock(const QString& title,
                                 const QString& description,
                                 QWidget* parent)
    : QDockWidget(title, parent)
    , m_view(new ImageGraphicsView(this))
    , m_description(description)
    , m_firstFrame(true)
{
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(
        QDockWidget::DockWidgetMovable   |
        QDockWidget::DockWidgetFloatable |
        QDockWidget::DockWidgetClosable
    );
    setMinimumSize(300, 200);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWidget(m_view);

    connect(this, &QDockWidget::topLevelChanged,
            this, &ImageViewerDock::onTopLevelChanged);
}

void ImageViewerDock::setImage(const QPixmap& pixmap)   { m_view->setImage(pixmap); }
void ImageViewerDock::setImage(const QString& filePath) { m_view->setImage(filePath); }
void ImageViewerDock::fitImageInView()                  { m_view->fitImageInView(); }
void ImageViewerDock::resetZoom()                       { m_view->resetZoom(); }
QString ImageViewerDock::description() const            { return m_description; }

void ImageViewerDock::onTopLevelChanged(bool floating)
{
    if (floating) { setMinimumSize(400, 300); resize(800, 600); }
    else          { setMinimumSize(300, 200); setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX); }
}

void ImageViewerDock::UpdateImageViewer(QImage image)
{
    if (image.isNull()) return;

    if (m_firstFrame || m_view->sceneRect().size().toSize() != image.size()) {
        m_view->setSceneRect(image.rect());
        m_view->fitInView(image.rect(), Qt::KeepAspectRatio);
        m_firstFrame = false;
    }

    m_view->updateImage(std::move(image));
}

void ImageViewerDock::closeEvent(QCloseEvent* event)
{
    // 카메라 정지 요청 시그널 emit — description으로 어떤 카메라인지 식별
    emit viewerClosed(m_description);

    // 부모 클래스의 closeEvent 호출 → WA_DeleteOnClose에 의해 위젯 소멸
    QDockWidget::closeEvent(event);
}
