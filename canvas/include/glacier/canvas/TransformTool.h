#pragma once

#include "glacier/core/Affine2D.h"
#include "glacier/core/Id.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QTransform>

#include <optional>

class QGraphicsView;

namespace glacier::canvas {

class BoardScene;

// Selection frame, handles and move/resize/rotate gestures (spec §3, M2.03).
//
// A single selected entity uses its own (possibly rotated or flipped) frame; a
// multi-selection uses its axis-aligned bounds. A gesture previews by setting
// graphics-item transforms only; the caller commits one DocumentChange built
// from boardDelta() (or the note-resize result) when the gesture ends.
class TransformTool
{
public:
    enum class Handle { None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Rotate };

    struct Frame
    {
        bool valid = false;
        QRectF local;        // frame-local rectangle
        Affine2D toScene;    // frame-local -> scene
        bool singleNote = false;
        bool anyUnlocked = false;
    };

    static constexpr double HandleSize = 9.0;     // viewport pixels
    static constexpr double RotateOffset = 26.0;  // viewport pixels above the frame
    static constexpr double MinExtent = 4.0;      // board units

    static Frame frameFor(const BoardScene &scene, const QList<Id> &ids);
    // Handle anchor points in viewport coordinates (Move excluded).
    static QList<std::pair<Handle, QPointF>> handles(const Frame &frame, const QGraphicsView &view);
    static Handle hitHandle(const Frame &frame, const QGraphicsView &view, QPointF viewportPos);

    bool isActive() const { return m_handle != Handle::None; }
    Handle handle() const { return m_handle; }
    const QList<Id> &ids() const { return m_ids; }

    void begin(BoardScene &scene, Handle handle, const Frame &frame, const QList<Id> &ids, QPointF scenePos);
    // Updates the preview; returns the current board-space delta.
    void update(QPointF scenePos, Qt::KeyboardModifiers modifiers);
    // Restores the pre-gesture presentation.
    void cancel();
    // Ends the gesture (presentation is left as previewed until the document
    // change arrives).
    void finish();

    Affine2D boardDelta() const { return m_delta; }
    bool changed() const { return m_changed; }
    // Single-note width resize: new width and local transform.
    bool isNoteResize() const { return m_noteResize; }
    double noteWidth() const { return m_noteWidth; }
    Affine2D noteLocalTransform() const { return m_noteLocal; }

private:
    void preview();

    BoardScene *m_scene = nullptr;
    Handle m_handle = Handle::None;
    Frame m_frame;
    QList<Id> m_ids;
    QPointF m_pressScene;
    QHash<Id, QTransform> m_startLocal;
    QHash<Id, QTransform> m_startScene;
    QHash<Id, QTransform> m_parentScene;
    Affine2D m_delta;
    bool m_changed = false;
    bool m_noteResize = false;
    double m_noteStartWidth = 0.0;
    double m_noteWidth = 0.0;
    Affine2D m_noteLocal;
};

} // namespace glacier::canvas
