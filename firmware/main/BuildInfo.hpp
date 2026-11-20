#pragma once

/**
 * What build of the firmware this is: the version from firmware/version.txt (ESP-IDF's app version) and the commit.
 */
namespace BuildInfo {
/**
 * Returns the app version, e.g. "0.1.0".
 *
 * @return Version text
 */
const char* getVersion();

/**
 * Returns the short commit hash the build was made from, or "unknown" outside a git checkout.
 *
 * @return Commit text
 */
const char* getCommit();
} // namespace BuildInfo
