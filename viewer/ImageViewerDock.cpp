#include "ImageViewerDock.h"
#include <QPixmapCache>

// ============================================================
// ImageGraphicsView
// ============================================================
ImageGraphicsView::ImageGraphicsView(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_pixmapItem(nullptr)
    , m_isPanning(false)
{
    setScene(m_scene);

    // 실시간 스트리밍 최적화:
    // Antialiasing, SmoothPixmapTransform 비활성화 → CPU 절약
    // (고화질 모드는 정지 영상에서만 필요)
    setRenderHint(QPainter::Antialiasing,           false);
    setRenderHint(QPainter::SmoothPixmapTransform,  false);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(QBrush(QColor(30, 30, 30)));
    setStyleSheet("border: none;");

    // MinimalViewportUpdate: 변경된 영역만 repaint → paintEvent 최소화
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
}

void ImageGraphicsView::setImage(const QPixmap& pixmap)
{
    if (pixmap.isNull()) return;
    if (!m_pixmapItem) {
        m_pixmapItem = m_scene->addPixmap(pixmap);
        // 실시간 중 FastTransformation: 축소 시 bilinear 대신 nearest-neighbor
        m_pixmapItem->setTransformationMode(Qt::FastTransformation);
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
    // Format_ARGB32 → QPixmap: Qt native format이므로 변환 없음
    setImage(QPixmap::fromImage(std::move(image)));
}

void ImageGraphicsView::clearImage()
{
    // scene->clear()로 pixmapItem을 완전히 소멸시켜 QPixmap 공유 데이터까지 해제
    m_scene->clear();
    m_pixmapItem = nullptr;      // scene이 소유권 가졌다가 해제했으므로 nullptr 처리
    m_scene->setSceneRect(QRectF());

    // Qt 내부 pixmap 캐시도 정리
    QPixmapCache::clear();
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
    if (event->button() == Qt::LeftButton ||
        event->button() == Qt::MiddleButton) {
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
        horizontalScrollBar()->setValue(
            horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(
            verticalScrollBar()->value() - delta.y());
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

// ============================================================
// ImageViewerDock
// ============================================================
ImageViewerDock::ImageViewerDock(const QString& title,
                                 const QString& description,
                                 QWidget* parent)
    : QDockWidget(title, parent)
    , m_view(new ImageGraphicsView(this))
    , m_description(description)
    , m_firstFrame(true)
{
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(QDockWidget::DockWidgetMovable   |
                QDockWidget::DockWidgetFloatable |
                QDockWidget::DockWidgetClosable);
    setMinimumSize(300, 200);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWidget(m_view);

    connect(this, &QDockWidget::topLevelChanged,
            this, &ImageViewerDock::onTopLevelChanged);
}

void ImageViewerDock::setImage(const QPixmap& p)   { m_view->setImage(p); }
void ImageViewerDock::setImage(const QString& f)   { m_view->setImage(f); }
void ImageViewerDock::fitImageInView()              { m_view->fitImageInView(); }
void ImageViewerDock::resetZoom()                  { m_view->resetZoom(); }
void ImageViewerDock::clearImage()
{
    // 수신 거부 플래그 설정 → 큐에 남은 프레임 무시
    m_accepting = false;
    m_view->clearImage();
    m_firstFrame = true;
}

void ImageViewerDock::acceptFrames()
{
    m_accepting = true;
}
QString ImageViewerDock::serialnumber() const        { return m_description; }

void ImageViewerDock::onTopLevelChanged(bool floating)
{
    if (floating) { setMinimumSize(400,300); resize(800,600); }
    else { setMinimumSize(300,200);
           setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX); }
}

void ImageViewerDock::UpdateImageViewer(QImage image)
{
    // clearImage() 이후 큐에 남아있던 이전 프레임 차단
    if (!m_accepting || image.isNull()) return;

    // QPixmap::fromImage(5MP BGRA) = ~3-5ms (GPU 업로드)
    // UI가 이전 프레임 처리 중이면 drop → QueuedConnection 큐 누적 방지
    bool expected = false;
    if (!m_rendering.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;  // 이전 프레임 렌더링 중 → drop

    if (m_firstFrame ||
        m_view->sceneRect().size().toSize() != image.size()) {
        m_view->setSceneRect(image.rect());
        m_view->fitInView(image.rect(), Qt::KeepAspectRatio);
        m_firstFrame = false;
    }

    m_view->updateImage(std::move(image));
    m_rendering.store(false, std::memory_order_release);
}

void ImageViewerDock::closeEvent(QCloseEvent* event)
{
    emit viewerClosed(m_description);
    QDockWidget::closeEvent(event);
}
