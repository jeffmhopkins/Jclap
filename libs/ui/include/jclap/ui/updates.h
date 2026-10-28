#pragma once

// Update notices: looks up the newest JClap release on GitHub, at most once a day, in the
// background, and says whether it has a newer version of this plugin. Each plugin has its own
// version, and a release lists them all (see .github/release-notes.md), so a release that only
// changed JSynth shows a notice in JSynth and nowhere else. Doesn't include Visage;
// jclap/ui/update_notice.h is the header widget that shows it.
//
// What the check learns is kept in a small text file in the user's settings folder (see
// userFolder()), shared by every JClap plugin, along with whether checking is on and which release
// the user dismissed. Offline, or with checking off, nothing is shown and nothing fails.
//
// Window starts the check when it opens on screen, so a plugin with a panel needs nothing more
// than an UpdateNotice in its header.

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace jclap::ui::updates {

// Which plugin this is: its CLAP id and its version (jclap_plugin_version in its CMakeLists.txt).
// The end of the id, after the last '.', is its name in a release's version list. ClapGui::init()
// calls this from the plugin's descriptor.
void setPlugin(const std::string &id, const std::string &version);

// This plugin's version, e.g. "0.10.2". Before setPlugin(), the release's (the project version in
// the top-level CMakeLists.txt).
const char *currentVersion();

struct Release {
   std::string version; // this plugin's version in the release, e.g. "0.10.3"
   std::string url;     // the release's page on GitHub
};

// This plugin's version in the newest release, if it is newer than this build, checking is on,
// the user hasn't dismissed it, and a window is open (startChecking() is held). Cheap enough to
// call from a UI timer.
std::optional<Release> available();

// Hides this plugin's notice for `version` for good. A later version shows again.
void dismiss(const std::string &version);

// Whether to check at all (on unless the user turned it off). Turning it on checks straight away
// if a window is open.
bool enabled();
void setEnabled(bool on);

// Opens a web page in the user's browser.
void openInBrowser(const std::string &url);

// Keeps the checker alive while held: the first call starts a check if one is due, and dropping
// the last handle stops one still running and waits for it. Window holds one while it is shown.
std::shared_ptr<void> startChecking();

// ---- The parts below are internal, exposed for tests. ----

// Whether version `a` is newer than `b` ("0.10.3" > "0.10.2", "1.0.0" > "0.99.9"). A leading "v" is
// ignored; a version that isn't three numbers is never newer.
bool isNewer(const std::string &a, const std::string &b);

// The version in a GitHub "latest release" reply ("v0.10.3" gives "0.10.3"), or empty if the reply
// has none, or it is a draft or prerelease.
std::string parseLatestRelease(const std::string &json);

// Each plugin's version in a release, from the "jclap-versions: jsynth=0.10.8 jverb=0.10.7" line its
// notes carry, keyed by the end of the plugin id. Empty for a release from before plugins had their
// own versions.
std::map<std::string, std::string> parsePluginVersions(const std::string &json);

struct State {
   bool enabled = true;
   long long checkedAt = 0; // Unix time of the last successful check
   std::string latest;      // the newest release then, e.g. "0.10.3"
   std::string dismissed;   // the release dismissed by a build from before plugins had versions
   // What a build with plugin versions learned. `release` is the newest release, empty if an older
   // build made the last check (which drops these lines), and `versions` holds each plugin's version
   // in it, empty if that release lists none (every plugin then has the release's version).
   std::string release;
   std::map<std::string, std::string> versions;
   std::map<std::string, std::string> dismissedVersions; // per plugin
};

// The settings file, and reading and writing it. A missing or unreadable file reads as defaults.
std::filesystem::path stateFile();
State readState(const std::filesystem::path &file);
bool writeState(const std::filesystem::path &file, const State &state);

// Whether a check is due `now` (Unix time): checking is on, and the last one was a day ago or more.
bool checkDue(const State &state, long long now);

// Points the checker at another settings file, for tests. Empty restores the default.
void setStateFileForTesting(const std::filesystem::path &file);

} // namespace jclap::ui::updates
