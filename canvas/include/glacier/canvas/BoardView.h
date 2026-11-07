#pragma once

#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/TransformTool.h"
#include "glacier/editing/CommandController.h"
#include "glacier/editing/Operations.h"

#include <QElapsedTimer>
#include <QKeySequence>
#include <QList>
#include <QGraphicsView>
#include <QPointF>
#include <QPointer>
#include <QTimer>

class QMimeData;

namespace glacier::canvas {

// Board canvas view (spec §3).
//
// Navigation: pan (middle drag, Space + left drag, touchpad scroll),
// cursor-anchored zoom (wheel, pinch, Ctrl+wheel), fit, 1%-6400%.
// Editing: click/Shift-click/rubber-band selection; drag to move; handles to
// resize (aspect kept, Shift non-uniform) and rotate (Shift snaps 15 degrees);
// double-click enters a group or edits a note; crop mode; Escape cancels or
// leaves the current mode. Every completed gesture is one undo step committed
// through the CommandController. Uses the raster viewport (no OpenGL).
class BoardView : public QGraphicsView
{
    Q_OBJECT

public:
    static constexpr double MinZoom = 0.01;
    static constexpr double MaxZoom = 64.0;
    static constexpr double WheelStep = 1.15;
    static constexpr double ButtonStep = 1.25;

    explicit BoardView(QWidget *parent = nullptr);

    void setBoardScene(BoardScene *scene);
    BoardScene *boardScene() const { return m_scene; }
    void setController(editing::CommandController *controller) { m_controller = controller; }

    // Navigation.
    double zoom() const;
    void setZoom(double zoom, QPointF viewportAnchor);
    void zoomBy(double factor, QPointF viewportAnchor);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitBoard();
    void fitSelection();
    void fitRect(const QRectF &sceneRect);
    QPointF viewCenter() const;
    // Last cursor position over the canvas, or the view centre.
    QPointF cursorScenePos() const;

    // Modes.
    bool isTextEditing() const { return m_editingNote != nullptr; }
    bool isCropping() const { return !m_cropId.isNull(); }
    bool isGestureActive() const { return m_tool.isActive() || m_cropDrag != CropDrag::None; }
    void beginCrop();      // single selected unlocked image
    void commitCrop();
    void cancelCrop();
    void beginNewNote(QPointF scenePos);
    void editNote(const Id &noteId);
    void commitTextEdit();
    void enterGroup(const Id &groupId);
    void leaveGroup();

    // Shortcuts that still reach the window while a note is being edited.
    void setTextEditingPassThrough(const QList<QKeySequence> &shortcuts) { m_textPassThrough = shortcuts; }
    QList<QKeySequence> textEditingPassThrough() const { return m_textPassThrough; }

    // Runs an edit through the controller; shows its error or lock notice.
    bool runEdit(const editing::OpResult &result, const QString &text);
    void showToast(const QString &text, int durationMs = 2600);
    QString toastText() const { return m_toast; }

signals:
    void dropReceived(const QMimeData *mime, QPointF scenePos);
    void contextMenuRequested(QPoint globalPos);
    void modeChanged();

protected:
    bool event(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    enum class Gesture { None, Pan, RubberBand, PendingMove, Tool };
    enum class CropDrag { None, Move, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

    BoardItem *unitAt(QPointF viewportPos) const;
    void panBy(QPointF viewportDelta);
    void updateCursor();
    QRectF rubberBandRect() const;
    void commitGesture();
    void applyViewSettings();
    void drawSelection(QPainter *painter);
    void drawCrop(QPainter *painter);
    QPolygonF cropPolygon(const QRectF &normalizedCrop) const;
    CropDrag hitCrop(QPointF viewportPos) const;
    void updateCrop(QPointF scenePos);

    QPointer<BoardScene> m_scene;
    QPointer<editing::CommandController> m_controller;
    Gesture m_gesture = Gesture::None;
    bool m_spaceHeld = false;
    QPointF m_lastPos;
    QPointF m_pressPos;
    QPointF m_lastCursor{-1, -1};
    QPointF m_rubberOrigin;
    QPointF m_rubberCurrent;
    bool m_rubberAdditive = false;
    TransformTool m_tool;

    // Crop mode.
    Id m_cropId;
    QRectF m_cropRect;      // normalized, being edited
    QRectF m_cropStartRect;
    CropDrag m_cropDrag = CropDrag::None;
    QPointF m_cropPressLocal;

    // Text editing.
    NoteItem *m_editingNote = nullptr; // scene-owned; a temporary item for new notes
    Id m_editingNoteId;                // never dereference m_editingNote to identify it
    bool m_editingNew = false;

    QString m_toast;
    QTimer m_toastTimer;
    QList<QKeySequence> m_textPassThrough;
};

} // namespace glacier::canvas
