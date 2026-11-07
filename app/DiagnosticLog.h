#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace glacier::diagnostics {

// Local diagnostics (M4.09): a Qt message handler that writes redacted lines
// to stderr and to a small per-process log file in the application data
// directory. One file per process (concurrent processes never interleave);
// each file stops growing at maxFileBytes, and only the newest keepFiles
// files are kept. Nothing leaves the machine.
struct LogOptions
{
    QString directory;                    // e.g. <AppDataLocation>/logs
    qint64 maxFileBytes = 1024 * 1024;
    int keepFiles = 5;
    bool toStderr = true;
};

// Installs the handler; returns false if the log file can't be created
// (stderr still works).
bool installLog(const LogOptions &options);
void uninstallLog();
// The current process's log file, or empty.
QString logFilePath();

// One log line: "2026-11-07T10:00:00.123Z warning glacier.files: message".
QString formatLine(QtMsgType type, const char *category, const QString &message, const QDateTime &time);

} // namespace glacier::diagnostics
