#pragma once

// Presets for any plugin: a factory bank the plugin compiles in, and the user's own saved as small
// text files in a per-plugin folder. Doesn't include Visage; jclap/ui/preset_bar.h is the browser.
//
// A user preset file is plain text: a "<Name> preset <version>" line, then one "<param id> <value>"
// per line. Saving writes every param, so a preset sounds the same even if a later version changes
// a default; loading leaves any param the file doesn't list at its default.
//
//    const jclap::ui::PresetFormat format{"MyPlugin", 1, ".mppreset", kParamCount};
//    writePresetFile(userPresetFolder(format) / "Warm.mppreset", format, params);
//    for (const Preset &p : userPresets(format)) ...

#include <clap/clap.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "jclap/ui/params.h"

namespace jclap::ui {

struct Preset {
   std::string name;
   std::string category;
   // Param id and value. A factory preset may list only what it changes; see completePresets().
   std::vector<std::pair<clap_id, double>> values;
};

struct PresetFormat {
   // The plugin's name: the preset folder's name and the first word of each file.
   std::string name;
   // The file version written. Files from version 1 up to this one are read.
   int version = 1;
   // With its dot, e.g. ".jvpreset".
   std::string extension;
   // Ids at or above this are skipped when reading.
   uint32_t paramCount = 0;

   std::string header() const { return name + " preset " + std::to_string(version); }
};

// Fills every preset out with the params it doesn't list, at `defaults` (one per param id), so
// loading one sets the whole sound and nothing from the previous preset carries over.
void completePresets(std::vector<Preset> &presets, const std::vector<double> &defaults);

// A folder of the user's own settings for `name`: %APPDATA%\<name> on Windows,
// ~/Library/Application Support/<name> on macOS, and ~/.config/<name> (or under $XDG_CONFIG_HOME)
// on Linux. It may not exist yet.
std::filesystem::path userFolder(const std::string &name);

// Where user presets live: userFolder(format.name) / "Presets".
std::filesystem::path userPresetFolder(const PresetFormat &format);

// Reads a user preset; false if the file isn't one of this format's. `version`, if given, is set to
// the file's version, for a plugin that converts older files.
bool readPresetFile(const std::filesystem::path &file, const PresetFormat &format, Preset &out,
                    int *version = nullptr);

// Saves every param through `params`. The file is written beside its final name and then renamed
// over it, so a failed save never leaves a half-written preset behind. False if it can't be written.
bool writePresetFile(const std::filesystem::path &file, const PresetFormat &format, const ParamAccess &params);

// The user's presets in the format's folder, sorted by name, with category "User".
std::vector<Preset> userPresets(const PresetFormat &format);

// A name made safe for a file name: letters, digits, spaces, dashes and underscores only, never empty.
std::string presetFileName(const std::string &name);

} // namespace jclap::ui
