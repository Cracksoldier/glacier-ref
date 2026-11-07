#include "PreferencesDialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QSet>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace glacier {

namespace {

constexpr int PrimaryColumn = 2;
constexpr int AlternateColumn = 3;
constexpr int ResetColumn = 4;

QKeySequenceEdit *sequenceEdit(const QKeySequence &key, QWidget *parent)
{
    auto *edit = new QKeySequenceEdit(key, parent);
    edit->setMaximumSequenceLength(1);
    edit->setClearButtonEnabled(true);
    return edit;
}

void setButtonColor(QPushButton *button, quint32 argb)
{
    const QColor c = QColor::fromRgba(argb);
    button->setText(c.name().toUpper());
    button->setStyleSheet(u"QPushButton { background: %1; color: %2; }"_s.arg(
        c.name(), c.lightness() > 128 ? u"#000"_s : u"#fff"_s));
}

} // namespace

PreferencesDialog::PreferencesDialog(SettingsService *settings, const QList<ShortcutEntry> &shortcuts,
                                     const Folders &folders, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_entries(shortcuts)
{
    setWindowTitle(tr("Preferences"));
    setObjectName(u"preferencesDialog"_s);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    // --- General ---
    auto *general = new QWidget(tabs);
    auto *form = new QFormLayout(general);
    m_recoveryInterval = new QSpinBox(general);
    m_recoveryInterval->setRange(SettingsService::MinRecoveryIntervalSeconds, SettingsService::MaxRecoveryIntervalSeconds);
    m_recoveryInterval->setSuffix(tr(" s"));
    m_recoveryInterval->setValue(settings->recoveryIntervalSeconds());
    m_recoveryInterval->setToolTip(tr("How often unsaved changes are kept for recovery after a crash."));
    form->addRow(tr("Keep unsaved changes every"), m_recoveryInterval);
    m_recoveryBudget = new QSpinBox(general);
    m_recoveryBudget->setRange(SettingsService::MinRecoveryBudgetMiB, SettingsService::MaxRecoveryBudgetMiB);
    m_recoveryBudget->setSingleStep(256);
    m_recoveryBudget->setSuffix(tr(" MiB"));
    m_recoveryBudget->setValue(settings->recoveryBudgetMiB());
    form->addRow(tr("Recovery storage limit"), m_recoveryBudget);
    m_cacheBudget = new QSpinBox(general);
    m_cacheBudget->setRange(SettingsService::MinCacheBudgetMiB, SettingsService::MaxCacheBudgetMiB);
    m_cacheBudget->setSingleStep(128);
    m_cacheBudget->setSuffix(tr(" MiB"));
    m_cacheBudget->setValue(settings->cacheBudgetMiB());
    m_cacheBudget->setToolTip(tr("Memory for decoded images. More keeps large boards sharper while you move around."));
    form->addRow(tr("Image cache"), m_cacheBudget);
    auto folder = [general](const QString &path) {
        auto *label = new QLabel(QDir::toNativeSeparators(path), general);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        return label;
    };
    form->addRow(tr("Settings"), folder(folders.settings));
    form->addRow(tr("Open images"), folder(folders.sessions));
    form->addRow(tr("Recovery"), folder(folders.recovery));
    form->addRow(tr("Logs"), folder(folders.logs));
    auto *portable = new QLabel(tr("Off: preferences and recovery data are stored in your user profile."), general);
    portable->setWordWrap(true);
    form->addRow(tr("Portable mode"), portable);
    tabs->addTab(general, tr("General"));

    // --- Appearance ---
    auto *appearance = new QWidget(tabs);
    auto *appearanceForm = new QFormLayout(appearance);
    m_theme = new QComboBox(appearance);
    m_theme->setObjectName(u"themeBox"_s);
    m_theme->addItem(tr("Glacier dark"), int(SettingsService::Theme::Glacier));
    m_theme->addItem(tr("System"), int(SettingsService::Theme::System));
    m_theme->setCurrentIndex(settings->theme() == SettingsService::Theme::System ? 1 : 0);
    appearanceForm->addRow(tr("Theme"), m_theme);
    m_backgroundColor = settings->newBoardBackground();
    m_background = new QPushButton(appearance);
    setButtonColor(m_background, m_backgroundColor);
    connect(m_background, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor::fromRgba(m_backgroundColor), this, tr("New Board Background"));
        if (c.isValid()) {
            m_backgroundColor = c.rgba() | 0xFF000000u;
            setButtonColor(m_background, m_backgroundColor);
        }
    });
    appearanceForm->addRow(tr("Background of new boards"), m_background);
    tabs->addTab(appearance, tr("Appearance"));

    // --- Shortcuts ---
    auto *shortcutPage = new QWidget(tabs);
    auto *shortcutLayout = new QVBoxLayout(shortcutPage);
    auto *filter = new QLineEdit(shortcutPage);
    filter->setPlaceholderText(tr("Filter commands"));
    filter->setClearButtonEnabled(true);
    shortcutLayout->addWidget(filter);
    m_table = new QTableWidget(int(m_entries.size()), 5, shortcutPage);
    m_table->setHorizontalHeaderLabels({tr("Menu"), tr("Command"), tr("Shortcut"), tr("Alternate"), QString()});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const ShortcutEntry &e = m_entries[i];
        const int row = int(i);
        m_table->setItem(row, 0, new QTableWidgetItem(e.menu));
        m_table->setItem(row, 1, new QTableWidgetItem(e.command));
        auto *primary = sequenceEdit(e.current.value(0), m_table);
        auto *alternate = sequenceEdit(e.current.value(1), m_table);
        m_table->setCellWidget(row, PrimaryColumn, primary);
        m_table->setCellWidget(row, AlternateColumn, alternate);
        m_primary.append(primary);
        m_alternate.append(alternate);
        connect(primary, &QKeySequenceEdit::keySequenceChanged, this, &PreferencesDialog::updateConflicts);
        connect(alternate, &QKeySequenceEdit::keySequenceChanged, this, &PreferencesDialog::updateConflicts);
        auto *reset = new QToolButton(m_table);
        reset->setText(tr("Default"));
        reset->setToolTip(tr("Restore the default shortcut"));
        connect(reset, &QToolButton::clicked, this, [this, row] { resetRow(row); });
        m_table->setCellWidget(row, ResetColumn, reset);
    }
    connect(filter, &QLineEdit::textChanged, m_table, [this](const QString &text) {
        for (int r = 0; r < m_table->rowCount(); ++r)
            m_table->setRowHidden(r, !text.isEmpty() && !m_table->item(r, 0)->text().contains(text, Qt::CaseInsensitive)
                                         && !m_table->item(r, 1)->text().contains(text, Qt::CaseInsensitive));
    });
    shortcutLayout->addWidget(m_table);
    m_conflictLabel = new QLabel(shortcutPage);
    m_conflictLabel->setObjectName(u"conflictLabel"_s);
    m_conflictLabel->setWordWrap(true);
    shortcutLayout->addWidget(m_conflictLabel);
    auto *hint = new QLabel(tr("Single keys (such as N, C or G) and Tab don't act while a note is being edited. "
                               "Ctrl+Alt combinations can clash with AltGr characters on some keyboard layouts."),
                            shortcutPage);
    hint->setWordWrap(true);
    shortcutLayout->addWidget(hint);
    auto *resetAll = new QPushButton(tr("Restore All Defaults"), shortcutPage);
    connect(resetAll, &QPushButton::clicked, this, [this] {
        for (int r = 0; r < m_table->rowCount(); ++r)
            resetRow(r);
    });
    shortcutLayout->addWidget(resetAll, 0, Qt::AlignLeft);
    tabs->addTab(shortcutPage, tr("Shortcuts"));

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);
    resize(760, 600);
    updateConflicts();
}

