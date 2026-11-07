#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

#include <functional>

class QTabWidget;
class QTableWidget;

namespace glacier {

// Offline help (spec §3, M4.01): basic usage from bundled HTML, the shortcut
// reference generated from the live action registry (so customized
// shortcuts show), and About. Nothing is loaded from the network; the
// project website opens in the browser only when its button is clicked.
class HelpDialog : public QDialog
{
    Q_OBJECT

public:
    struct ShortcutRow
    {
        QString id;      // action ID
        QString menu;    // e.g. "Board"
        QString command; // action text without accelerator markers
        QList<QKeySequence> shortcuts;
    };
    enum class Page { Basics, Shortcuts, About };

    HelpDialog(const QList<ShortcutRow> &rows, const QString &aboutText, std::function<void()> openWebsite,
               QWidget *parent = nullptr);

    void showPage(Page page);
    QTableWidget *shortcutTable() const { return m_table; }

private:
    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace glacier
