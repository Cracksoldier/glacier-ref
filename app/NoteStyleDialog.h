#pragma once

#include <QColor>
#include <QDialog>

class QPushButton;
class QSpinBox;

namespace glacier {

// Font size, text colour and background colour for notes (spec §3).
class NoteStyleDialog : public QDialog
{
    Q_OBJECT

public:
    NoteStyleDialog(double fontSize, quint32 textColor, quint32 backgroundColor, QWidget *parent = nullptr);

    double fontSize() const;
    quint32 textColor() const { return m_text.rgba(); }
    quint32 backgroundColor() const { return m_background.rgba(); }

private:
    void pick(QColor &color, QPushButton *button, const QString &title);
    static void paintSwatch(QPushButton *button, const QColor &color);

    QSpinBox *m_size = nullptr;
    QPushButton *m_textButton = nullptr;
    QPushButton *m_backgroundButton = nullptr;
    QColor m_text;
    QColor m_background;
};

} // namespace glacier
