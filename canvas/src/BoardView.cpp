#include "glacier/canvas/BoardView.h"

#include "glacier/canvas/BoardScene.h"

#include <QColor>
#include <QGraphicsItem>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace glacier::canvas {

namespace {

const QColor DefaultBackground(0x3a, 0x3d, 0x41);
const QColor OutlineDark(0x10, 0x14, 0x18);
const QColor OutlineLight(0x7f, 0xd4, 0xff);
constexpr double FitMargin = 0.05;
constexpr double DragThreshold = 3.0;

bool acceptsMime(const QMimeData *mime)
{
    return mime && (mime->hasUrls() || mime->hasImage() || mime->hasText());
}

} // namespace

BoardView::BoardView(QWidget *parent)
    : QGraphicsView(parent)
{
    setBackgroundBrush(DefaultBackground);
    setFrameShape(QFrame::NoFrame);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    // Foreground overlays (selection, rubber band) are drawn in viewport
    // coordinates, so scrolling must repaint instead of blitting.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setDragMode(QGraphicsView::NoDrag);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->grabGesture(Qt::PinchGesture);
}

void BoardView::setBoardScene(BoardScene *scene)
{
    if (m_scene)
        disconnect(m_scene, nullptr, this, nullptr);
    m_scene = scene;
    setScene(scene);
    if (scene) {
        connect(scene, &QGraphicsScene::selectionChanged, viewport(), qOverload<>(&QWidget::update));
        centerOn(0, 0);
    }
}

double BoardView::zoom() const
{
    return transform().m11();
}

void BoardView::setZoom(double target, QPointF viewportAnchor)
{
    if (!std::isfinite(target))
        return;
    target = std::clamp(target, MinZoom, MaxZoom);
    const QPointF sceneAnchor = mapToScene(viewportAnchor.toPoint());
    setTransform(QTransform::fromScale(target, target));
    // Keep the anchor's scene point under the same viewport position.
    const QPointF moved = mapToScene(viewportAnchor.toPoint());
    const QPointF centre = mapToScene(viewport()->rect().center());
    centerOn(centre + (sceneAnchor - moved));
}

void BoardView::zoomBy(double factor, QPointF viewportAnchor)
{
    setZoom(zoom() * factor, viewportAnchor);
}

void BoardView::zoomIn()
{
    zoomBy(ButtonStep, viewport()->rect().center());
}

void BoardView::zoomOut()
{
    zoomBy(1.0 / ButtonStep, viewport()->rect().center());
}

void BoardView::resetZoom()
{
    setZoom(1.0, viewport()->rect().center());
}

QPointF BoardView::viewCenter() const
{
    return mapToScene(viewport()->rect().center());
}

void BoardView::fitRect(const QRectF &sceneRect)
{
    if (!sceneRect.isValid() || sceneRect.isEmpty())
        return;
    const QSizeF available = QSizeF(viewport()->size()) * (1.0 - 2 * FitMargin);
    const double target = std::min(available.width() / sceneRect.width(), available.height() / sceneRect.height());
    setTransform(QTransform::fromScale(std::clamp(target, MinZoom, MaxZoom), std::clamp(target, MinZoom, MaxZoom)));
    centerOn(sceneRect.center());
}

void BoardView::fitBoard()
{
    const QRectF content = m_scene ? m_scene->contentBounds() : QRectF();
    if (content.isNull()) {
        setTransform(QTransform());
        centerOn(0, 0);
        return;
    }
    fitRect(content);
}

void BoardView::fitSelection()
{
    if (m_scene)
        fitRect(m_scene->selectionBounds());
}

void BoardView::panBy(QPointF viewportDelta)
{
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - int(std::lround(viewportDelta.x())));
    verticalScrollBar()->setValue(verticalScrollBar()->value() - int(std::lround(viewportDelta.y())));
}

void BoardView::wheelEvent(QWheelEvent *event)
{
    const bool touchpad = event->pointingDevice()
                       && event->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad;
    if (touchpad && !(event->modifiers() & Qt::ControlModifier)) {
        panBy(event->pixelDelta().isNull() ? QPointF(event->angleDelta()) / 8.0 : QPointF(event->pixelDelta()));
        event->accept();
        return;
    }
    const double steps = event->angleDelta().y() / 120.0;
    if (steps != 0.0)
        zoomBy(std::pow(WheelStep, steps), event->position());
    event->accept();
}

