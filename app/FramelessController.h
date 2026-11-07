#pragma once

#include <QObject>
#include <QPointer>
#include <Qt>

class QWidget;

namespace glacier {

class FramelessGrip;

// Frameless windows (spec §3, M4.04): with the title bar hidden, the window is
// moved by dragging a grip band along the top edge (or Alt+drag anywhere on
// the canvas) and resized from its edges and corners. Both go through the
// compositor (QWindow::startSystemMove/Resize, spec §7); when the platform
// refuses, `unsupported` is emitted so the window can restore its frame.
class FramelessController : public QObject
{
    Q_OBJECT

public:
    static constexpr int EdgeMargin = 6;
    static constexpr int GripHeight = 10;

    FramelessController(QWidget *window, QWidget *view, QWidget *viewport, QObject *parent = nullptr);
    ~FramelessController() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    QWidget *grip() const;

    // Edges of `window` within EdgeMargin of a window-relative point.
    static Qt::Edges edgesAt(QPoint windowPos, QSize windowSize);

    // For the grip and tests: start a move or resize (false if refused).
    bool beginMove();
    bool beginResize(Qt::Edges edges);
    void toggleMaximized();

signals:
    void unsupported();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void layoutGrip();

    QPointer<QWidget> m_window;
    QPointer<QWidget> m_view;
    QPointer<QWidget> m_viewport;
    FramelessGrip *m_grip = nullptr;
    bool m_enabled = false;
    bool m_cursorSet = false;
};

} // namespace glacier
