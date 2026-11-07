#include "FramelessController.h"

#include "glacier/platform/PlatformWindowService.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <QWindow>

namespace glacier {

namespace {
Qt::CursorShape cursorFor(Qt::Edges edges)
{
    const bool left = edges & Qt::LeftEdge, right = edges & Qt::RightEdge;
    const bool top = edges & Qt::TopEdge, bottom = edges & Qt::BottomEdge;
    if ((left && top) || (right && bottom))
        return Qt::SizeFDiagCursor;
    if ((right && top) || (left && bottom))
        return Qt::SizeBDiagCursor;
    if (left || right)
        return Qt::SizeHorCursor;
    if (top || bottom)
        return Qt::SizeVerCursor;
    return Qt::SizeAllCursor; // the grip moves the window
}
} // namespace

// The move grip: a thin band along the top of the canvas, faintly drawn
// while the pointer is over it.
class FramelessGrip : public QWidget
{
public:
    FramelessGrip(FramelessController *controller, QWidget *parent)
        : QWidget(parent)
        , m_controller(controller)
    {
        setObjectName(QStringLiteral("framelessGrip"));
        setMouseTracking(true);
        setCursor(Qt::SizeAllCursor);
        setToolTip(tr("Drag to move the window · double-click to maximize"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!underMouse())
            return;
        QPainter p(this);
        p.fillRect(rect(), QColor(255, 255, 255, 28));
        p.fillRect(QRect(width() / 2 - 24, height() / 2 - 1, 48, 2), QColor(255, 255, 255, 110));
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        setCursor(cursorFor(FramelessController::edgesAt(mapTo(window(), event->position().toPoint()), window()->size())));
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        const Qt::Edges edges = FramelessController::edgesAt(mapTo(window(), event->position().toPoint()), window()->size());
        if (edges)
            m_controller->beginResize(edges);
        else
            m_controller->beginMove();
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        m_controller->toggleMaximized();
        event->accept();
    }

private:
    FramelessController *m_controller;
};

FramelessController::FramelessController(QWidget *window, QWidget *view, QWidget *viewport, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_view(view)
    , m_viewport(viewport)
{
    m_grip = new FramelessGrip(this, view);
    m_grip->hide();
    view->installEventFilter(this);
    viewport->installEventFilter(this);
    viewport->setMouseTracking(true);
}

FramelessController::~FramelessController() = default; // the grip belongs to the view

QWidget *FramelessController::grip() const
{
    return m_grip;
}

void FramelessController::setEnabled(bool enabled)
{
    m_enabled = enabled;
    m_grip->setVisible(enabled);
    if (enabled) {
        layoutGrip();
        m_grip->raise();
    } else if (m_cursorSet && m_viewport) {
        m_viewport->unsetCursor();
        m_cursorSet = false;
    }
}

void FramelessController::layoutGrip()
{
    if (m_view)
        m_grip->setGeometry(0, 0, m_view->width(), GripHeight);
}

Qt::Edges FramelessController::edgesAt(QPoint p, QSize size)
{
    Qt::Edges edges;
    if (p.x() < EdgeMargin)
        edges |= Qt::LeftEdge;
    if (p.x() >= size.width() - EdgeMargin)
        edges |= Qt::RightEdge;
    if (p.y() < EdgeMargin / 2 + 1)
        edges |= Qt::TopEdge;
    if (p.y() >= size.height() - EdgeMargin)
        edges |= Qt::BottomEdge;
    return edges;
}

bool FramelessController::beginMove()
{
    const bool ok = m_window && platform::startSystemMove(m_window->windowHandle());
    if (!ok)
        emit unsupported();
    return ok;
}

bool FramelessController::beginResize(Qt::Edges edges)
{
    const bool ok = m_window && platform::startSystemResize(m_window->windowHandle(), edges);
    if (!ok)
        emit unsupported();
    return ok;
}

void FramelessController::toggleMaximized()
{
    if (!m_window)
        return;
    if (m_window->isMaximized())
        m_window->showNormal();
    else
        m_window->showMaximized();
}

bool FramelessController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view && event->type() == QEvent::Resize)
        layoutGrip();
    if (!m_enabled || watched != m_viewport || !m_window)
        return false;
    switch (event->type()) {
    case QEvent::MouseMove: {
        auto *e = static_cast<QMouseEvent *>(event);
        if (e->buttons() != Qt::NoButton)
            return false;
        const Qt::Edges edges = edgesAt(m_viewport->mapTo(m_window, e->position().toPoint()), m_window->size());
        if (edges) {
            m_viewport->setCursor(cursorFor(edges));
            m_cursorSet = true;
            return true; // the canvas doesn't see moves in the resize margin
        }
        if (m_cursorSet) {
            m_viewport->unsetCursor();
            m_cursorSet = false;
        }
        return false;
    }
    case QEvent::MouseButtonPress: {
        auto *e = static_cast<QMouseEvent *>(event);
        if (e->button() != Qt::LeftButton)
            return false;
        const Qt::Edges edges = edgesAt(m_viewport->mapTo(m_window, e->position().toPoint()), m_window->size());
        if (edges) {
            beginResize(edges);
            return true;
        }
        if (e->modifiers() & Qt::AltModifier) {
            beginMove();
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

} // namespace glacier
