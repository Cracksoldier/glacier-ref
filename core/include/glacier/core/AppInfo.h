#pragma once

#include <QString>

// Application identity shared by the app, platform integration, and packaging.
namespace glacier::app_info {

// Reverse-DNS application ID: Wayland app_id, desktop-file name, macOS bundle
// identifier, Flatpak ID.
QString applicationId();

// User-visible product name.
QString displayName();

// QCoreApplication identity, which determines the QSettings location.
QString organizationName();
QString organizationDomain();
QString applicationName();

// Native board file type.
QString boardMimeType();
QString boardFileSuffix();

// Semantic version of this build, e.g. "0.1.0".
QString version();

// Project repository, opened only on explicit user action from Help.
QString homepageUrl();

} // namespace glacier::app_info
