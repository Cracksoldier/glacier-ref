// Generates the reproducible benchmark datasets (spec §9, milestone M1.11):
//
//   standard/  300 distinct 2048x2048 JPEG images
//   stress/    1,000 mixed-format, mixed-size images (PNG, JPEG, WebP, BMP,
//              TIFF, SVG), including a few large JPEGs. GIF is absent because
//              Qt cannot write it; GIF decoding is covered by unit tests.
//
// Output is deterministic for a given --seed. The 50 notes of the standard
// dataset are added once notes exist (M2).

#include <QBuffer>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QRandomGenerator>
#include <QTextStream>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <atomic>

using namespace Qt::StringLiterals;

namespace {

struct Spec
{
    QString path;
    QByteArray format;
    QSize size;
    quint32 seed;
};

// Photo-like grain so JPEG/WebP files have realistic entropy (and decode cost)
// instead of compressing smooth gradients to a few kilobytes.
void addGrain(QImage &image, quint32 seed)
{
    QRandomGenerator rng(seed ^ 0x9e3779b9u);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const quint32 r = rng.generate();
            auto jitter = [r](int channel, int shift) {
                return std::clamp(channel + int((r >> shift) & 0x1f) - 16, 0, 255);
            };
            const QRgb c = line[x];
            line[x] = qRgb(jitter(qRed(c), 0), jitter(qGreen(c), 8), jitter(qBlue(c), 16));
        }
    }
}

QImage paint(QSize size, quint32 seed, int label)
{
    QRandomGenerator rng(seed);
    QImage image(size, QImage::Format_RGB32);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient bg(0, 0, size.width(), size.height());
    bg.setColorAt(0, QColor::fromHsv(int(rng.bounded(360)), 120 + int(rng.bounded(100)), 120 + int(rng.bounded(120))));
    bg.setColorAt(1, QColor::fromHsv(int(rng.bounded(360)), 80 + int(rng.bounded(100)), 60 + int(rng.bounded(120))));
    p.fillRect(image.rect(), bg);
    const int shapes = 12 + int(rng.bounded(24));
    for (int i = 0; i < shapes; ++i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor::fromHsv(int(rng.bounded(360)), int(rng.bounded(256)), int(rng.bounded(256)),
                                   80 + int(rng.bounded(150))));
        const int w = 1 + int(rng.bounded(std::max(2, size.width() / 3)));
        const int h = 1 + int(rng.bounded(std::max(2, size.height() / 3)));
        const QRect r(int(rng.bounded(size.width())) - w / 2, int(rng.bounded(size.height())) - h / 2, w, h);
        if (rng.bounded(2))
            p.drawEllipse(r);
        else
            p.drawRect(r);
    }
    QFont font = p.font();
    font.setPixelSize(std::max(12, std::min(size.width(), size.height()) / 6));
    font.setBold(true);
    p.setFont(font);
    p.setPen(QColor(255, 255, 255, 220));
    p.drawText(image.rect(), Qt::AlignCenter, QString::number(label));
    p.end();
    return image;
}

QByteArray svgDocument(quint32 seed, QSize size, int label)
{
    QRandomGenerator rng(seed);
    QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' width='" + QByteArray::number(size.width())
                   + "' height='" + QByteArray::number(size.height()) + "'>";
    svg += "<defs><linearGradient id='g'><stop offset='0' stop-color='#" + QByteArray::number(rng.bounded(0xffffff), 16)
         + "'/><stop offset='1' stop-color='#204060'/></linearGradient></defs>";
    svg += "<rect width='100%' height='100%' fill='url(#g)'/>";
    for (int i = 0; i < 20; ++i) {
        svg += "<circle cx='" + QByteArray::number(rng.bounded(size.width())) + "' cy='"
             + QByteArray::number(rng.bounded(size.height())) + "' r='" + QByteArray::number(5 + rng.bounded(size.width() / 6))
             + "' fill='hsl(" + QByteArray::number(rng.bounded(360)) + ",70%,60%)' fill-opacity='0.6'/>";
    }
    svg += "<text x='50%' y='55%' font-size='" + QByteArray::number(size.height() / 5)
         + "' text-anchor='middle' fill='white'>" + QByteArray::number(label) + "</text></svg>";
    return svg;
}

