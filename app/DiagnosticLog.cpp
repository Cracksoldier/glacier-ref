#include "DiagnosticLog.h"

#include "glacier/core/Diagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>

#include <cstdio>
#include <memory>

using namespace Qt::StringLiterals;

namespace glacier::diagnostics {

namespace {

struct LogState
{
    QMutex mutex;
    LogOptions options;
    std::unique_ptr<QFile> file;
    qint64 written = 0;
    bool capped = false;
    QtMessageHandler previous = nullptr;
};

LogState &state()
{
    // Never destroyed: messages may still arrive during static destruction.
    static LogState *s = new LogState;
    return *s;
}

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "debug";
    case QtInfoMsg:
        return "info";
    case QtWarningMsg:
        return "warning";
    case QtCriticalMsg:
        return "critical";
    case QtFatalMsg:
        return "fatal";
    }
    return "info";
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString line = formatLine(type, context.category, message, QDateTime::currentDateTimeUtc());
    const QByteArray bytes = line.toUtf8() + '\n';
    LogState &s = state();
    {
        QMutexLocker locker(&s.mutex);
        if (s.options.toStderr) {
            std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
            std::fflush(stderr);
        }
        if (s.file && !s.capped) {
            if (s.written + bytes.size() > s.options.maxFileBytes) {
                const QByteArray note = "-- log size limit reached; further messages go to stderr only --\n";
                s.file->write(note);
                s.capped = true;
            } else {
                s.file->write(bytes);
                s.written += bytes.size();
            }
            s.file->flush();
        }
    }
    if (type == QtFatalMsg)
        std::abort();
}

void pruneOldLogs(const QString &directory, int keep)
{
    QFileInfoList logs = QDir(directory).entryInfoList({u"glacier-ref-*.log"_s}, QDir::Files, QDir::Time);
    for (qsizetype i = keep; i < logs.size(); ++i) // newest first
        QFile::remove(logs[i].absoluteFilePath());
}

} // namespace

QString formatLine(QtMsgType type, const char *category, const QString &message, const QDateTime &time)
{
    const QString cat = category && *category ? QString::fromLatin1(category) : u"default"_s;
    return u"%1 %2 %3: %4"_s.arg(time.toUTC().toString(Qt::ISODateWithMs), QString::fromLatin1(levelName(type)), cat,
                                 redact(message));
}

bool installLog(const LogOptions &options)
{
    LogState &s = state();
    QMutexLocker locker(&s.mutex);
    s.options = options;
    s.file.reset();
    s.written = 0;
    s.capped = false;
    bool ok = false;
    if (!options.directory.isEmpty() && QDir().mkpath(options.directory)) {
        // Keep room for this process's file among the newest ones.
        pruneOldLogs(options.directory, std::max(0, options.keepFiles - 1));
        const QString name = u"glacier-ref-%1-%2.log"_s.arg(
            QDateTime::currentDateTimeUtc().toString(u"yyyyMMdd-HHmmss"_s)).arg(QCoreApplication::applicationPid());
        auto file = std::make_unique<QFile>(QDir(options.directory).filePath(name));
        if (file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            s.file = std::move(file);
            ok = true;
        }
    }
    QtMessageHandler previous = qInstallMessageHandler(handler);
    if (previous != handler)
        s.previous = previous;
    return ok;
}

void uninstallLog()
{
    LogState &s = state();
    qInstallMessageHandler(s.previous);
    QMutexLocker locker(&s.mutex);
    s.file.reset();
}

QString logFilePath()
{
    LogState &s = state();
    QMutexLocker locker(&s.mutex);
    return s.file ? s.file->fileName() : QString();
}

} // namespace glacier::diagnostics
