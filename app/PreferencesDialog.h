#pragma once

#include "SettingsService.h"

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

class QComboBox;
class QDialogButtonBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace glacier {

// Preferences (spec §3, M4.02): general (recovery, image cache, data
// folders, portable mode), appearance (theme, new-board background) and
// shortcuts (every action, with conflict reporting). Changes are written to
// the SettingsService on OK and apply to all windows at once.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    struct ShortcutEntry
    {
        QString id;
        QString menu;
        QString command;
        QList<QKeySequence> defaults;
        QList<QKeySequence> current;
    };
    struct Folders
    {
        QString settings, sessions, recovery, logs;
    };

    PreferencesDialog(SettingsService *settings, const QList<ShortcutEntry> &shortcuts, const Folders &folders,
                      QWidget *parent = nullptr);

    // The shortcut assignment as edited (primary and alternate), by action ID.
    SettingsService::ShortcutMap editedShortcuts() const;
    // Conflicts in the edited assignment (OK is disabled while there are any).
    QHash<QKeySequence, QStringList> conflicts() const;
    QPushButton *okButton() const;

    // For tests: edit a row as the key sequence editors would.
    void setShortcutForTesting(const QString &id, const QList<QKeySequence> &keys);

    void accept() override;

private:
    void updateConflicts();
    void resetRow(int row);
    int rowOf(const QString &id) const;

    SettingsService *m_settings;
    QList<ShortcutEntry> m_entries;
    QSpinBox *m_recoveryInterval = nullptr;
    QSpinBox *m_recoveryBudget = nullptr;
    QSpinBox *m_cacheBudget = nullptr;
    QComboBox *m_theme = nullptr;
    QPushButton *m_background = nullptr;
    quint32 m_backgroundColor = 0;
    QTableWidget *m_table = nullptr;
    QList<QKeySequenceEdit *> m_primary;
    QList<QKeySequenceEdit *> m_alternate;
    QLabel *m_conflictLabel = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace glacier
