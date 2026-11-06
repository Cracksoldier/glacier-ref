#pragma once

#include <QHashFunctions>
#include <QString>
#include <QStringView>
#include <QUuid>

#include <optional>

namespace glacier {

// Stable identifier for documents, items, groups, and assets.
//
// Serialized as a canonical UUID string: 36 characters, lowercase hex, no
// braces (e.g. "3f2b8c1e-7a4d-4e0f-9b6a-2c5d8e1f0a7b"). Parsing accepts only
// that canonical form so one UUID never has two distinct string spellings in a
// manifest. The null UUID is never a valid Id; a default-constructed Id is
// null (needed for Qt containers/metatypes) and is rejected by validation.
class Id
{
public:
    Id() = default;

    // Creates a new random (version 4) Id.
    static Id generate();

    // Parses the canonical string form; returns std::nullopt for anything else.
    static std::optional<Id> fromString(QStringView text);

    QString toString() const;
    QUuid toUuid() const { return m_uuid; }
    bool isNull() const { return m_uuid.isNull(); }

    friend bool operator==(const Id &, const Id &) = default;
    friend size_t qHash(const Id &id, size_t seed = 0) noexcept { return qHash(id.m_uuid, seed); }

private:
    explicit Id(const QUuid &uuid) : m_uuid(uuid) {}

    QUuid m_uuid;
};

} // namespace glacier
