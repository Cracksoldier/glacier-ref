#pragma once

#include "glacier/canvas/BoardScene.h"

#include <QGraphicsView>
#include <QPointF>
#include <QPointer>

class QMimeData;

namespace glacier::canvas {

// Board canvas view (spec §3): pan (middle drag, Space + left drag, touchpad
// scroll), cursor-anchored zoom (wheel, pinch, Ctrl+wheel), fit board/selection
// within the 1%-6400% zoom range, Shift-click and rubber-band selection, and
// file/image drops. Uses the raster viewport (no OpenGL).
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

    double zoom() const;
    // Sets the zoom (clamped) keeping the scene point under `viewportAnchor` fixed.
    void setZoom(double zoom, QPointF viewportAnchor);
    void zoomBy(double factor, QPointF viewportAnchor);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitBoard();
    void fitSelection();
    void fitRect(const QRectF &sceneRect);

    // Scene position at the centre of the viewport (e.g. paste/import anchor).
    QPointF viewCenter() const;

signals:
    void dropReceived(const QMimeData *mime, QPointF scenePos);

protected:
    void wheelEvent(QWheelEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    enum class Gesture { None, Pan, RubberBand };

    void panBy(QPointF viewportDelta);
    void updateCursor();
    QRectF rubberBandRect() const;

    QPointer<BoardScene> m_scene;
    Gesture m_gesture = Gesture::None;
    bool m_spaceHeld = false;
    QPointF m_lastPos;
    QPointF m_rubberOrigin;
    QPointF m_rubberCurrent;
    bool m_rubberAdditive = false;
};

} // namespace glacier::canvas