bool BoardView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            zoomBy(1.0 + gesture->value(), mapFromGlobal(gesture->globalPosition().toPoint()));
            return true;
        }
    }
    return QGraphicsView::viewportEvent(event);
}

void BoardView::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld)) {
        m_gesture = Gesture::Pan;
        m_lastPos = pos;
        updateCursor();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton || !m_scene) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    const bool shift = event->modifiers() & Qt::ShiftModifier;
    QGraphicsItem *hit = itemAt(pos.toPoint());
    if (hit) {
        if (shift) {
            hit->setSelected(!hit->isSelected());
        } else if (!hit->isSelected()) {
            m_scene->clearSelection();
            hit->setSelected(true);
        }
        event->accept();
        return;
    }
    m_gesture = Gesture::RubberBand;
    m_rubberOrigin = pos;
    m_rubberCurrent = pos;
    m_rubberAdditive = shift;
    event->accept();
}

void BoardView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    switch (m_gesture) {
    case Gesture::Pan:
        panBy(pos - m_lastPos);
        m_lastPos = pos;
        event->accept();
        return;
    case Gesture::RubberBand:
        m_rubberCurrent = pos;
        viewport()->update();
        event->accept();
        return;
    case Gesture::None:
        break;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void BoardView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_gesture == Gesture::Pan && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_gesture = Gesture::None;
        updateCursor();
        event->accept();
        return;
    }
    if (m_gesture == Gesture::RubberBand && event->button() == Qt::LeftButton) {
        m_gesture = Gesture::None;
        const QRectF band = rubberBandRect();
        if (m_scene) {
            if (band.width() < DragThreshold && band.height() < DragThreshold) {
                if (!m_rubberAdditive)
                    m_scene->clearSelection(); // click on empty canvas
            } else {
                QPainterPath path;
                path.addPolygon(mapToScene(band.toAlignedRect()));
                m_scene->setSelectionArea(path,
                                          m_rubberAdditive ? Qt::AddToSelection : Qt::ReplaceSelection,
                                          Qt::IntersectsItemShape, viewportTransform());
            }
        }
        viewport()->update();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void BoardView::mouseDoubleClickEvent(QMouseEvent *event)
{
    event->accept(); // reserved for group/note editing (M2)
}

void BoardView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        updateCursor();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_gesture == Gesture::RubberBand) {
        m_gesture = Gesture::None;
        viewport()->update();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void BoardView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        updateCursor();
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void BoardView::focusOutEvent(QFocusEvent *event)
{
    m_spaceHeld = false;
    if (m_gesture == Gesture::Pan)
        m_gesture = Gesture::None;
    updateCursor();
    QGraphicsView::focusOutEvent(event);
}

void BoardView::updateCursor()
{
    if (m_gesture == Gesture::Pan)
        viewport()->setCursor(Qt::ClosedHandCursor);
    else if (m_spaceHeld)
        viewport()->setCursor(Qt::OpenHandCursor);
    else
        viewport()->unsetCursor();
}

QRectF BoardView::rubberBandRect() const
{
    return QRectF(m_rubberOrigin, m_rubberCurrent).normalized();
}

void BoardView::dragEnterEvent(QDragEnterEvent *event)
{
    if (acceptsMime(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void BoardView::dragMoveEvent(QDragMoveEvent *event)
{
    if (acceptsMime(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void BoardView::dropEvent(QDropEvent *event)
{
    if (!acceptsMime(event->mimeData())) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    emit dropReceived(event->mimeData(), mapToScene(event->position().toPoint()));
}

void BoardView::drawForeground(QPainter *painter, const QRectF &)
{
    if (!m_scene)
        return;
    painter->save();
    painter->resetTransform(); // viewport coordinates: constant on-screen width
    painter->setRenderHint(QPainter::Antialiasing, false);

    const auto selected = m_scene->selectedItems();
    for (const QGraphicsItem *item : selected) {
        const QPolygonF outline = mapFromScene(item->sceneBoundingRect());
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(OutlineDark, 4));
        painter->drawPolygon(outline);
        painter->setPen(QPen(OutlineLight, 2));
        painter->drawPolygon(outline);
    }

    if (m_gesture == Gesture::RubberBand) {
        const QRectF band = rubberBandRect();
        painter->setPen(QPen(OutlineLight, 1, Qt::DashLine));
        painter->setBrush(QColor(0x7f, 0xd4, 0xff, 40));
        painter->drawRect(band);
    }
    painter->restore();
}

} // namespace glacier::canvas