QPushButton *PreferencesDialog::okButton() const
{
    return m_buttons->button(QDialogButtonBox::Ok);
}

int PreferencesDialog::rowOf(const QString &id) const
{
    for (qsizetype i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].id == id)
            return int(i);
    return -1;
}

void PreferencesDialog::resetRow(int row)
{
    const ShortcutEntry &e = m_entries[row];
    m_primary[row]->setKeySequence(e.defaults.value(0));
    m_alternate[row]->setKeySequence(e.defaults.value(1));
    updateConflicts();
}

void PreferencesDialog::setShortcutForTesting(const QString &id, const QList<QKeySequence> &keys)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    m_primary[row]->setKeySequence(keys.value(0));
    m_alternate[row]->setKeySequence(keys.value(1));
    updateConflicts();
}

SettingsService::ShortcutMap PreferencesDialog::editedShortcuts() const
{
    SettingsService::ShortcutMap map;
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const ShortcutEntry &e = m_entries[i];
        // Untouched rows keep their full list (platform bindings can have more than two).
        if (m_primary[i]->keySequence() == e.current.value(0) && m_alternate[i]->keySequence() == e.current.value(1)) {
            map.insert(e.id, e.current);
            continue;
        }
        if (m_primary[i]->keySequence() == e.defaults.value(0) && m_alternate[i]->keySequence() == e.defaults.value(1)) {
            map.insert(e.id, e.defaults);
            continue;
        }
        QList<QKeySequence> keys;
        for (const QKeySequence &k : {m_primary[i]->keySequence(), m_alternate[i]->keySequence()})
            if (!k.isEmpty() && !keys.contains(k))
                keys.append(k);
        map.insert(m_entries[i].id, keys);
    }
    return map;
}

