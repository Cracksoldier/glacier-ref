// SVG acceptance policy (spec §4). Strings use single-quoted XML attributes so
// no raw string literals are needed (moc mis-tokenizes some raw strings).

#include "GlacierTestMain.h"
#include "TestImages.h"

#include "glacier/io/ImageDecoder.h"
#include "glacier/io/SvgValidator.h"

#include <QGuiApplication>
#include <QObject>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::io;

namespace {

QByteArray svg(const QByteArray &body, const QByteArray &extraNs = {})
{
    return "<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' "
        + extraNs + " width='20' height='10'>" + body + "</svg>";
}

QByteArray pngDataUri(const QByteArray &png)
{
    return "data:image/png;base64," + png.toBase64();
}

} // namespace

class TestSvgValidator : public QObject
{
    Q_OBJECT

private slots:
    void accepts_data()
    {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<bool>("animated");
        const QByteArray png = test::encode(QImage(4, 4, QImage::Format_RGB32), "png");

        QTest::newRow("shapes") << svg("<rect width='10' height='10' fill='red'/><circle cx='5' cy='5' r='2'/>") << false;
        QTest::newRow("gradient by fragment")
            << svg("<defs><linearGradient id='g'><stop offset='0' stop-color='red'/></linearGradient></defs>"
                   "<rect width='5' height='5' fill='url(#g)' style='stroke:url( \"#g\" )'/>") << false;
        QTest::newRow("use with xlink:href") << svg("<rect id='r' width='2' height='2'/><use xlink:href='#r' x='4'/>") << false;
        QTest::newRow("use with href") << svg("<rect id='r' width='2' height='2'/><use href='#r' x='4'/>") << false;
        QTest::newRow("style element") << svg("<style>.a { fill: url(#g); }</style><rect class='a' width='3' height='3'/>") << false;
        QTest::newRow("png data uri") << svg("<image width='4' height='4' xlink:href='" + pngDataUri(png) + "'/>") << false;
        QTest::newRow("prologue and comments")
            << "<?xml version='1.0' encoding='UTF-8'?>\n<!-- made by hand -->\n" + svg("<g><path d='M0 0L5 5'/></g>") << false;
        QTest::newRow("inkscape metadata")
            << svg("<sodipodi:namedview id='nv' inkscape:zoom='1'/>"
                   "<metadata><rdf:RDF><cc:Work rdf:about=''/></rdf:RDF></metadata>"
                   "<g inkscape:label='Layer 1' inkscape:groupmode='layer'><rect width='1' height='1'/></g>",
                   "xmlns:sodipodi='http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd' "
                   "xmlns:inkscape='http://www.inkscape.org/namespaces/inkscape' "
                   "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
                   "xmlns:cc='http://creativecommons.org/ns#'") << false;
        QTest::newRow("data and aria attributes")
            << svg("<g data-name='Layer' aria-hidden='true' role='img'><rect width='1' height='1'/></g>") << false;
        QTest::newRow("animation") << svg("<rect width='1' height='1'><animate attributeName='x' from='0' to='5' dur='1s'/></rect>") << true;
    }

    void accepts()
    {
        QFETCH(QByteArray, document);
        QFETCH(bool, animated);
        const SvgValidation result = validateSvg(document);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.animated, animated);

