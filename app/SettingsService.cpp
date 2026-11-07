#include "SettingsService.h"

#include <QSettings>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace glacier {

namespace {

const QString MenuBarKey = u"ui/menuBarVisible"_s;
const QString FirstRunKey = u"ui/firstRunHintShown"_s;
const QString ThemeKey = u"appearance/theme"_s;
const QString BackgroundKey = u"appearance/newBoardBackground"_s;
const QString ShortcutGroup = u"shortcuts"_s;
const QString RecoveryIntervalKey = u"recovery/intervalSeconds"_s;
const QString RecoveryBudgetKey = u"recovery/budgetMiB"_s;
const QString CacheBudgetKey = u"cache/budgetMiB"_s;
const QString GeometryKey = u"window/geometry"_s;
const QString FramelessKey = u"window/frameless"_s;

// Stored as portable text, one sequence per line; an empty string means
// "no shortcut".
QString encode(const QList<QKeySequence> &keys)
{
    QStringList parts;
    for (const QKeySequence &k : keys)
        if (!k.isEmpty())
            parts.append(k.toString(QKeySequence::PortableText));
    return parts.join(u'\n');
}

QList<QKeySequence> decode(const QString &text)
{
    QList<QKeySequence> keys;
    for (const QString &part : text.split(u'\n', Qt::SkipEmptyParts)) {
        const QKeySequence k = QKeySequence::fromString(part, QKeySequence::PortableText);
        if (!k.isEmpty())
            keys.append(k);
    }
    return keys;
}

int clampedInt(const QString &key, int fallback, int min, int max)
{
    bool ok = false;
    const int value = QSettings().value(key, fallback).toInt(&ok);
    return std::clamp(ok ? value : fallback, min, max);
}

} // namespace

SettingsService::SettingsService(QObject *parent)
    : QObject(parent)
{
}

bool SettingsService::menuBarVisible() const
{
    return QSettings().value(MenuBarKey, false).toBool();
}

void SettingsService::setMenuBarVisible(bool visible)
{
    if (visible == menuBarVisible())
        return;
    QSettings().setValue(MenuBarKey, visible);
    emit menuBarVisibleChanged(visible);
}

bool SettingsService::firstRunHintShown() const
{
    return QSettings().value(FirstRunKey, false).toBool();
}

void SettingsService::setFirstRunHintShown()
{
    QSettings().setValue(FirstRunKey, true);
}

SettingsService::Theme SettingsService::theme() const
{
    return QSettings().value(ThemeKey).toString() == u"system"_s ? Theme::System : Theme::Glacier;
}

void SettingsService::setTheme(Theme theme)
{
    if (theme == this->theme())
        return;
    QSettings().setValue(ThemeKey, theme == Theme::System ? u"system"_s : u"glacier"_s);
    emit appearanceChanged();
}

quint32 SettingsService::newBoardBackground() const
{
    bool ok = false;
    const quint32 value = QSettings().value(BackgroundKey).toString().toUInt(&ok, 16);
    return ok ? (value | 0xFF000000u) : DefaultBoardBackground;
}

void SettingsService::setNewBoardBackground(quint32 argb)
{
    argb |= 0xFF000000u; // boards have opaque backgrounds
    if (argb == newBoardBackground())
        return;
    QSettings().setValue(BackgroundKey, QString::number(argb, 16));
    emit appearanceChanged();
}

SettingsService::ShortcutMap SettingsService::shortcutOverrides() const
{
    ShortcutMap map;
    QSettings settings;
    settings.beginGroup(ShortcutGroup);
    for (const QString &id : settings.childKeys())
        map.insert(id, decode(settings.value(id).toString()));
    return map;
}

void SettingsService::setShortcutOverrides(const ShortcutMap &overrides)
{
    if (overrides == shortcutOverrides())
        return;
    QSettings settings;
    settings.remove(ShortcutGroup);
    settings.beginGroup(ShortcutGroup);
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        settings.setValue(it.key(), encode(it.value()));
    settings.endGroup();
    emit shortcutsChanged();
}

QList<QKeySequence> SettingsService::shortcuts(const QString &actionId, const QList<QKeySequence> &defaults) const
{
    QSettings settings;
    const QString key = ShortcutGroup + u'/' + actionId;
    return settings.contains(key) ? decode(settings.value(key).toString()) : defaults;
}

QHash<QKeySequence, QStringList> SettingsService::conflicts(const ShortcutMap &effective)
{
    QHash<QKeySequence, QStringList> users;
    for (auto it = effective.cbegin(); it != effective.cend(); ++it)
        for (const QKeySequence &k : it.value())
            if (!k.isEmpty() && !users[k].contains(it.key()))
                users[k].append(it.key());
    QHash<QKeySequence, QStringList> result;
    for (auto it = users.cbegin(); it != users.cend(); ++it) {
        if (it.value().size() > 1) {
            QStringList ids = it.value();
            ids.sort();
            result.insert(it.key(), ids);
        }
    }
    return result;
}

int SettingsService::recoveryIntervalSeconds() const
{
    return clampedInt(RecoveryIntervalKey, DefaultRecoveryIntervalSeconds, MinRecoveryIntervalSeconds,
                      MaxRecoveryIntervalSeconds);
}

void SettingsService::setRecoveryIntervalSeconds(int seconds)
{
    seconds = std::clamp(seconds, MinRecoveryIntervalSeconds, MaxRecoveryIntervalSeconds);
    if (seconds == recoveryIntervalSeconds())
        return;
    QSettings().setValue(RecoveryIntervalKey, seconds);
    emit recoveryChanged();
}

int SettingsService::recoveryBudgetMiB() const
{
    return clampedInt(RecoveryBudgetKey, DefaultRecoveryBudgetMiB, MinRecoveryBudgetMiB, MaxRecoveryBudgetMiB);
}

void SettingsService::setRecoveryBudgetMiB(int mib)
{
    mib = std::clamp(mib, MinRecoveryBudgetMiB, MaxRecoveryBudgetMiB);
    if (mib == recoveryBudgetMiB())
        return;
    QSettings().setValue(RecoveryBudgetKey, mib);
    emit recoveryChanged();
}

int SettingsService::cacheBudgetMiB() const
{
    return clampedInt(CacheBudgetKey, DefaultCacheBudgetMiB, MinCacheBudgetMiB, MaxCacheBudgetMiB);
}

void SettingsService::setCacheBudgetMiB(int mib)
{
    mib = std::clamp(mib, MinCacheBudgetMiB, MaxCacheBudgetMiB);
    if (mib == cacheBudgetMiB())
        return;
    QSettings().setValue(CacheBudgetKey, mib);
    emit cacheBudgetChanged(mib);
}

QByteArray SettingsService::windowGeometry() const
{
    return QSettings().value(GeometryKey).toByteArray();
}

void SettingsService::setWindowGeometry(const QByteArray &geometry)
{
    QSettings().setValue(GeometryKey, geometry);
}

bool SettingsService::frameless() const
{
    return QSettings().value(FramelessKey, false).toBool();
}

void SettingsService::setFrameless(bool frameless)
{
    QSettings().setValue(FramelessKey, frameless);
}

} // namespace glacier
