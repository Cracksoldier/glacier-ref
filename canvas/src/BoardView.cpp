#include "glacier/canvas/BoardView.h"

#include "glacier/editing/CommandController.h"
#include "glacier/editing/NoteLayout.h"

#include <QColor>
#include <QContextMenuEvent>
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

using namespace Qt::StringLiterals;
using namespace glacier::editing;

namespace glacier::canvas {

namespace {

const QColor OutlineDark(0x10, 0x14, 0x18);
const QColor OutlineLight(0x7f, 0xd4, 0xff);
const QColor LockedColor(0xff, 0xb3, 0x47);
constexpr double FitMargin = 0.05;
constexpr double DragThreshold = 3.0;
constexpr double MinCrop = 0.01;

bool acceptsMime(const QMimeData *mime)
{
    return mime && (mime->hasUrls() || mime->hasImage() || mime->hasText());
}

void drawOutline(QPainter *painter, const QPolygonF &polygon, const QColor &light)
{
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(OutlineDark, 4));
    painter->drawPolygon(polygon);
    painter->setPen(QPen(light, 2));
    painter->drawPolygon(polygon);
}

void drawHandle(QPainter *painter, QPointF centre, bool round = false)
{
    const double s = TransformTool::HandleSize;
    const QRectF r(centre.x() - s / 2, centre.y() - s / 2, s, s);
    painter->setPen(QPen(OutlineDark, 1.5));
    painter->setBrush(Qt::white);
    if (round)
        painter->drawEllipse(r);
    else
        painter->drawRect(r);
}

} // namespace

BoardView::BoardView(QWidget *parent)
    : QGraphicsView(parent)
{
    setBackgroundBrush(QColor::fromRgba(ViewSettings{}.backgroundColor));
    setFrameShape(QFrame::NoFrame);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    // Foreground overlays are drawn in viewport coordinates, so scrolling must
    // repaint instead of blitting.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setDragMode(QGraphicsView::NoDrag);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->grabGesture(Qt::PinchGesture);
    m_toastTimer.setSingleShot(true);
    connect(&m_toastTimer, &QTimer::timeout, this, [this] {
        m_toast.clear();
        viewport()->update();
    });
}

void BoardView::setBoardScene(BoardScene *scene)
{
    if (m_scene)
        disconnect(m_scene, nullptr, this, nullptr);
    m_scene = scene;
    setScene(scene);
    if (scene) {
        connect(scene, &QGraphicsScene::selectionChanged, viewport(), qOverload<>(&QWidget::update));
        connect(scene, &QGraphicsScene::changed, viewport(), qOverload<>(&QWidget::update));
        connect(scene, &BoardScene::viewSettingsChanged, this, &BoardView::applyViewSettings);
        connect(scene, &BoardScene::activeGroupChanged, this, &BoardView::modeChanged);
        if (BoardDocument *doc = scene->document()) {
            // Edits from elsewhere (undo, another action) may remove what this
            // view is working on; never keep pointers to deleted items.
            connect(doc, &BoardDocument::changed, this, [this](const DocumentDelta &delta) {
                // The scene may already have deleted the item: compare IDs only.
                if (m_editingNote && !m_editingNew && delta.removed.contains(m_editingNoteId)) {
                    m_editingNote = nullptr;
                    m_editingNoteId = Id();
                    emit modeChanged();
                }
                if (isCropping() && delta.removed.contains(m_cropId)) {
                    m_cropId = Id();
                    m_cropDrag = CropDrag::None;
                    emit modeChanged();
                }
            });
        }
        applyViewSettings();
        centerOn(0, 0);
    }
}

void BoardView::applyViewSettings()
{
    if (m_scene && m_scene->document())
        setBackgroundBrush(QColor::fromRgba(m_scene->document()->view().backgroundColor));
}

// --- Navigation ----------------------------------------------------------------

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

QPointF BoardView::cursorScenePos() const
{
    if (viewport()->rect().contains(m_lastCursor.toPoint()))
        return mapToScene(m_lastCursor.toPoint());
    return viewCenter();
}