bool write(const Spec &spec, int label)
{
    QFile file(spec.path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (spec.format == "svg")
        return file.write(svgDocument(spec.seed, spec.size, label)) > 0;
    QImage image = paint(spec.size, spec.seed, label);
    if (spec.format == "jpeg" || spec.format == "webp")
        addGrain(image, spec.seed);
    QImageWriter writer(&file, spec.format);
    if (spec.format == "jpeg")
        writer.setQuality(85);
    else if (spec.format == "webp")
        writer.setQuality(80);
    else if (spec.format == "tiff")
        writer.setCompression(1); // LZW
    return writer.write(image);
}

QList<Spec> standardSpecs(const QDir &dir, quint32 seed)
{
    QList<Spec> specs;
    for (int i = 1; i <= 300; ++i)
        specs.append({dir.filePath(u"img_%1.jpg"_s.arg(i, 4, 10, QChar(u'0'))), "jpeg", {2048, 2048}, seed + quint32(i)});
    return specs;
}

QList<Spec> stressSpecs(const QDir &dir, quint32 seed)
{
    QRandomGenerator rng(seed ^ 0x5eed5eedu);
    QList<Spec> specs;
    for (int i = 1; i <= 1000; ++i) {
        const int pick = int(rng.bounded(100));
        QByteArray format;
        QString suffix;
        int maxSide;
        if (pick < 42) { format = "jpeg"; suffix = u"jpg"_s; maxSide = 4096; }
        else if (pick < 62) { format = "png"; suffix = u"png"_s; maxSide = 2048; }
        else if (pick < 77) { format = "webp"; suffix = u"webp"_s; maxSide = 3072; }
        else if (pick < 85) { format = "bmp"; suffix = u"bmp"_s; maxSide = 1024; }
        else if (pick < 90) { format = "tiff"; suffix = u"tif"_s; maxSide = 1536; }
        else { format = "svg"; suffix = u"svg"_s; maxSide = 1600; }
        const int w = 256 + int(rng.bounded(maxSide - 255));
        const int h = 256 + int(rng.bounded(maxSide - 255));
        QSize size(w, h);
        if (i % 200 == 0) { // five large photos (48 MP) exercise the large-decode path
            format = "jpeg";
            suffix = u"jpg"_s;
            size = QSize(8000, 6000);
        }
        specs.append({dir.filePath(u"stress_%1.%2"_s.arg(i, 4, 10, QChar(u'0')).arg(suffix)), format, size,
                      seed + 100000u + quint32(i)});
    }
    return specs;
}

int generate(const QString &name, const QList<Spec> &specs, const QDir &dir, quint32 seed)
{
    QTextStream out(stdout);
    out << "Generating " << specs.size() << " images into " << dir.path() << " ..." << Qt::endl;
    QList<int> indexes;
    for (int i = 0; i < specs.size(); ++i)
        indexes.append(i);
    std::atomic<int> failures{0};
    QtConcurrent::blockingMap(indexes, [&](int i) {
        if (!write(specs[i], i + 1))
            ++failures;
    });
    QFile readme(dir.filePath(u"DATASET.txt"_s));
    if (readme.open(QIODevice::WriteOnly)) {
        QTextStream(&readme) << "Glacier Ref " << name << " dataset, generated by glacier-fixturegen\n"
                             << "seed: " << seed << "\nimages: " << specs.size() << "\n";
    }
    out << "  done, " << failures.load() << " failures" << Qt::endl;
    return failures.load();
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Generates Glacier Ref benchmark datasets."_s);
    parser.addHelpOption();
    const QCommandLineOption outputOption(u"output"_s, u"Output directory (default /data/glacier-fixtures)."_s,
                                          u"dir"_s, u"/data/glacier-fixtures"_s);
    const QCommandLineOption seedOption(u"seed"_s, u"Random seed (default 1)."_s, u"n"_s, u"1"_s);
    const QCommandLineOption standardOption(u"standard"_s, u"Generate the standard dataset."_s);
    const QCommandLineOption stressOption(u"stress"_s, u"Generate the stress dataset."_s);
    parser.addOptions({outputOption, seedOption, standardOption, stressOption});
    parser.process(app);

    const quint32 seed = parser.value(seedOption).toUInt();
    const bool all = !parser.isSet(standardOption) && !parser.isSet(stressOption);
    int failures = 0;
    for (const auto &[flag, name] : {std::pair{parser.isSet(standardOption) || all, u"standard"_s},
                                     std::pair{parser.isSet(stressOption) || all, u"stress"_s}}) {
        if (!flag)
            continue;
        QDir dir(QDir(parser.value(outputOption)).filePath(name));
        if (!dir.mkpath(u"."_s)) {
            QTextStream(stderr) << "cannot create " << dir.path() << Qt::endl;
            return 2;
        }
        failures += generate(name, name == u"standard"_s ? standardSpecs(dir, seed) : stressSpecs(dir, seed), dir, seed);
    }
    return failures == 0 ? 0 : 1;
}
