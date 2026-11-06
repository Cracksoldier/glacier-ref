#include "glacier/canvas/TransformTool.h"

#include "glacier/canvas/BoardItems.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/editing/Geometry.h"
#include "glacier/editing/NoteLayout.h"
#include "glacier/editing/Operations.h"

#include <QGraphicsView>
#include <QLineF>

#include <algorithm>
#include <cmath>

using namespace glacier::editing;

namespace glacier::canvas {

namespace {

using Handle = TransformTool::Handle;

QPointF localPoint(const QRectF &r, Handle h)
{
    switch (h) {
    case Handle::TopLeft: return r.topLeft();
    case Handle::Top: return {r.center().x(), r.top()};
    case Handle::TopRight: return r.topRight();
    case Handle::Right: return {r.right(), r.center().y()};
    case Handle::BottomRight: return r.bottomRight();
    case Handle::Bottom: return {r.center().x(), r.bottom()};
    case Handle::BottomLeft: return r.bottomLeft();
    case Handle::Left: return {r.left(), r.center().y()};
    default: return r.center();
    }
}

Handle opposite(Handle h)
{
    switch (h) {
    case Handle::TopLeft: return Handle::BottomRight;
    case Handle::Top: return Handle::Bottom;
    case Handle::TopRight: return Handle::BottomLeft;
    case Handle::Right: return Handle::Left;
    case Handle::BottomRight: return Handle::TopLeft;
    case Handle::Bottom: return Handle::Top;
    case Handle::BottomLeft: return Handle::TopRight;
    case Handle::Left: return Handle::Right;
    default: return Handle::None;
    }
}

bool affectsX(Handle h)
{
    return h != Handle::Top && h != Handle::Bottom;
}

bool affectsY(Handle h)
{
    return h != Handle::Left && h != Handle::Right;
}

bool isCorner(Handle h)
{
    return h == Handle::TopLeft || h == Handle::TopRight || h == Handle::BottomLeft || h == Handle::BottomRight;
}

bool onLeft(Handle h)
{
    return h == Handle::TopLeft || h == Handle::Left || h == Handle::BottomLeft;
}

QPointF toViewport(const QGraphicsView &view, QPointF scenePos)
{
    return view.viewportTransform().map(scenePos);
}

double clampScale(double s, double extent)
{
    if (!std::isfinite(s))
        return 1.0;
    const double minimum = extent > 0.0 ? TransformTool::MinExtent / extent : 0.01;
    return std::clamp(s, std::max(minimum, 1e-4), 1000.0);
}

} // namespace

TransformTool::Frame TransformTool::frameFor(const BoardScene &scene, const QList<Id> &ids)
{
    Frame frame;
    const BoardDocument *doc = scene.document();
    if (ids.isEmpty() || !doc)
        return frame;
    for (const Id &id : ids)
        if (!ops::isLocked(*doc, id))
            frame.anyUnlocked = true;
    if (ids.size() == 1) {
        const BoardItem *item = scene.itemFor(ids.first());
        if (!item)
            return frame;
        frame.local = item->localRect();
        frame.toScene = fromQTransform(item->sceneTransform());
        frame.singleNote = item->type() == NoteItem::Type;
    } else {
        frame.local = scene.selectionBounds();
        frame.toScene = Affine2D();
    }
    frame.valid = frame.local.isValid() && frame.toScene.isInvertible();
    return frame;
}

QList<std::pair<Handle, QPointF>> TransformTool::handles(const Frame &frame, const QGraphicsView &view)
{
    QList<std::pair<Handle, QPointF>> out;
    if (!frame.valid || !frame.anyUnlocked)
        return out;
    QList<Handle> resize{Handle::TopLeft, Handle::Top, Handle::TopRight, Handle::Right,
                         Handle::BottomRight, Handle::Bottom, Handle::BottomLeft, Handle::Left};
    if (frame.singleNote) // notes reflow: only width handles
        resize = {Handle::Left, Handle::Right};
    for (Handle h : resize)
        out.append({h, toViewport(view, frame.toScene.map(localPoint(frame.local, h)))});

    const QPointF top = toViewport(view, frame.toScene.map(localPoint(frame.local, Handle::Top)));
    const QPointF centre = toViewport(view, frame.toScene.map(frame.local.center()));
    QLineF up(centre, top);
    if (up.length() < 1e-6)
        up = QLineF(centre, centre - QPointF(0, 1));
    up.setLength(up.length() + RotateOffset);
    out.append({Handle::Rotate, up.p2()});
    return out;
}

Handle TransformTool::hitHandle(const Frame &frame, const QGraphicsView &view, QPointF viewportPos)
{
    const double reach = HandleSize / 2.0 + 3.0;
    for (const auto &[h, pos] : handles(frame, view)) {
        if (std::abs(pos.x() - viewportPos.x()) <= reach && std::abs(pos.y() - viewportPos.y()) <= reach)
            return h;
    }
    return Handle::None;
}

void TransformTool::begin(BoardScene &scene, Handle handle, const Frame &frame, const QList<Id> &ids,
                          QPointF scenePos)
{
    m_scene = &scene;
    m_handle = handle;
    m_frame = frame;
    m_pressScene = scenePos;
    m_delta = Affine2D();
    m_changed = false;
    m_ids.clear();
    m_startLocal.clear();
    m_startScene.clear();
    m_parentScene.clear();
    const BoardDocument *doc = scene.document();
    for (const Id &id : ids) {
        BoardItem *item = scene.itemFor(id);
        if (!item || !doc || ops::isLocked(*doc, id))
            continue; // locked entities never move (spec §3)
        m_ids.append(id);
        m_startLocal.insert(id, item->transform());
        m_startScene.insert(id, item->sceneTransform());
        m_parentScene.insert(id, item->parentItem() ? item->parentItem()->sceneTransform() : QTransform());
    }
    m_noteResize = frame.singleNote && handle != Handle::Move && handle != Handle::Rotate && m_ids.size() == 1;
    if (m_noteResize) {
        m_noteStartWidth = frame.local.width();
        m_noteWidth = m_noteStartWidth;
        m_noteLocal = fromQTransform(m_startLocal.value(m_ids.first()));
    }
}

void TransformTool::update(QPointF scenePos, Qt::KeyboardModifiers modifiers)
{
    if (!isActive() || m_ids.isEmpty())
        return;
    const bool shift = modifiers & Qt::ShiftModifier;
    m_changed = true;

    if (m_handle == Handle::Move) {
        const QPointF d = scenePos - m_pressScene;
        m_delta = Affine2D::translation(d.x(), d.y());
    } else if (m_handle == Handle::Rotate) {
        const QPointF centre = m_frame.toScene.map(m_frame.local.center());
        const double start = QLineF(centre, m_pressScene).angle();
        const double now = QLineF(centre, scenePos).angle();
        double delta = -(now - start); // QLineF angles are counter-clockwise
        if (shift) {
            if (m_ids.size() == 1) {
                const double base = rotationDegrees(m_frame.toScene);
                delta = snapAngle(base + delta) - base;
            } else {
                delta = snapAngle(delta);
            }
        }
        m_delta = rotationAbout(centre, delta);
    } else {
        const QRectF r = m_frame.local;
        const QPointF p = localPoint(r, m_handle);
        const QPointF a = localPoint(r, opposite(m_handle));
        const QPointF m = m_frame.toScene.inverted().map(scenePos);
        double sx = affectsX(m_handle) && p.x() != a.x() ? (m.x() - a.x()) / (p.x() - a.x()) : 1.0;
        double sy = affectsY(m_handle) && p.y() != a.y() ? (m.y() - a.y()) / (p.y() - a.y()) : 1.0;

        if (m_noteResize) {
            const double width = std::clamp(m_noteStartWidth * sx, NoteLayout::MinWidth, NoteLayout::MaxWidth);
            m_noteWidth = width;
            const Affine2D start = fromQTransform(m_startLocal.value(m_ids.first()));
            m_noteLocal = onLeft(m_handle) ? Affine2D::translation(m_noteStartWidth - width, 0) * start : start;
            if (NoteItem *note = m_scene->noteItem(m_ids.first())) {
                note->setPreviewWidth(width);
                note->setTransform(toQTransform(m_noteLocal));
            }
            return;
        }

        if (!shift) { // keep the aspect ratio (spec §3)
            double s;
            if (isCorner(m_handle)) {
                const QPointF v = p - a, w = m - a;
                const double vv = v.x() * v.x() + v.y() * v.y();
                s = vv > 0.0 ? (w.x() * v.x() + w.y() * v.y()) / vv : 1.0;
            } else {
                s = affectsX(m_handle) ? sx : sy;
            }
            sx = sy = clampScale(s, std::min(r.width(), r.height()));
        } else {
            sx = clampScale(sx, r.width());
            sy = clampScale(sy, r.height());
        }
        m_delta = scalingInFrame(m_frame.toScene, a, sx, sy);
    }
    preview();
}

void TransformTool::preview()
{
    for (const Id &id : std::as_const(m_ids)) {
        BoardItem *item = m_scene->itemFor(id);
        if (!item)
            continue;
        const Affine2D world = fromQTransform(m_startScene.value(id)) * m_delta;
        const Affine2D local = world * fromQTransform(m_parentScene.value(id)).inverted();
        item->setTransform(toQTransform(local));
    }
}

void TransformTool::cancel()
{
    if (m_scene) {
        for (const Id &id : std::as_const(m_ids)) {
            if (BoardItem *item = m_scene->itemFor(id)) {
                item->setTransform(m_startLocal.value(id));
                if (m_noteResize)
                    if (NoteItem *note = m_scene->noteItem(id))
                        note->setPreviewWidth(std::nullopt);
            }
        }
    }
    finish();
}

void TransformTool::finish()
{
    if (m_scene && m_noteResize)
        if (NoteItem *note = m_scene->noteItem(m_ids.value(0)))
            note->setPreviewWidth(std::nullopt);
    m_handle = Handle::None;
    m_scene = nullptr;
}

} // namespace glacier::canvas