void BoardView::fitRect(const QRectF &sceneRect)
{
    if (!sceneRect.isValid() || sceneRect.isEmpty())
        return;
    const QSizeF available = QSizeF(viewport()->size()) * (1.0 - 2 * FitMargin);
    const double target = std::clamp(std::min(available.width() / sceneRect.width(),
                                              available.height() / sceneRect.height()),
                                     MinZoom, MaxZoom);
    setTransform(QTransform::fromScale(target, target));
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

// --- Edits -----------------------------------------------------------------------

bool BoardView::runEdit(const OpResult &result, const QString &text)
{
    if (!m_controller)
        return false;
    QString error;
    const bool ok = m_controller->run(result, text, &error);
    if (!ok && !error.isEmpty())
        showToast(error);
    else if (ok && result.skippedLocked > 0)
        showToast(tr("%n locked item(s) were not changed.", nullptr, result.skippedLocked));
    return ok;
}

void BoardView::showToast(const QString &text, int durationMs)
{
    m_toast = text;
    m_toastTimer.start(durationMs);
    viewport()->update();
}

// --- Hit testing -------------------------------------------------------------------

BoardItem *BoardView::unitAt(QPointF viewportPos) const
{
    const auto hits = items(viewportPos.toPoint());
    for (QGraphicsItem *hit : hits) {
        if (hit->type() == GroupItem::Type)
            continue; // groups are hit through their children
        for (QGraphicsItem *it = hit; it; it = it->parentItem()) {
            auto *board = dynamic_cast<BoardItem *>(it);
            if (board && board->flags().testFlag(QGraphicsItem::ItemIsSelectable))
                return board;
        }
    }
    return nullptr;
}

// --- Mouse -------------------------------------------------------------------------

void BoardView::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    m_pressPos = pos;
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

    // Text editing: clicks inside the note go to the editor.
    if (m_editingNote) {
        const QPointF local = m_editingNote->mapFromScene(mapToScene(pos.toPoint()));
        if (m_editingNote->localRect().contains(local)) {
            QGraphicsView::mousePressEvent(event);
            return;
        }
        commitTextEdit();
        event->accept();
        return;
    }

    // Crop mode.
    if (isCropping()) {
        const CropDrag drag = hitCrop(pos);
        if (drag == CropDrag::None) {
            commitCrop();
        } else {
            m_cropDrag = drag;
            m_cropStartRect = m_cropRect;
            if (ImageItem *item = m_scene->imageItem(m_cropId))
                m_cropPressLocal = item->mapFromScene(mapToScene(pos.toPoint()));
        }
        event->accept();
        return;
    }

    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const QList<Id> selected = m_scene->selectedIds();
    const TransformTool::Frame frame = TransformTool::frameFor(*m_scene, selected);
    // Handles win over Shift-click: Shift modifies resizing and rotation.
    const TransformTool::Handle handle = TransformTool::hitHandle(frame, *this, pos);
    if (handle != TransformTool::Handle::None) {
        m_tool.begin(*m_scene, handle, frame, selected, mapToScene(pos.toPoint()));
        m_gesture = Gesture::Tool;
        event->accept();
        return;
    }

    BoardItem *hit = unitAt(pos);
    if (hit) {
        if (shift) {
            hit->setSelected(!hit->isSelected());
        } else {
            if (!hit->isSelected()) {
                m_scene->clearSelection();
                hit->setSelected(true);
            }
            m_gesture = Gesture::PendingMove;
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
    m_lastCursor = pos;
    if (m_cropDrag != CropDrag::None) {
        updateCrop(mapToScene(pos.toPoint()));
        event->accept();
        return;
    }
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
    case Gesture::PendingMove:
        if (QLineF(pos, m_pressPos).length() >= DragThreshold && m_scene) {
            const QList<Id> selected = m_scene->selectedIds();
            m_tool.begin(*m_scene, TransformTool::Handle::Move, TransformTool::frameFor(*m_scene, selected), selected,
                         mapToScene(m_pressPos.toPoint()));
            m_gesture = Gesture::Tool;
        } else {
            event->accept();
            return;
        }
        [[fallthrough]];
    case Gesture::Tool:
        m_tool.update(mapToScene(pos.toPoint()), event->modifiers());
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
    if (m_cropDrag != CropDrag::None && event->button() == Qt::LeftButton) {
        m_cropDrag = CropDrag::None;
        event->accept();
        return;
    }
    if (m_gesture == Gesture::Pan && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_gesture = Gesture::None;
        updateCursor();
        event->accept();
        return;
    }
    if (m_gesture == Gesture::Tool && event->button() == Qt::LeftButton) {
        commitGesture();
        event->accept();
        return;
    }
    if (m_gesture == Gesture::PendingMove && event->button() == Qt::LeftButton) {
        m_gesture = Gesture::None; // plain click: selection already handled
        event->accept();
        return;
    }
    if (m_gesture == Gesture::RubberBand && event->button() == Qt::LeftButton) {
        m_gesture = Gesture::None;
        const QRectF band = rubberBandRect();
        if (m_scene) {
            if (band.width() < DragThreshold && band.height() < DragThreshold) {
                if (!m_rubberAdditive)
                    m_scene->clearSelection();
            } else {
                QPainterPath path;
                path.addPolygon(mapToScene(band.toAlignedRect()));
                m_scene->setSelectionArea(path, m_rubberAdditive ? Qt::AddToSelection : Qt::ReplaceSelection,
                                          Qt::IntersectsItemShape, viewportTransform());
            }
        }
        viewport()->update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && !m_editingNote) {
        event->accept(); // the press was handled here (selection click)
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void BoardView::commitGesture()
{
    m_gesture = Gesture::None;
    if (!m_scene || !m_scene->document()) {
        m_tool.cancel();
        return;
    }
    const BoardDocument &doc = *m_scene->document();
    const bool changed = m_tool.changed();
    const QList<Id> ids = m_tool.ids();
    const TransformTool::Handle handle = m_tool.handle();
    OpResult result;
    if (changed && m_tool.isNoteResize() && !ids.isEmpty())
        result = ops::resizeNote(doc, ids.first(), m_tool.noteWidth(), m_tool.noteLocalTransform());
    else if (changed)
        result = ops::transform(doc, ids, m_tool.boardDelta());
    if (!changed || !result.change) {
        m_tool.cancel();
        if (!result.error.isEmpty())
            showToast(result.error);
        return;
    }
    m_tool.finish();
    const QString text = handle == TransformTool::Handle::Move     ? tr("Move")
                       : handle == TransformTool::Handle::Rotate   ? tr("Rotate")
                                                                   : tr("Resize");
    if (!runEdit(result, text)) {
        // Rejected: restore the presentation from the document.
        for (const Id &id : ids)
            if (BoardItem *item = m_scene->itemFor(id))
                item->setTransform(toQTransform(doc.image(id) ? doc.image(id)->transform
                                                : doc.note(id) ? doc.note(id)->transform
                                                : doc.group(id) ? doc.group(id)->transform
                                                                : Affine2D()));
    }
    viewport()->update();
}

void BoardView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_scene || event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }
    if (m_editingNote) {
        QGraphicsView::mouseDoubleClickEvent(event); // word selection in the editor
        return;
    }
    BoardItem *hit = unitAt(event->position());
    if (!hit) {
        // Double-click on a group member while outside group mode.
        const auto hits = items(event->position().toPoint());
        for (QGraphicsItem *it : hits) {
            if (auto *board = dynamic_cast<BoardItem *>(it); board && board->parentItem())
                if (auto *group = dynamic_cast<GroupItem *>(board->parentItem())) {
                    enterGroup(group->entityId());
                    event->accept();
                    return;
                }
        }
        if (!m_scene->activeGroup().isNull())
            leaveGroup();
        event->accept();
        return;
    }
    if (hit->type() == GroupItem::Type)
        enterGroup(hit->entityId());
    else if (hit->type() == NoteItem::Type)
        editNote(hit->entityId());
    event->accept();
}

void BoardView::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_scene && !m_editingNote) {
        BoardItem *hit = unitAt(event->pos());
        if (hit && !hit->isSelected()) {
            m_scene->clearSelection();
            hit->setSelected(true);
        } else if (!hit) {
            m_scene->clearSelection();
        }
    }
    m_lastCursor = event->pos();
    emit contextMenuRequested(event->globalPos());
    event->accept();
}

