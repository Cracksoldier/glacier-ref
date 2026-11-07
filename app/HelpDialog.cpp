#include "HelpDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace glacier {

HelpDialog::HelpDialog(const QList<ShortcutRow> &rows, const QString &aboutText, std::function<void()> openWebsite,
                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Glacier Ref Help"));
    setObjectName(u"helpDialog"_s);
    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs);

    // Basics: bundled HTML; the browser never follows or fetches links.
    auto *basics = new QTextBrowser(m_tabs);
    basics->setOpenLinks(false);
    basics->setOpenExternalLinks(false);
    basics->setSource(QUrl(u"qrc:/help/basics.html"_s));
    m_tabs->addTab(basics, tr("Basics"));

    // Shortcuts.
    auto *shortcutPage = new QWidget(m_tabs);
    auto *shortcutLayout = new QVBoxLayout(shortcutPage);
    auto *filter = new QLineEdit(shortcutPage);
    filter->setPlaceholderText(tr("Filter commands"));
    filter->setClearButtonEnabled(true);
    shortcutLayout->addWidget(filter);
    m_table = new QTableWidget(int(rows.size()), 3, shortcutPage);
    m_table->setObjectName(u"shortcutTable"_s);
    m_table->setHorizontalHeaderLabels({tr("Menu"), tr("Command"), tr("Shortcut")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (qsizetype i = 0; i < rows.size(); ++i) {
        QStringList keys;
        for (const QKeySequence &k : rows[i].shortcuts)
            keys.append(k.toString(QKeySequence::NativeText));
        m_table->setItem(int(i), 0, new QTableWidgetItem(rows[i].menu));
        m_table->setItem(int(i), 1, new QTableWidgetItem(rows[i].command));
        m_table->setItem(int(i), 2, new QTableWidgetItem(keys.join(u", "_s)));
    }
    connect(filter, &QLineEdit::textChanged, m_table, [this](const QString &text) {
        for (int r = 0; r < m_table->rowCount(); ++r) {
            bool match = text.isEmpty();
            for (int c = 0; c < 3 && !match; ++c)
                match = m_table->item(r, c)->text().contains(text, Qt::CaseInsensitive);
            m_table->setRowHidden(r, !match);
        }
    });
    shortcutLayout->addWidget(m_table);
    auto *note = new QLabel(tr("Change shortcuts in Edit ▸ Preferences ▸ Shortcuts. While a note is being edited, "
                               "keys go to the text; only Save, Open, New Window, Close and Quit still work."),
                            shortcutPage);
    note->setWordWrap(true);
    shortcutLayout->addWidget(note);
    m_tabs->addTab(shortcutPage, tr("Shortcuts"));

    // About.
    auto *aboutPage = new QWidget(m_tabs);
    auto *aboutLayout = new QVBoxLayout(aboutPage);
    auto *about = new QLabel(aboutText, aboutPage);
    about->setTextFormat(Qt::PlainText);
    about->setTextInteractionFlags(Qt::TextSelectableByMouse);
    about->setWordWrap(true);
    aboutLayout->addWidget(about);
    aboutLayout->addStretch();
    auto *website = new QPushButton(tr("Open Project Website"), aboutPage);
    website->setObjectName(u"websiteButton"_s);
    website->setToolTip(tr("Opens the project page in your web browser. Glacier Ref itself stays offline."));
    connect(website, &QPushButton::clicked, this, [openWebsite] {
        if (openWebsite)
            openWebsite();
    });
    aboutLayout->addWidget(website, 0, Qt::AlignLeft);
    m_tabs->addTab(aboutPage, tr("About"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(720, 560);
}

void HelpDialog::showPage(Page page)
{
    m_tabs->setCurrentIndex(int(page));
}

} // namespace glacier
