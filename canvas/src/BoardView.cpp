#include "glacier/canvas/BoardView.h"

#include <QColor>
#include <QGraphicsScene>

namespace glacier::canvas {

namespace {
// Neutral default board background (spec §3).
const QColor DefaultBackground(0x3a, 0x3d, 0x41);
} // namespace

BoardView::BoardView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setBackgroundBrush(DefaultBackground);
    setFrameShape(QFrame::NoFrame);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

} // namespace glacier::canvas
