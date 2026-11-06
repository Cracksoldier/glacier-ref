#include "MainWindow.h"

#include "glacier/canvas/BoardView.h"
#include "glacier/core/AppInfo.h"
#include "glacier/platform/PlatformInfo.h"

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>

namespace glacier {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_view(new canvas::BoardView(this))
{
    setCentralWidget(m_view);
    setWindowTitle(app_info::displayName());
    resize(1280, 800);
    createMenus();
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setObjectName(QStringLiteral("actionQuit"));
    // Explicit default (spec §3): the platform's QKeySequence::Quit is empty on
    // Windows and on some Linux platform themes. Ctrl maps to Cmd on macOS.
    quitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::closeAllWindows);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAction = helpMenu->addAction(tr("&About Glacier Ref"));
    aboutAction->setObjectName(QStringLiteral("actionAbout"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    QAction *aboutQtAction = helpMenu->addAction(tr("About &Qt"));
    aboutQtAction->setObjectName(QStringLiteral("actionAboutQt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::showAbout()
{
    const platform::PlatformInfo info = platform::collectPlatformInfo();
    QMessageBox box(this);
    box.setWindowTitle(tr("About Glacier Ref"));
    box.setTextFormat(Qt::PlainText);
    box.setText(tr("Glacier Ref %1\n\nAn offline reference board.\n%2")
                    .arg(app_info::version(), app_info::homepageUrl()));
    box.setDetailedText(info.toText());
    box.exec();
}

} // namespace glacier
