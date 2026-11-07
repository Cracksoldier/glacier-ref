// Writes the compatibility fixtures in tests/fixtures/boards (M3.11):
//
//   glacier_test_boardfixtures <output dir>
//
// Run it only to create fixtures for a new schema version; the committed
// files are the compatibility contract and must never be regenerated for an
// existing version. tst_compatibility loads them on every platform.
#include "TestBoardFiles.h"

#include "glacier/core/LinkPaths.h"
#include "glacier/persistence/BoardSerializer.h"
#include "glacier/persistence/ZipArchive.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::persistence;

namespace {

bool save(const BoardContent &content, const std::shared_ptr<SessionAssetStore> &store, const QString &path)
{
    SaveRequest request;
    request.content = content;
    request.path = path;
    request.store = store;
    request.generator = u"Glacier Ref fixture generator"_s;
    request.previewPng = test::encode(test::markedImage({16, 8}, QColor(90, 90, 90)), "png");
    const SaveResult result = saveBoard(request);
    if (!result.ok)
        QTextStream(stderr) << path << u": "_s << result.error << Qt::endl;
    return result.ok;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        QTextStream(stderr) << "usage: glacier_test_boardfixtures <output dir>" << Qt::endl;
        return 2;
    }
    if (!QDir().mkpath(QString::fromLocal8Bit(argv[1])))
        return 1;
    const QDir out(QDir(QString::fromLocal8Bit(argv[1])).absolutePath()); // links need absolute paths
    if (!out.mkpath(u"refs"_s))
        return 1;
    test::StoreFixture fixture;

    // 1. Every embedded feature.
    if (!save(test::richBoard(*fixture.store), fixture.store, out.filePath(u"v1-rich.glacierref"_s)))
        return 1;

    // 2. A relative link to refs/linked.png next to the board.
    const QString linked = out.filePath(u"refs/linked.png"_s);
    QFile file(linked);
    if (!file.open(QIODevice::WriteOnly) || file.write(test::encode(test::markedImage({20, 10}, Qt::yellow), "png")) < 0)
        return 1;
    file.close();
    if (!save(test::richBoard(*fixture.store, link_paths::normalize(linked)), fixture.store,
              out.filePath(u"v1-linked.glacierref"_s)))
        return 1;

    // 3. A board from a future schema version, which must be refused.
    QJsonObject manifest = QJsonDocument::fromJson(writeManifest(test::richBoard(*fixture.store), {})).object();
    manifest.insert(u"schemaVersion"_s, 2);
    manifest.insert(u"newRequiredFeature"_s, u"something schema 1 cannot show"_s);
    QFile future(out.filePath(u"v2-future.glacierref"_s));
    if (!future.open(QIODevice::WriteOnly))
        return 1;
    ZipWriter writer(&future);
    if (!writer.add(u"manifest.json"_s, QJsonDocument(manifest).toJson(), true) || !writer.finish())
        return 1;
    future.close();

    QTextStream(stdout) << "fixtures written to " << out.absolutePath() << Qt::endl;
    return 0;
}
