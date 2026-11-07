#include "glacier/core/LinkPaths.h"

#include <QStringList>

using namespace Qt::StringLiterals;

namespace glacier::link_paths {

namespace {

// Root-insensitive comparison: drive letters and UNC names are compared
// case-insensitively, as Windows does.
bool sameRoot(const QString &a, const QString &b)
{
    if (a == u"/"_s || b == u"/"_s)
        return a == b;
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

QStringList segments(const QString &normalizedPath, const QString &root)
{
    return normalizedPath.mid(root.size()).split(u'/', Qt::SkipEmptyParts);
}

} // namespace

QString root(const QString &path)
{
    const QString p = QString(path).replace(u'\\', u'/');
    if (p.startsWith(u"//"_s)) {
        // //server/share/...
        const qsizetype serverEnd = p.indexOf(u'/', 2);
        if (serverEnd <= 2)
            return {};
        qsizetype shareEnd = p.indexOf(u'/', serverEnd + 1);
        if (shareEnd == serverEnd + 1)
            return {};
        if (shareEnd < 0)
            shareEnd = p.size();
        return p.left(shareEnd) + u'/';
    }
    if (p.size() >= 3 && p[1] == u':' && p[2] == u'/' && p[0].isLetter() && p[0].unicode() < 128)
        return p.left(3);
    if (p.startsWith(u'/'))
        return u"/"_s;
    return {};
}

bool isAbsolute(const QString &path)
{
    return !root(path).isEmpty();
}

QString normalize(const QString &path)
{
    const QString p = QString(path).replace(u'\\', u'/');
    const QString r = root(p);
    QStringList out;
    for (const QString &segment : segments(p, r)) {
        if (segment == u"."_s)
            continue;
        if (segment == u".."_s) {
            if (!out.isEmpty() && out.last() != u".."_s)
                out.removeLast();
            else if (r.isEmpty())
                out.append(segment); // leading ".." of a relative path
            continue;                // ".." above an absolute root is dropped
        }
        out.append(segment);
    }
    if (r.isEmpty())
        return out.isEmpty() ? u"."_s : out.join(u'/');
    return r + out.join(u'/');
}

StoredPath toStored(const QString &absoluteTarget, const QString &boardDir)
{
    const QString target = normalize(absoluteTarget);
    const QString targetRoot = root(target);
    const QString dir = boardDir.isEmpty() ? QString() : normalize(boardDir);
    const QString dirRoot = root(dir);
    if (targetRoot.isEmpty() || dirRoot.isEmpty() || !sameRoot(targetRoot, dirRoot))
        return {target, false};

    const bool caseInsensitive = targetRoot != u"/"_s;
    const QStringList t = segments(target, targetRoot);
    const QStringList d = segments(dir, dirRoot);
    qsizetype common = 0;
    // The last target segment is the file name; it is never a shared directory.
    while (common < d.size() && common + 1 < t.size()
           && t[common].compare(d[common], caseInsensitive ? Qt::CaseInsensitive : Qt::CaseSensitive) == 0)
        ++common;
    if (common == 0)
        return {target, false}; // only the root in common: keep it absolute

    QStringList relative;
    for (qsizetype i = common; i < d.size(); ++i)
        relative.append(u".."_s);
    relative.append(t.mid(common));
    return {relative.join(u'/'), true};
}

QString resolve(const StoredPath &stored, const QString &boardDir)
{
    if (stored.path.isEmpty() || stored.path.contains(QChar(0)))
        return {};
    if (!stored.relative)
        return isAbsolute(stored.path) ? normalize(stored.path) : QString();
    if (isAbsolute(stored.path) || boardDir.isEmpty() || !isAbsolute(boardDir))
        return {};
    return normalize(boardDir + u'/' + stored.path);
}

} // namespace glacier::link_paths
