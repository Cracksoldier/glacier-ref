#include "Theme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>

using namespace Qt::StringLiterals;

namespace glacier::theme {

namespace {
QString &systemStyleName()
{
    static QString *name = new QString;
    return *name;
}
} // namespace

void rememberSystemStyle()
{
    if (systemStyleName().isEmpty() && QApplication::style())
        systemStyleName() = QApplication::style()->name();
}

QPalette glacierPalette()
{
    const QColor window(0x20, 0x23, 0x27);
    const QColor base(0x2a, 0x2e, 0x33);
    const QColor alternate(0x31, 0x35, 0x3b);
    const QColor text(0xe6, 0xe6, 0xe6);
    const QColor disabled(0x7c, 0x82, 0x8a);
    const QColor accent(0x5f, 0xa8, 0xd3); // glacier blue
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, alternate);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x45, 0x4a, 0x51));
    p.setColor(QPalette::Midlight, QColor(0x3a, 0x3f, 0x45));
    p.setColor(QPalette::Mid, QColor(0x2f, 0x33, 0x38));
    p.setColor(QPalette::Dark, QColor(0x18, 0x1a, 0x1d));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, QColor(0x10, 0x12, 0x14));
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::LinkVisited, accent.darker(120));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x3a, 0x3f, 0x45));
    return p;
}

void apply(SettingsService::Theme theme)
{
    rememberSystemStyle();
    if (theme == SettingsService::Theme::Glacier) {
        QApplication::setStyle(QStyleFactory::create(u"Fusion"_s));
        QApplication::setPalette(glacierPalette());
        return;
    }
    QStyle *system = systemStyleName().isEmpty() ? nullptr : QStyleFactory::create(systemStyleName());
    QApplication::setStyle(system ? system : QStyleFactory::create(u"Fusion"_s));
    // An empty palette (nothing resolved) falls back to the platform's own,
    // e.g. the KDE color scheme.
    QApplication::setPalette(QPalette());
}

} // namespace glacier::theme
