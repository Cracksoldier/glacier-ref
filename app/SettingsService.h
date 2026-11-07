#pragma once

#include <QByteArray>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace glacier {

// Typed access to the user's preferences (spec §6 SettingsService, M4.02),
// stored with QSettings. One instance per process (owned by AppServices);
// windows follow its change signals, so every open window updates at once.
class SettingsService : public QObject
{
    Q_OBJECT

public:
    enum class Theme { Glacier, System };

    static constexpr int DefaultRecoveryIntervalSeconds = 60;
    static constexpr int MinRecoveryIntervalSeconds = 10;
    static constexpr int MaxRecoveryIntervalSeconds = 3600;
    static constexpr int DefaultRecoveryBudgetMiB = 2048;
    static constexpr int MinRecoveryBudgetMiB = 256;
    static constexpr int MaxRecoveryBudgetMiB = 32768;
    static constexpr int DefaultCacheBudgetMiB = 512;
    static constexpr int MinCacheBudgetMiB = 128;
    static constexpr int MaxCacheBudgetMiB = 8192;
    static constexpr quint32 DefaultBoardBackground = 0xFF3A3D41u;

    explicit SettingsService(QObject *parent = nullptr);

    // Minimal UI (spec §3).
    bool menuBarVisible() const;
    void setMenuBarVisible(bool visible);
    bool firstRunHintShown() const;
    void setFirstRunHintShown();

    // Appearance.
    Theme theme() const;
    void setTheme(Theme theme);
    quint32 newBoardBackground() const; // opaque ARGB
    void setNewBoardBackground(quint32 argb);

    // Shortcuts: overrides by action ID; an action without an override uses
    // its default. An override may be empty (no shortcut).
    using ShortcutMap = QHash<QString, QList<QKeySequence>>;
    ShortcutMap shortcutOverrides() const;
    void setShortcutOverrides(const ShortcutMap &overrides);
    QList<QKeySequence> shortcuts(const QString &actionId, const QList<QKeySequence> &defaults) const;
    // Key sequences assigned to more than one action, with those actions.
    static QHash<QKeySequence, QStringList> conflicts(const ShortcutMap &effective);

    // Recovery and cache (clamped to the ranges above).
    int recoveryIntervalSeconds() const;
    void setRecoveryIntervalSeconds(int seconds);
    int recoveryBudgetMiB() const;
    void setRecoveryBudgetMiB(int mib);
    int cacheBudgetMiB() const;
    void setCacheBudgetMiB(int mib);

    // Window (local preferences, spec §5: never stored in boards).
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);
    bool frameless() const;
    void setFrameless(bool frameless);

signals:
    void menuBarVisibleChanged(bool visible);
    void shortcutsChanged();
    void appearanceChanged();
    void cacheBudgetChanged(int mib);
    void recoveryChanged();
};

} // namespace glacier