        const ProbeOutcome probe = probeImage(document);
        QVERIFY2(probe.info, qPrintable(probe.error));
        QCOMPARE(probe.info->format, ImageFormat::Svg);
        QCOMPARE(probe.notices.isEmpty(), !animated);
        QVERIFY(!decodeImage(document, *probe.info, 64).isNull());
    }

    void rejects_data()
    {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<QString>("expected"); // fragment of the error message
        const QByteArray hugePng = test::pngHeaderOnly(20000, 20000);

        QTest::newRow("script") << svg("<script>alert(1)</script>") << u"<script>"_s;
        QTest::newRow("foreignObject") << svg("<foreignObject><div/></foreignObject>") << u"<foreignObject>"_s;
        QTest::newRow("onload") << svg("<rect width='1' height='1' onload='x()'/>") << u"onload"_s;
        QTest::newRow("http image") << svg("<image xlink:href='http://example.com/a.png' width='1' height='1'/>") << u"external reference"_s;
        QTest::newRow("https href") << svg("<use href='https://example.com/s.svg#a'/>") << u"external reference"_s;
        QTest::newRow("relative href") << svg("<use href='other.svg#a'/>") << u"external reference"_s;
        QTest::newRow("css url http") << svg("<rect width='1' height='1' style='fill:url(http://e.com/p.svg#x)'/>") << u"url("_s;
        QTest::newRow("presentation url file") << svg("<rect width='1' height='1' fill='url(file:///etc/x.svg#p)'/>") << u"url("_s;
        QTest::newRow("style @import") << svg("<style>@import url(#x);</style>") << u"@import"_s;
        QTest::newRow("css escape") << svg("<style>.a { fill: u\\72l(http://e.com) }</style>") << u"escapes"_s;
        QTest::newRow("doctype entity")
            << "<?xml version='1.0'?><!DOCTYPE svg [<!ENTITY e 'x'>]>" + svg("<text>&e;</text>") << u"DOCTYPE"_s;
        QTest::newRow("xml-stylesheet")
            << "<?xml version='1.0'?><?xml-stylesheet href='http://e.com/s.css'?>" + svg("") << u"processing instructions"_s;
        QTest::newRow("nested svg data uri")
            << svg("<image width='1' height='1' href='data:image/svg+xml;base64,PHN2Zy8+'/>") << u"PNG and JPEG"_s;
        QTest::newRow("gif data uri")
            << svg("<image width='1' height='1' href='data:image/gif;base64," + test::gifFrames(1).toBase64() + "'/>") << u"PNG and JPEG"_s;
        QTest::newRow("mislabelled data uri")
            << svg("<image width='1' height='1' href='data:image/png;base64," + test::gifFrames(1).toBase64() + "'/>") << u"not a PNG or JPEG"_s;
        QTest::newRow("bad base64") << svg("<image width='1' height='1' href='data:image/png;base64,***'/>") << u"base64"_s;
        QTest::newRow("huge embedded image") << svg("<image width='1' height='1' href='" + pngDataUri(hugePng) + "'/>") << u"dimensions"_s;
        QTest::newRow("unknown element") << svg("<blink/>") << u"<blink>"_s;
        QTest::newRow("unknown attribute") << svg("<rect bogus='1'/>") << u"bogus"_s;
        QTest::newRow("foreign namespace element") << svg("<x:thing/>", "xmlns:x='urn:example'") << u"x:thing"_s;
        QTest::newRow("no svg namespace") << QByteArray("<svg width='1' height='1'/>") << u"root element"_s;
        QTest::newRow("wrong root") << QByteArray("<html xmlns='http://www.w3.org/1999/xhtml'/>") << u"root element"_s;
        QTest::newRow("malformed") << QByteArray("<svg xmlns='http://www.w3.org/2000/svg'><rect></svg>") << u"malformed"_s;
        QTest::newRow("nesting bomb") << svg(QByteArray("<g>").repeated(300) + QByteArray("</g>").repeated(300)) << u"too deep"_s;
    }

    void rejects()
    {
        QFETCH(QByteArray, document);
        QFETCH(QString, expected);
        const SvgValidation result = validateSvg(document);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(expected), qPrintable(result.error));
        QVERIFY(!probeImage(document).info);
    }

    void localFileCanaryIsRejectedBeforeRendering()
    {
        // A real local image next to the document must not be pulled in.
        QTemporaryDir dir;
        const QString canary = dir.filePath(u"canary.png"_s);
        QVERIFY(test::encode(QImage(4, 4, QImage::Format_RGB32), "png").size() > 0);
        QFile file(canary);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(test::encode(QImage(4, 4, QImage::Format_RGB32), "png"));
        file.close();

        for (const QString &href : {QUrl::fromLocalFile(canary).toString(), canary, u"canary.png"_s}) {
            const QByteArray document = svg("<image width='4' height='4' xlink:href='" + href.toUtf8() + "'/>");
            const SvgValidation result = validateSvg(document);
            QVERIFY2(!result.ok, qPrintable(href));
            QVERIFY(!probeImage(document).info);
        }
    }

    void enforcesSizeLimit()
    {
        Limits limits;
        limits.maxAssetBytes = 100;
        QVERIFY(!validateSvg(svg(QByteArray("<rect width='1' height='1'/>").repeated(10)), limits).ok);
    }
};

GLACIER_TEST_GUI_MAIN(TestSvgValidator)
#include "tst_svgvalidator.moc"
