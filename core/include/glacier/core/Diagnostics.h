#pragma once

#include <QLoggingCategory>
#include <QString>

// Logging categories and redaction (spec §9, M4.09). Default logs never
// contain note text or image bytes, and personal paths are redacted.
//
// Categories log info and above by default; enable debug output with
// QT_LOGGING_RULES="glacier.*.debug=true".
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcFiles)
Q_DECLARE_LOGGING_CATEGORY(lcRecovery)
Q_DECLARE_LOGGING_CATEGORY(lcImport)

namespace glacier::diagnostics {

// Replaces the home directory with "~" (with either separator) and the user
// name with "<user>", so messages and reports carry no personal paths.
QString redact(const QString &text);
// The same with explicit values (tests).
QString redact(const QString &text, const QString &homeDir, const QString &userName);

} // namespace glacier::diagnostics
