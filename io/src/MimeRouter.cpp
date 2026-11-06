#include "glacier/io/MimeRouter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QSet>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace glacier::io {

namespace {

QString tr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("glacier::io", text, disambiguation, n);
}

bool isRemote(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == u"http"_s || scheme == u"https"_s;
}

const QSet<QString> &imageSuffixes()
{
    static const QSet<QString> set = {
        u"png"_s, u"jpg"_s, u"jpeg"_s, u"jpe"_s, u"webp"_s, u"bmp"_s, u"gif"_s, u"tif"_s, u"tiff"_s, u"svg"_s,
    };
    return set;
}

void addPath(ImportPlan &plan, const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        const QDir dir(info.absoluteFilePath());
        const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        int otherFiles = 0;
        for (const QFileInfo &file : files) {
            if (imageSuffixes().contains(file.suffix().toLower()))
                plan.sources.append({ImportSource::Kind::File, file.absoluteFilePath(), {}, file.fileName()});
            else
                ++otherFiles;
        }
        const QStringList subfolders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (!subfolders.isEmpty())
            plan.notices.append(tr("Subfolders of \"%1\" were not imported.").arg(info.fileName()));
        if (otherFiles > 0)
            plan.notices.append(tr("%n file(s) in \"%1\" were skipped because they are not images.", nullptr, otherFiles)
                                    .arg(info.fileName()));
        return;
    }
    if (!info.exists()) {
        plan.notices.append(tr("\"%1\" was not found.").arg(info.fileName()));
        return;
    }
    plan.sources.append({ImportSource::Kind::File, info.absoluteFilePath(), {}, info.fileName()});
}

} // namespace

QString onlineImagesNotSupportedMessage()
{
    return tr("Online image links are not supported. Download the image or copy its image data first.");
}

ImportPlan planForPaths(const QStringList &paths)
{
    ImportPlan plan;
    for (const QString &path : paths)
        addPath(plan, path);
    if (plan.sources.isEmpty() && plan.blockingMessage.isEmpty())
        plan.blockingMessage = plan.notices.isEmpty() ? tr("There is nothing to import.") : plan.notices.join(u'\n');
    return plan;
}

ImportPlan routeMimeData(const QMimeData &mime)
{
    ImportPlan plan;
    QStringList localPaths;
    int remote = 0;
    int other = 0;

    for (const QUrl &url : mime.urls()) {
        if (url.isLocalFile())
            localPaths.append(url.toLocalFile());
        else if (isRemote(url))
            ++remote;
        else
            ++other;
    }
    if (mime.urls().isEmpty() && mime.hasText()) {
        const QUrl url(mime.text().trimmed(), QUrl::StrictMode);
        if (url.isValid() && isRemote(url))
            ++remote;
    }

    if (!localPaths.isEmpty()) {
        // Local files win: ignore pixel data of the same drag (no duplicates).
        for (const QString &path : std::as_const(localPaths))
            addPath(plan, path);
        if (remote > 0)
            plan.notices.append(onlineImagesNotSupportedMessage());
        if (other > 0)
            plan.notices.append(tr("%n link(s) of an unsupported kind were skipped.", nullptr, other));
        if (plan.sources.isEmpty())
            plan.blockingMessage = plan.notices.join(u'\n');
        return plan;
    }

    if (mime.hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime.imageData());
        if (!image.isNull()) {
            plan.sources.append({ImportSource::Kind::ClipboardImage, {}, image, tr("Pasted image")});
            return plan;
        }
    }

    if (remote > 0)
        plan.blockingMessage = onlineImagesNotSupportedMessage();
    else if (other > 0)
        plan.blockingMessage = tr("This kind of link cannot be imported.");
    else
        plan.blockingMessage = tr("There is no image to import.");
    return plan;
}

} // namespace glacier::io