QHash<QKeySequence, QStringList> PreferencesDialog::conflicts() const
{
    return SettingsService::conflicts(editedShortcuts());
}

void PreferencesDialog::updateConflicts()
{
    const auto found = conflicts();
    QStringList messages;
    QSet<QString> conflicting;
    for (auto it = found.cbegin(); it != found.cend(); ++it) {
        QStringList commands;
        for (const QString &id : it.value()) {
            conflicting.insert(id);
            const int row = rowOf(id);
            commands.append(row >= 0 ? m_entries[row].command : id);
        }
        messages.append(tr("%1 is used by %2.").arg(it.key().toString(QKeySequence::NativeText),
                                                    commands.join(tr(" and "))));
    }
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const bool bad = conflicting.contains(m_entries[i].id);
        for (int c = 0; c < 2; ++c)
            m_table->item(int(i), c)->setForeground(bad ? QColor(0xe0, 0x60, 0x50) : m_table->palette().text().color());
    }
    messages.sort();
    m_conflictLabel->setText(messages.isEmpty() ? QString()
                                                : tr("Resolve these conflicts first: %1").arg(messages.join(u' ')));
    okButton()->setEnabled(found.isEmpty());
}

void PreferencesDialog::accept()
{
    if (!conflicts().isEmpty())
        return;
    m_settings->setRecoveryIntervalSeconds(m_recoveryInterval->value());
    m_settings->setRecoveryBudgetMiB(m_recoveryBudget->value());
    m_settings->setCacheBudgetMiB(m_cacheBudget->value());
    m_settings->setTheme(SettingsService::Theme(m_theme->currentData().toInt()));
    m_settings->setNewBoardBackground(m_backgroundColor);
    // Only differences from the defaults are stored.
    SettingsService::ShortcutMap overrides;
    const SettingsService::ShortcutMap edited = editedShortcuts();
    for (const ShortcutEntry &e : std::as_const(m_entries))
        if (edited.value(e.id) != e.defaults)
            overrides.insert(e.id, edited.value(e.id));
    m_settings->setShortcutOverrides(overrides);
    QDialog::accept();
}

} // namespace glacier
