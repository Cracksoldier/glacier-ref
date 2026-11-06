#include "glacier/core/Id.h"

namespace glacier {

namespace {
constexpr qsizetype CanonicalLength = 36;
}

Id Id::generate()
{
    return Id(QUuid::createUuid());
}

std::optional<Id> Id::fromString(QStringView text)
{
    if (text.size() != CanonicalLength)
        return std::nullopt;

    const QUuid uuid = QUuid::fromString(text);
    if (uuid.isNull())
        return std::nullopt;

    // Reject non-canonical spellings such as uppercase hex.
    if (uuid.toString(QUuid::WithoutBraces) != text)
        return std::nullopt;

    return Id(uuid);
}

QString Id::toString() const
{
    return m_uuid.toString(QUuid::WithoutBraces);
}

} // namespace glacier
