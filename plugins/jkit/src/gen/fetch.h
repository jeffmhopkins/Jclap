#pragma once

// Downloads one file over HTTPS into place, checking its SHA-256 as it comes in: WinHTTP on Windows,
// the system's libcurl (loaded at run time, as the update check does) elsewhere. It writes beside the
// file and renames it in only once the hash matches, so a cancelled or broken download never leaves
// a file that looks finished.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace jkit::gen {

struct Download {
   std::string url;
   std::filesystem::path file;
   uint64_t size = 0;  // expected, for progress before the server says
   std::string sha256; // lower-case hex
};

// `progress` is called with the bytes received so far. False with `error` set on failure (including
// "cancelled" when `cancel` turned true).
bool fetch(const Download &d, const std::function<void(uint64_t)> &progress, const std::atomic<bool> &cancel,
           std::string &error);

// The SHA-256 of a file on disk, or "" if it can't be read.
std::string fileSha256(const std::filesystem::path &file);

} // namespace jkit::gen