// --- Keyboard ----------------------------------------------------------------------

bool BoardView::event(QEvent *event)
{
    // Text editing takes priority over canvas shortcuts (spec §3): while a note
    // is being edited, every key goes to the editor, except the window's file
    // commands (Save, Open, Close, ...), which commit the text first.
    if (event->type() == QEvent::ShortcutOverride && m_editingNote) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const QKeySequence pressed(key->keyCombination());
        if (std::any_of(m_textPassThrough.cbegin(), m_textPassThrough.cend(),
                        [&pressed](const QKeySequence &s) { return s == pressed; }))
            return QGraphicsView::event(event); // not accepted: the shortcut fires
        event->accept();
        return true;
    }
    return QGraphicsView::event(event);
}

void BoardView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_tool.isActive()) {
            m_tool.cancel();
            m_gesture = Gesture::None;
        } else if (m_editingNote) {
            commitTextEdit();
        } else if (isCropping()) {
            cancelCrop();
        } else if (m_gesture == Gesture::RubberBand) {
            m_gesture = Gesture::None;
        } else if (m_scene && !m_scene->activeGroup().isNull()) {
            leaveGroup();
        } else {
            emit escapeUnhandled(); // no canvas mode: the window may use it (frameless mode)
        }
        viewport()->update();
        event->accept();
        return;
    }
    if (m_editingNote) {
        QGraphicsView::keyPressEvent(event); // to the editor
        return;
    }
    if (isCropping() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        commitCrop();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        updateCursor();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void BoardView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && !m_editingNote) {
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

// --- Drops -------------------------------------------------------------------------

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

// --- Groups ------------------------------------------------------------------------

void BoardView::enterGroup(const Id &groupId)
{
    if (m_scene) {
        m_scene->enterGroup(groupId);
        viewport()->update();
    }
}

void BoardView::leaveGroup()
{
    if (m_scene) {
        m_scene->leaveGroup();
        viewport()->update();
    }
}

// --- Crop --------------------------------------------------------------------------

void BoardView::beginCrop()
{
    if (!m_scene || !m_scene->document() || isCropping())
        return;
    const QList<Id> selected = m_scene->selectedIds();
    if (selected.size() != 1)
        return;
    ImageItem *item = m_scene->imageItem(selected.first());
    if (!item)
        return;
    if (ops::isLocked(*m_scene->document(), selected.first())) {
        showToast(tr("Locked items cannot be cropped."));
        return;
    }
    m_cropId = selected.first();
    m_cropRect = item->crop();
    item->setCropEditing(true);
    item->setPreviewCrop(m_cropRect);
    emit modeChanged();
    viewport()->update();
}

void BoardView::commitCrop()
{
    if (!isCropping())
        return;
    const Id id = m_cropId;
    const QRectF crop = m_cropRect;
    cancelCrop();
    if (m_scene && m_scene->document())
        runEdit(ops::setCrop(*m_scene->document(), id, crop), tr("Crop"));
}

void BoardView::cancelCrop()
{
    if (!isCropping())
        return;
    if (ImageItem *item = m_scene ? m_scene->imageItem(m_cropId) : nullptr) {
        item->setPreviewCrop(std::nullopt);
        item->setCropEditing(false);
    }
    m_cropId = Id();
    m_cropDrag = CropDrag::None;
    emit modeChanged();
    viewport()->update();
}

QPolygonF BoardView::cropPolygon(const QRectF &c) const
{
    ImageItem *item = m_scene ? m_scene->imageItem(m_cropId) : nullptr;
    if (!item)
        return {};
    const QSizeF s = item->displaySize();
    const QRectF local(c.x() * s.width(), c.y() * s.height(), c.width() * s.width(), c.height() * s.height());
    return mapFromScene(item->mapToScene(local));
}

BoardView::CropDrag BoardView::hitCrop(QPointF viewportPos) const
{
    const QPolygonF p = cropPolygon(m_cropRect); // tl, tr, br, bl, (tl)
    if (p.size() < 4)
        return CropDrag::None;
    const double reach = TransformTool::HandleSize / 2.0 + 4.0;
    auto near = [&](QPointF q) { return QLineF(q, viewportPos).length() <= reach; };
    if (near(p[0])) return CropDrag::TopLeft;
    if (near(p[1])) return CropDrag::TopRight;
    if (near(p[2])) return CropDrag::BottomRight;
    if (near(p[3])) return CropDrag::BottomLeft;
    if (near((p[0] + p[1]) / 2)) return CropDrag::Top;
    if (near((p[1] + p[2]) / 2)) return CropDrag::Right;
    if (near((p[2] + p[3]) / 2)) return CropDrag::Bottom;
    if (near((p[3] + p[0]) / 2)) return CropDrag::Left;
    if (p.containsPoint(viewportPos, Qt::OddEvenFill)) return CropDrag::Move;
    return CropDrag::None;
}

void BoardView::updateCrop(QPointF scenePos)
{
    ImageItem *item = m_scene ? m_scene->imageItem(m_cropId) : nullptr;
    if (!item)
        return;
    const QSizeF s = item->displaySize();
    const QPointF local = item->mapFromScene(scenePos);
    const QPointF n(local.x() / s.width(), local.y() / s.height()); // normalized, image-local
    QRectF r = m_cropStartRect;
    double left = r.left(), right = r.right(), top = r.top(), bottom = r.bottom();
    switch (m_cropDrag) {
    case CropDrag::Move: {
        const QPointF d((local.x() - m_cropPressLocal.x()) / s.width(), (local.y() - m_cropPressLocal.y()) / s.height());
        r.translate(d);
        r.moveLeft(std::clamp(r.left(), 0.0, 1.0 - r.width()));
        r.moveTop(std::clamp(r.top(), 0.0, 1.0 - r.height()));
        m_cropRect = r;
        item->setPreviewCrop(m_cropRect);
        viewport()->update();
        return;
    }
    case CropDrag::Left: left = n.x(); break;
    case CropDrag::Right: right = n.x(); break;
    case CropDrag::Top: top = n.y(); break;
    case CropDrag::Bottom: bottom = n.y(); break;
    case CropDrag::TopLeft: left = n.x(); top = n.y(); break;
    case CropDrag::TopRight: right = n.x(); top = n.y(); break;
    case CropDrag::BottomLeft: left = n.x(); bottom = n.y(); break;
    case CropDrag::BottomRight: right = n.x(); bottom = n.y(); break;
    case CropDrag::None: return;
    }
    left = std::clamp(left, 0.0, r.right() - MinCrop);
    right = std::clamp(right, r.left() + MinCrop, 1.0);
    top = std::clamp(top, 0.0, r.bottom() - MinCrop);
    bottom = std::clamp(bottom, r.top() + MinCrop, 1.0);
    if (m_cropDrag == CropDrag::Left || m_cropDrag == CropDrag::TopLeft || m_cropDrag == CropDrag::BottomLeft)
        right = r.right();
    if (m_cropDrag == CropDrag::Top || m_cropDrag == CropDrag::TopLeft || m_cropDrag == CropDrag::TopRight)
        bottom = r.bottom();
    m_cropRect = QRectF(QPointF(left, top), QPointF(right, bottom));
    item->setPreviewCrop(m_cropRect);
    viewport()->update();
}

// --- Notes -------------------------------------------------------------------------

void BoardView::beginNewNote(QPointF scenePos)
{
    if (!m_scene || m_editingNote)
        return;
    NoteData proto;
    proto.id = Id::generate();
    proto.width = NoteLayout::DefaultWidth;
    proto.fontSize = NoteLayout::DefaultFontSize;
    proto.height = NoteLayout::height({}, proto.width, proto.fontSize);
    proto.transform = Affine2D::translation(scenePos.x() - proto.width / 2, scenePos.y() - proto.height / 2);
    auto *temp = new NoteItem(proto); // not in the document until committed
    temp->setZValue(1e12);
    m_scene->addItem(temp);
    m_scene->clearSelection();
    m_editingNote = temp;
    m_editingNew = true;
    setFocus();
    temp->beginEditing();
    emit modeChanged();
}

void BoardView::editNote(const Id &noteId)
{
    if (!m_scene || !m_scene->document() || m_editingNote)
        return;
    NoteItem *item = m_scene->noteItem(noteId);
    if (!item)
        return;
    if (ops::isLocked(*m_scene->document(), noteId)) {
        showToast(tr("Locked notes cannot be edited."));
        return;
    }
    m_editingNote = item;
    m_editingNoteId = noteId;
    m_editingNew = false;
    setFocus();
    item->beginEditing();
    emit modeChanged();
}

void BoardView::commitTextEdit()
{
    if (!m_editingNote)
        return;
    NoteItem *note = m_editingNote;
    m_editingNote = nullptr;
    m_editingNoteId = Id();
    const QString text = note->endEditing();
    if (m_scene && m_scene->document()) {
        const BoardDocument &doc = *m_scene->document();
        if (m_editingNew) {
            const NoteData proto = note->data();
            const QPointF topLeft = proto.transform.translationPart();
            const double height = NoteLayout::height(text, proto.width, proto.fontSize);
            delete note; // temporary item
            if (!text.trimmed().isEmpty()) {
                const OpResult created = ops::createNote(
                    doc, topLeft + QPointF(proto.width / 2, height / 2), text, proto.fontSize, proto.width);
                if (runEdit(created, tr("New Note")))
                    m_scene->selectIds(created.created);
            }
        } else {
            runEdit(ops::editNote(doc, note->entityId(), text), tr("Edit Note"));
        }
    }
    m_editingNew = false;
    setFocus();
    emit modeChanged();
    viewport()->update();
}

// --- Foreground --------------------------------------------------------------------

void BoardView::drawForeground(QPainter *painter, const QRectF &)
{
    if (!m_scene)
        return;
    painter->save();
    painter->resetTransform(); // viewport coordinates: constant on-screen size
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Active group boundary.
    if (const Id group = m_scene->activeGroup(); !group.isNull()) {
        const QRectF b = m_scene->document()->worldBounds(group);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(OutlineLight, 1, Qt::DashLine));
        painter->drawPolygon(mapFromScene(b.adjusted(-6 / zoom(), -6 / zoom(), 6 / zoom(), 6 / zoom())));
    }

    if (isCropping())
        drawCrop(painter);
    else if (!m_editingNote)
        drawSelection(painter);

    if (m_gesture == Gesture::RubberBand) {
        painter->setPen(QPen(OutlineLight, 1, Qt::DashLine));
        painter->setBrush(QColor(0x7f, 0xd4, 0xff, 40));
        painter->drawRect(rubberBandRect());
    }

    if (!m_toast.isEmpty()) {
        QFont font = painter->font();
        font.setPointSizeF(font.pointSizeF() * 1.05);
        painter->setFont(font);
        const QFontMetrics metrics(font);
        const QSize size = metrics.size(0, m_toast) + QSize(28, 14);
        const QRect box(QPoint((viewport()->width() - size.width()) / 2, viewport()->height() - size.height() - 24),
                        size);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x10, 0x14, 0x18, 225));
        painter->drawRoundedRect(box, 8, 8);
        painter->setPen(Qt::white);
        painter->drawText(box, Qt::AlignCenter, m_toast);
    }
    painter->restore();
}

