#pragma once

#include "glacier/persistence/RecoveryStore.h"

#include <QDialog>
#include <QList>

#include <functional>

class QPushButton;
class QTreeWidget;

namespace glacier {

// Startup recovery choices (spec §5, M3.10): lists the boards with unsaved
// changes from sessions that ended abnormally and offers Recover, Open
// Original or Discard for the selected ones. "Decide Later" keeps everything
// for the next start. Handled entries leave the list; the dialog closes when
// it is empty.
class RecoveryDialog : public QDialog
{
    Q_OBJECT

public:
    using Entry = persistence::RecoveryStore::Entry;
    using Handler = std::function<void(const Entry &)>;

    RecoveryDialog(const QList<Entry> &entries, Handler recover, Handler openOriginal, Handler discard,
                   QWidget *parent = nullptr);

    int remaining() const;

    // For tests: act on all entries as if selected and clicked.
    void recoverAll();
    void discardAll();

private:
    QList<Entry> selectedEntries() const;
    void removeSelected();
    void updateButtons();
    void act(const Handler &handler);

    QList<Entry> m_entries;
    Handler m_recover;
    Handler m_openOriginal;
    Handler m_discard;
    QTreeWidget *m_list = nullptr;
    QPushButton *m_recoverButton = nullptr;
    QPushButton *m_originalButton = nullptr;
    QPushButton *m_discardButton = nullptr;
};

} // namespace glacier
