#include "RecoveryDialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace glacier {

namespace {
constexpr int EntryRole = Qt::UserRole;
}

RecoveryDialog::RecoveryDialog(const QList<Entry> &entries, Handler recover, Handler openOriginal, Handler discard,
                               QWidget *parent)
    : QDialog(parent)
    , m_entries(entries)
    , m_recover(std::move(recover))
    , m_openOriginal(std::move(openOriginal))
    , m_discard(std::move(discard))
{
    setWindowTitle(tr("Recover Unsaved Changes"));
    setObjectName(u"recoveryDialog"_s);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Glacier Ref did not close normally. These boards have unsaved changes that can "
                                "be recovered. Recovered boards open as changed; save them to keep the changes."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Board"), tr("File"), tr("Last change")});
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries[i];
        auto *item = new QTreeWidgetItem(m_list);
        item->setText(0, e.title.isEmpty() ? tr("Untitled") : e.title);
        item->setText(1, e.boardPath.isEmpty() ? tr("(never saved)") : e.boardPath);
        item->setToolTip(1, item->text(1));
        item->setText(2, QLocale().toString(e.time.toLocalTime(), QLocale::ShortFormat));
        item->setData(0, EntryRole, int(i));
        item->setSelected(true);
    }
    layout->addWidget(m_list);

    auto *buttons = new QDialogButtonBox(this);
    m_recoverButton = buttons->addButton(tr("&Recover"), QDialogButtonBox::AcceptRole);
    m_originalButton = buttons->addButton(tr("Open &Original"), QDialogButtonBox::ActionRole);
    m_discardButton = buttons->addButton(tr("&Discard…"), QDialogButtonBox::DestructiveRole);
    QPushButton *later = buttons->addButton(tr("Decide &Later"), QDialogButtonBox::RejectRole);
    m_recoverButton->setDefault(true);
    layout->addWidget(buttons);

    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &RecoveryDialog::updateButtons);
    // The buttons act without closing the dialog (QDialogButtonBox roles would close it).
    disconnect(buttons, nullptr, this, nullptr);
    connect(m_recoverButton, &QPushButton::clicked, this, [this] { act(m_recover); });
    connect(m_originalButton, &QPushButton::clicked, this, [this] {
        // The recovery data stays; it is offered again until recovered or discarded.
        const auto items = m_list->selectedItems();
        for (QTreeWidgetItem *item : items) {
            const Entry &e = m_entries.at(item->data(0, EntryRole).toInt());
            if (e.boardPath.isEmpty())
                continue; // never saved: there is no original
            m_openOriginal(e);
            delete item;
        }
        if (m_list->topLevelItemCount() == 0)
            accept();
        updateButtons();
    });
    connect(m_discardButton, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::warning(this, tr("Discard Unsaved Changes"),
                                                 tr("Discard the unsaved changes of %n board(s)? This cannot be undone.",
                                                    nullptr, int(selectedEntries().size())),
                                                 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer == QMessageBox::Discard)
            act(m_discard);
    });
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    resize(640, 320);
    updateButtons();
}

int RecoveryDialog::remaining() const
{
    return m_list->topLevelItemCount();
}

QList<RecoveryDialog::Entry> RecoveryDialog::selectedEntries() const
{
    QList<Entry> chosen;
    const auto items = m_list->selectedItems();
    for (QTreeWidgetItem *item : items)
        chosen.append(m_entries.at(item->data(0, EntryRole).toInt()));
    return chosen;
}

void RecoveryDialog::removeSelected()
{
    qDeleteAll(m_list->selectedItems());
    if (m_list->topLevelItemCount() == 0)
        accept();
    updateButtons();
}

void RecoveryDialog::act(const Handler &handler)
{
    const QList<Entry> chosen = selectedEntries();
    for (const Entry &e : chosen)
        handler(e);
    removeSelected();
}

void RecoveryDialog::updateButtons()
{
    const QList<Entry> chosen = selectedEntries();
    bool anyOriginal = false;
    for (const Entry &e : chosen)
        anyOriginal = anyOriginal || !e.boardPath.isEmpty();
    m_recoverButton->setEnabled(!chosen.isEmpty());
    m_discardButton->setEnabled(!chosen.isEmpty());
    m_originalButton->setEnabled(anyOriginal);
}

void RecoveryDialog::recoverAll()
{
    m_list->selectAll();
    act(m_recover);
}

void RecoveryDialog::discardAll()
{
    m_list->selectAll();
    act(m_discard);
}

} // namespace glacier