void BoardView::drawSelection(QPainter *painter)
{
    const QList<Id> selected = m_scene->selectedIds();
    if (selected.isEmpty())
        return;
    const BoardDocument *doc = m_scene->document();
    for (const Id &id : selected) {
        const BoardItem *item = m_scene->itemFor(id);
        if (!item)
            continue;
        const bool locked = doc && ops::isLocked(*doc, id);
        drawOutline(painter, mapFromScene(item->mapToScene(item->localRect())), locked ? LockedColor : OutlineLight);
    }
    const TransformTool::Frame frame = TransformTool::frameFor(*m_scene, selected);
    if (!frame.valid)
        return;
    if (selected.size() > 1) {
        QPolygonF box;
        for (const QPointF &corner : {frame.local.topLeft(), frame.local.topRight(), frame.local.bottomRight(),
                                      frame.local.bottomLeft()})
            box.append(mapFromScene(frame.toScene.map(corner)));
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(OutlineLight, 1, Qt::DashLine));
        painter->drawPolygon(box);
    }
    const auto handles = TransformTool::handles(frame, *this);
    for (const auto &[h, pos] : handles) {
        if (h == TransformTool::Handle::Rotate) {
            const QPointF top = mapFromScene(frame.toScene.map(QPointF(frame.local.center().x(), frame.local.top())));
            painter->setPen(QPen(OutlineLight, 1));
            painter->drawLine(top, pos);
            drawHandle(painter, pos, true);
        } else {
            drawHandle(painter, pos);
        }
    }
    if (!frame.anyUnlocked) {
        const QPointF corner = mapFromScene(frame.toScene.map(frame.local.topRight()));
        const QString label = tr("Locked");
        const QFontMetrics metrics(painter->font());
        const QRectF pill(corner + QPointF(6, -metrics.height() - 10), QSizeF(metrics.horizontalAdvance(label) + 14, metrics.height() + 6));
        painter->setPen(Qt::NoPen);
        painter->setBrush(LockedColor);
        painter->drawRoundedRect(pill, 6, 6);
        painter->setPen(OutlineDark);
        painter->drawText(pill, Qt::AlignCenter, label);
    }
}

void BoardView::drawCrop(QPainter *painter)
{
    const QPolygonF full = cropPolygon(QRectF(0, 0, 1, 1));
    const QPolygonF crop = cropPolygon(m_cropRect);
    if (crop.size() < 4)
        return;
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(OutlineLight, 1, Qt::DashLine));
    painter->drawPolygon(full);
    drawOutline(painter, crop, OutlineLight);
    for (int i = 0; i < 4; ++i) {
        drawHandle(painter, crop[i]);
        drawHandle(painter, (crop[i] + crop[(i + 1) % 4]) / 2);
    }
}

} // namespace glacier::canvas
