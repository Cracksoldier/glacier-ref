#include "glacier/core/AppInfo.h"

#include "glacier/core/Version.h"

using namespace Qt::StringLiterals;

namespace glacier::app_info {

QString applicationId()
{
    return u"io.github.cracksoldier.GlacierRef"_s;
}

QString displayName()
{
    return u"Glacier Ref"_s;
}

QString organizationName()
{
    return u"GlacierRef"_s;
}

QString organizationDomain()
{
    return u"cracksoldier.github.io"_s;
}

QString applicationName()
{
    return u"GlacierRef"_s;
}

QString boardMimeType()
{
    return u"application/x-glacierref"_s;
}

QString boardFileSuffix()
{
    return u"glacierref"_s;
}

QString version()
{
    return QString::fromLatin1(GLACIER_VERSION_STRING);
}

QString homepageUrl()
{
    return QString::fromLatin1(GLACIER_HOMEPAGE_URL);
}

} // namespace glacier::app_info
