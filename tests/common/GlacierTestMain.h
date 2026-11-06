#pragma once

// Test entry points that honor GLACIER_TEST_LOCALE.
//
// When GLACIER_TEST_LOCALE is set (e.g. "de_AT"), the test process switches
// both the C runtime locale and Qt's default QLocale to it after the
// application object exists (QCoreApplication resets the C locale from the
// environment on Unix). If the locale cannot be activated, the test fails
// instead of silently running in the default locale.

#include <QByteArray>
#include <QLocale>
#include <QString>
#include <QTest>

#include <clocale>
#include <cstdio>

namespace glacier::test {

inline bool applyRequestedLocale()
{
    const QByteArray requested = qgetenv("GLACIER_TEST_LOCALE");
    if (requested.isEmpty())
        return true;

    QByteArray dashed = requested;
    dashed.replace('_', '-');
    const QByteArray candidates[] = {
        requested + ".UTF-8",
        requested + ".utf8",
        dashed,       // Windows UCRT form, e.g. "de-AT"
        requested,
    };

    bool cLocaleSet = false;
    for (const QByteArray &candidate : candidates) {
        if (std::setlocale(LC_ALL, candidate.constData()) != nullptr) {
            cLocaleSet = true;
            break;
        }
    }
    if (!cLocaleSet) {
        std::fprintf(stderr,
                     "GLACIER_TEST_LOCALE=%s requested, but no matching C locale is available "
                     "(on Linux, generate it, e.g. locale-gen %s.UTF-8)\n",
                     requested.constData(), requested.constData());
        return false;
    }

    QLocale::setDefault(QLocale(QString::fromLatin1(requested)));
    return true;
}

} // namespace glacier::test

#define GLACIER_TEST_MAIN_WITH_APP(AppType, TestObject)                                            \
    int main(int argc, char *argv[])                                                               \
    {                                                                                              \
        AppType app(argc, argv);                                                                   \
        if (!glacier::test::applyRequestedLocale())                                                \
            return 2;                                                                              \
        TestObject tc;                                                                             \
        QTEST_SET_MAIN_SOURCE_PATH                                                                 \
        return QTest::qExec(&tc, argc, argv);                                                      \
    }

// Qt Core only.
#define GLACIER_TEST_CORE_MAIN(TestObject) GLACIER_TEST_MAIN_WITH_APP(QCoreApplication, TestObject)
// Qt Gui (images, fonts, screens); runs on the offscreen platform in CTest.
#define GLACIER_TEST_GUI_MAIN(TestObject) GLACIER_TEST_MAIN_WITH_APP(QGuiApplication, TestObject)
// Qt Widgets.
#define GLACIER_TEST_WIDGETS_MAIN(TestObject) GLACIER_TEST_MAIN_WITH_APP(QApplication, TestObject)
