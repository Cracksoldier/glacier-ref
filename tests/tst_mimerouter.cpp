#include "GlacierTestMain.h"

#include "glacier/io/MimeRouter.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QObject>
#include <QTemporaryDir>
#include <QUrl>

using namespace Qt::StringLiterals;
using namespace glacier::io;

namespace {

QString touch(const QDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write("x");
    return path;
}

QImage sampleImage()
{
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    return image;
}

} // namespace

class TestMimeRouter : public QObject
{
    Q_OBJECT

private slots:
    void localFilesWinOverImageData()
    {
        QTemporaryDir dir;
        const QString a = touch(QDir(dir.path()), u"a.png"_s);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(a)});
        mime.setImageData(sampleImage()); // same drag also offers pixels
        const ImportPlan plan = routeMimeData(mime);
        QCOMPARE(plan.sources.size(), 1);
        QCOMPARE(plan.sources.first().kind, ImportSource::Kind::File);
        QCOMPARE(plan.sources.first().displayName, u"a.png"_s);
        QVERIFY(plan.blockingMessage.isEmpty());
    }

    void imageDataOnly()
    {
        QMimeData mime;
        mime.setImageData(sampleImage());
        const ImportPlan plan = routeMimeData(mime);
        QCOMPARE(plan.sources.size(), 1);
        QCOMPARE(plan.sources.first().kind, ImportSource::Kind::ClipboardImage);
        QCOMPARE(plan.sources.first().image.size(), QSize(4, 4));
    }

    void onlineOnlyIsRefusedWithSpecMessage_data()
    {
        QTest::addColumn<bool>("asUrl");
        QTest::newRow("uri-list") << true;
        QTest::newRow("plain text") << false;
    }

    void onlineOnlyIsRefusedWithSpecMessage()
    {
        QFETCH(bool, asUrl);
        QMimeData mime;
        if (asUrl)
            mime.setUrls({QUrl(u"https://example.com/picture.jpg"_s)});
        else
            mime.setText(u"http://example.com/picture.jpg"_s);
        const ImportPlan plan = routeMimeData(mime);
        QVERIFY(plan.sources.isEmpty());
        QCOMPARE(plan.blockingMessage,
                 u"Online image links are not supported. Download the image or copy its image data first."_s);
    }

    void onlineLinkWithPixelsUsesPixels()
    {
        // Browsers often offer both the link and the image data.
        QMimeData mime;
        mime.setUrls({QUrl(u"https://example.com/picture.jpg"_s)});
        mime.setImageData(sampleImage());
        const ImportPlan plan = routeMimeData(mime);
        QCOMPARE(plan.sources.size(), 1);
        QCOMPARE(plan.sources.first().kind, ImportSource::Kind::ClipboardImage);
    }

    void mixedLocalAndRemote()
    {
        QTemporaryDir dir;
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(touch(QDir(dir.path()), u"b.jpg"_s)),
                      QUrl(u"http://example.com/c.png"_s), QUrl(u"ftp://example.com/d.png"_s)});
        const ImportPlan plan = routeMimeData(mime);
        QCOMPARE(plan.sources.size(), 1);
        QCOMPARE(plan.notices.size(), 2);
        QVERIFY(plan.notices.contains(onlineImagesNotSupportedMessage()));
    }

    void folderImportsTopLevelOnly()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        QVERIFY(root.mkpath(u"refs/sub"_s));
        QDir refs(root.filePath(u"refs"_s));
        touch(refs, u"b.png"_s);
        touch(refs, u"A.jpg"_s);
        touch(refs, u"notes.txt"_s);
        touch(QDir(refs.filePath(u"sub"_s)), u"deep.png"_s);

        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(refs.path())});
        const ImportPlan plan = routeMimeData(mime);
        QCOMPARE(plan.sources.size(), 2);
        QCOMPARE(plan.sources[0].displayName, u"A.jpg"_s); // sorted, case-insensitive
        QCOMPARE(plan.sources[1].displayName, u"b.png"_s);
        QCOMPARE(plan.notices.size(), 2);
        QVERIFY(plan.notices[0].contains(u"Subfolders"_s));
        QVERIFY(plan.notices[1].contains(u"1 file"_s));
    }

    void unicodeAndSpacesInPaths()
    {
        QTemporaryDir dir;
        const QString path = touch(QDir(dir.path()), u"Glacier Ref Größe 参考.png"_s);
        QVERIFY(QFile::exists(path));
        const ImportPlan plan = planForPaths({path});
        QCOMPARE(plan.sources.size(), 1);
        QCOMPARE(plan.sources.first().filePath, path);
        QCOMPARE(plan.sources.first().displayName, u"Glacier Ref Größe 参考.png"_s);

        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        QCOMPARE(routeMimeData(mime).sources.first().filePath, path);
    }

    void missingFileIsReported()
    {
        const ImportPlan plan = planForPaths({u"/definitely/not/here.png"_s});
        QVERIFY(plan.sources.isEmpty());
        QVERIFY(plan.blockingMessage.contains(u"not found"_s));
    }

    void nothingImportable()
    {
        QMimeData mime;
        mime.setText(u"just some words"_s);
        const ImportPlan plan = routeMimeData(mime);
        QVERIFY(plan.sources.isEmpty());
        QVERIFY(!plan.blockingMessage.isEmpty());
    }
};

GLACIER_TEST_GUI_MAIN(TestMimeRouter)
#include "tst_mimerouter.moc"
