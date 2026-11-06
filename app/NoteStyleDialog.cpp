#include "NoteStyleDialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace glacier {

NoteStyleDialog::NoteStyleDialog(double fontSize, quint32 textColor, quint32 backgroundColor, QWidget *parent)
    : QDialog(parent)
    , m_text(QColor::fromRgba(textColor))
    , m_background(QColor::fromRgba(backgroundColor))
{
    setWindowTitle(tr("Note Style"));
    m_size = new QSpinBox(this);
    m_size->setRange(4, 400);
    m_size->setValue(int(std::lround(fontSize)));
    m_textButton = new QPushButton(this);
    m_backgroundButton = new QPushButton(this);
    paintSwatch(m_textButton, m_text);
    paintSwatch(m_backgroundButton, m_background);
    connect(m_textButton, &QPushButton::clicked, this, [this] { pick(m_text, m_textButton, tr("Text Colour")); });
    connect(m_backgroundButton, &QPushButton::clicked, this,
            [this] { pick(m_background, m_backgroundButton, tr("Background Colour")); });

    auto *form = new QFormLayout;
    form->addRow(tr("Font size:"), m_size);
    form->addRow(tr("Text colour:"), m_textButton);
    form->addRow(tr("Background:"), m_backgroundButton);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

double NoteStyleDialog::fontSize() const
{
    return m_size->value();
}

void NoteStyleDialog::pick(QColor &color, QPushButton *button, const QString &title)
{
    const QColor chosen = QColorDialog::getColor(color, this, title, QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) {
        color = chosen;
        paintSwatch(button, color);
    }
}

void NoteStyleDialog::paintSwatch(QPushButton *button, const QColor &color)
{
    QPixmap swatch(48, 16);
    swatch.fill(color);
    button->setIcon(swatch);
    button->setIconSize(swatch.size());
    button->setText(color.name(QColor::HexArgb));
}

} // namespace glacier
