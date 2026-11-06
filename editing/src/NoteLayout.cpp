#include "glacier/editing/NoteLayout.h"

#include <QFontMetricsF>
#include <QTextLayout>

#include <algorithm>
#include <cmath>

namespace glacier::editing {

QFont NoteLayout::font(double fontSize)
{
    QFont f;
    f.setPixelSize(std::max(1, int(std::lround(fontSize))));
    return f;
}

double NoteLayout::height(const QString &text, double width, double fontSize)
{
    const QFont f = font(fontSize);
    const QFontMetricsF metrics(f);
    const double lineWidth = std::max(1.0, width - 2 * Padding);
    double textHeight = 0.0;
    const QStringList paragraphs = text.split(u'\n');
    for (const QString &paragraph : paragraphs) {
        QTextLayout layout(paragraph, f);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        int lines = 0;
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(lineWidth);
            ++lines;
        }
        layout.endLayout();
        textHeight += std::max(1, lines) * metrics.lineSpacing();
    }
    return std::ceil(textHeight + 2 * Padding);
}

} // namespace glacier::editing
