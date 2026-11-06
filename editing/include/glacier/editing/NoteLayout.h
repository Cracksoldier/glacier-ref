#pragma once

#include <QFont>
#include <QSizeF>
#include <QString>

namespace glacier::editing {

// Shared text layout for notes, used both to paint them and to compute the
// height stored in the model (spec §3: wrapping plain text).
struct NoteLayout
{
    static constexpr double Padding = 10.0;
    static constexpr double MinWidth = 40.0;
    static constexpr double MaxWidth = 100000.0;
    static constexpr double DefaultWidth = 240.0;
    static constexpr double DefaultFontSize = 16.0;

    static QFont font(double fontSize);
    // Height for wrapping `text` at `width` (both in board units), including padding.
    static double height(const QString &text, double width, double fontSize);
};

} // namespace glacier::editing
