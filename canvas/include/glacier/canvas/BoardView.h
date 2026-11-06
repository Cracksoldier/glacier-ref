#pragma once

#include <QGraphicsView>

class QGraphicsScene;

namespace glacier::canvas {

// Board canvas view. M0: an empty scene with the default background; pan,
// zoom, and items arrive in M1. Uses the raster viewport (no OpenGL).
class BoardView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit BoardView(QWidget *parent = nullptr);

    QGraphicsScene *boardScene() const { return m_scene; }

private:
    QGraphicsScene *m_scene = nullptr; // owned via QObject parent (this view)
};

} // namespace glacier::canvas
