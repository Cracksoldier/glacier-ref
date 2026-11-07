#include "glacier/core/Diagnostics.h"

#include <QDir>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcApp, "glacier.app", QtInfoMsg)
Q_LOGGING_CATEGORY(lcFiles, "glacier.files", QtInfoMsg)
Q_LOGGING_CATEGORY(lcRecovery, "glacier.recovery", QtInfoMsg)
Q_LOGGING_CATEGORY(lcImport, "glacier.import", QtInfoMsg)

namespace glacier::diagnostics {

QString redact(const QString &text, const QString &homeDir, const QString &userName)
{
    QString out = text;
    if (!homeDir.isEmpty() && homeDir != u"/"_s) {
        const QString forward = QDir::fromNativeSeparators(homeDir);
        const QString backward = QString(forward).replace(u'/', u'\\');
        for (const QString &home : {forward, backward})
            out.replace(home, u"~"_s, Qt::CaseInsensitive);
    }
    // Short names would match ordinary words; only redact real-looking ones.
    if (userName.size() >= 3) {
        const QRegularExpression word(u"(?<![\\w.-])"_s + QRegularExpression::escape(userName) + u"(?![\\w-])"_s,
                                      QRegularExpression::CaseInsensitiveOption);
        out.replace(word, u"<user>"_s);
    }
    return out;
}

QString redact(const QString &text)
{
    static const QString home = QDir::homePath();
    static const QString user = [] {
        QString name = qEnvironmentVariable("USER");
        if (name.isEmpty())
            name = qEnvironmentVariable("USERNAME");
        return name;
    }();
    return redact(text, home, user);
}

} // namespace glacier::diagnostics
