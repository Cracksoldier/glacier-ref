#pragma once

#include <QMainWindow>

namespace glacier::canvas {
class BoardView;
}

namespace glacier {

// Top-level window. Each window owns one board (spec §3); in M0 it shows an
// empty canvas with File and Help menus.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    canvas::BoardView *boardView() const { return m_view; }

private:
    void createMenus();
    void showAbout();

    canvas::BoardView *m_view = nullptr; // owned via QObject parent
};

} // namespace glacier
