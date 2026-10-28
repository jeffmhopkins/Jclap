#include "jclap/ui/updates.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>

#include "jclap/ui/presets.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

#ifndef JCLAP_VERSION
#define JCLAP_VERSION "0.0.0"
#endif

namespace jclap::ui::updates {
namespace {

constexpr const char *kApiHost = "api.github.com";
constexpr const char *kApiPath = "/repos/jeffmhopkins/Jclap/releases/latest";
constexpr const char *kReleasePage = "https://github.com/jeffmhopkins/Jclap/releases/tag/v";
constexpr const char *kFileHeader = "JClap updates 1";
constexpr long long kCheckEverySeconds = 24 * 60 * 60;
constexpr int kTimeoutMs = 8000;
// A release reply is a few kilobytes; anything far bigger isn't one.
constexpr size_t kMaxReply = 1 << 20;

long long unixNow() {
   return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool parseVersion(std::string s, int out[3]) {
   if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
      s.erase(0, 1);
   size_t pos = 0;
   for (int i = 0; i < 3; ++i) {
      if (pos >= s.size() || s[pos] < '0' || s[pos] > '9')
         return false;
      long v = 0;
      while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9' && v < 100000)
         v = v * 10 + (s[pos++] - '0');
      out[i] = static_cast<int>(v);
      if (i < 2 && (pos >= s.size() || s[pos++] != '.'))
         return false;
   }
   return pos == s.size();
}

// The value after `"key":` at the top of a JSON reply, skipping whitespace: where it starts, or npos.
size_t valueOf(const std::string &json, const char *key) {
   const std::string quoted = std::string("\"") + key + "\"";
   for (size_t at = json.find(quoted); at != std::string::npos; at = json.find(quoted, at + 1)) {
      size_t p = at + quoted.size();
      while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
         ++p;
      if (p >= json.size() || json[p] != ':')
         continue;
      ++p;
      while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
         ++p;
      return p;
   }
   return std::string::npos;
}

// A plugin's name in a version list: the end of its id, like "polar-xy".
bool validName(const std::string &s) {
   if (s.empty() || s.size() > 64)
      return false;
   for (char ch : s) {
      if (!(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9') && ch != '-' && ch != '_')
         return false;
   }
   return true;
}

bool isTrue(const std::string &json, const char *key) {
   const size_t p = valueOf(json, key);
   return p != std::string::npos && json.compare(p, 4, "true") == 0;
}

// ---- Fetching the reply ----

#if defined(_WIN32)

std::wstring widen(const std::string &s) {
   if (s.empty())
      return {};
   const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
   std::wstring out(static_cast<size_t>(n), L'\0');
   MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
   return out;
}

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

// The request in flight, so cancelFetch() can close it from another thread, which makes WinHTTP's
// blocking calls return at once. Whoever takes it out closes it.
std::atomic<HINTERNET> inFlight{nullptr};

void cancelFetch() {
   if (HINTERNET h = inFlight.exchange(nullptr))
      WinHttpCloseHandle(h);
}

bool fetch(const std::atomic<bool> &cancel, std::string &body) {
   const std::wstring agent = widen(std::string("JClap/") + JCLAP_VERSION);
   HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
   // Before Windows 8.1 there is no automatic proxy.
   if (!session)
      session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
   if (!session)
      return false;
   WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
   bool ok = false;
   HINTERNET connection = WinHttpConnect(session, widen(kApiHost).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
   HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", widen(kApiPath).c_str(), nullptr,
                                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                       WINHTTP_FLAG_SECURE)
                                  : nullptr;
   if (request) {
      inFlight.store(request);
      // Cancelled before the request was stored: close it here.
      if (cancel.load())
         cancelFetch();
      DWORD status = 0, size = sizeof(status);
      if (WinHttpSendRequest(request, L"Accept: application/vnd.github+json", static_cast<DWORD>(-1),
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
          WinHttpReceiveResponse(request, nullptr) &&
          WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
          status == 200) {
         ok = true;
         for (;;) {
            DWORD available = 0, read = 0;
            if (cancel.load() || !WinHttpQueryDataAvailable(request, &available)) {
               ok = false;
               break;
            }
            if (available == 0)
               break;
            const size_t at = body.size();
            if (at + available > kMaxReply) {
               ok = false;
               break;
            }
            body.resize(at + available);
            if (!WinHttpReadData(request, &body[at], available, &read)) {
               ok = false;
               break;
            }
            body.resize(at + read);
         }
      }
      cancelFetch();
   }
   if (connection)
      WinHttpCloseHandle(connection);
   WinHttpCloseHandle(session);
   return ok && !cancel.load();
}

#else

// libcurl, loaded when first needed rather than linked, so the plugins don't depend on it: every Mac
// has it, and nearly every Linux desktop. Without it there is simply no check. The few calls used
// have had the same signatures and option numbers since libcurl 7.32.
struct Curl {
   void *(*init)();
   int (*setopt)(void *, int, ...);
   int (*perform)(void *);
   int (*getinfo)(void *, int, ...);
   void (*cleanup)(void *);
};

enum : int {
   kOptWriteData = 10001,
   kOptUrl = 10002,
   kOptUserAgent = 10018,
   kOptHttpHeader = 10023,
   kOptWriteFunction = 20011,
   kOptNoProgress = 43,
   kOptFollowLocation = 52,
   kOptMaxRedirs = 68,
   kOptNoSignal = 99,
   kOptTimeoutMs = 155,
   kOptConnectTimeoutMs = 156,
   kOptXferInfoData = 10057,
   kOptXferInfoFunction = 20219,
   kInfoResponseCode = 0x200000 + 2,
};

const Curl *loadCurl() {
   // Never unloaded: libcurl and the TLS library under it can leave exit handlers behind.
   static const Curl *curl = []() -> const Curl * {
#if defined(__APPLE__)
      const char *names[] = {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib"};
#else
      const char *names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl-nss.so.4"};
#endif
      for (const char *name : names) {
         void *lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
         if (!lib)
            continue;
         static Curl c;
         c.init = reinterpret_cast<void *(*)()>(dlsym(lib, "curl_easy_init"));
         c.setopt = reinterpret_cast<int (*)(void *, int, ...)>(dlsym(lib, "curl_easy_setopt"));
         c.perform = reinterpret_cast<int (*)(void *)>(dlsym(lib, "curl_easy_perform"));
         c.getinfo = reinterpret_cast<int (*)(void *, int, ...)>(dlsym(lib, "curl_easy_getinfo"));
         c.cleanup = reinterpret_cast<void (*)(void *)>(dlsym(lib, "curl_easy_cleanup"));
         if (c.init && c.setopt && c.perform && c.getinfo && c.cleanup)
            return &c;
      }
      return nullptr;
   }();
   return curl;
}

struct Transfer {
   const std::atomic<bool> *cancel;
   std::string *body;
};

size_t onData(char *data, size_t size, size_t count, void *user) {
   auto *t = static_cast<Transfer *>(user);
   const size_t n = size * count;
   if (t->body->size() + n > kMaxReply)
      return 0;
   t->body->append(data, n);
   return n;
}

int onProgress(void *user, int64_t, int64_t, int64_t, int64_t) {
   return static_cast<Transfer *>(user)->cancel->load() ? 1 : 0;
}

void cancelFetch() {}

bool fetch(const std::atomic<bool> &cancel, std::string &body) {
   const Curl *curl = loadCurl();
   void *h = curl ? curl->init() : nullptr;
   if (!h)
      return false;
   Transfer transfer{&cancel, &body};
   const std::string url = std::string("https://") + kApiHost + kApiPath;
   const std::string agent = std::string("JClap/") + JCLAP_VERSION;
   curl->setopt(h, kOptUrl, url.c_str());
   curl->setopt(h, kOptUserAgent, agent.c_str());
   curl->setopt(h, kOptWriteFunction, &onData);
   curl->setopt(h, kOptWriteData, static_cast<void *>(&transfer));
   curl->setopt(h, kOptNoProgress, 0L);
   curl->setopt(h, kOptXferInfoFunction, &onProgress);
   curl->setopt(h, kOptXferInfoData, static_cast<void *>(&transfer));
   curl->setopt(h, kOptFollowLocation, 1L);
   curl->setopt(h, kOptMaxRedirs, 3L);
   // No signals: this isn't the host's main thread.
   curl->setopt(h, kOptNoSignal, 1L);
   curl->setopt(h, kOptTimeoutMs, static_cast<long>(kTimeoutMs));
   curl->setopt(h, kOptConnectTimeoutMs, static_cast<long>(kTimeoutMs));
   long status = 0;
   const bool ok = curl->perform(h) == 0 && curl->getinfo(h, kInfoResponseCode, &status) == 0 && status == 200;
   curl->cleanup(h);
   return ok && !cancel.load();
}

#endif

// ---- The checker ----

struct Checker {
   std::mutex mutex;
   std::filesystem::path testFile;
   bool loaded = false;
   State state;
   int users = 0;
   // One try per session, so being offline doesn't mean a request every time a panel opens.
   bool tried = false;
   bool running = false;
   std::thread worker;
   std::atomic<bool> cancel{false};
   // Which plugin this binary is (setPlugin()).
   std::string name;
   std::string version = JCLAP_VERSION;
};

// Never destroyed: a thread is joined when the last window closes, not while the plugin unloads
// (which on Windows holds the loader lock, so joining there would hang).
Checker &checker() {
   static Checker *c = new Checker;
   return *c;
}

std::filesystem::path fileLocked(const Checker &c) { return c.testFile.empty() ? stateFile() : c.testFile; }

void loadLocked(Checker &c) {
   c.state = readState(fileLocked(c));
   c.loaded = true;
}

void saveLocked(Checker &c) { writeState(fileLocked(c), c.state); }

void check() {
   Checker &c = checker();
   std::string body;
   const bool ok = fetch(c.cancel, body);
   const std::string latest = ok ? parseLatestRelease(body) : std::string();
   std::lock_guard<std::mutex> lock(c.mutex);
   if (!latest.empty()) {
      // Another plugin may have changed the file meanwhile; keep what it wrote.
      loadLocked(c);
      c.state.checkedAt = unixNow();
      c.state.latest = latest;
      c.state.release = latest;
      c.state.versions = parsePluginVersions(body);
      saveLocked(c);
   }
   c.running = false;
}

void startIfDueLocked(Checker &c) {
   if (c.users <= 0 || c.running || c.tried || !checkDue(c.state, unixNow()))
      return;
   if (c.worker.joinable())
      c.worker.join(); // the last one has finished: it cleared `running`
   c.tried = true;
   c.running = true;
   c.cancel = false;
   try {
      c.worker = std::thread(check);
   }
   catch (const std::system_error &) {
      c.running = false;
   }
}

void release() {
   Checker &c = checker();
   std::thread worker;
   {
      std::lock_guard<std::mutex> lock(c.mutex);
      if (--c.users > 0)
         return;
      c.cancel = true;
      // A check cut short is tried again the next time a window opens.
      if (c.running)
         c.tried = false;
      worker = std::move(c.worker);
   }
   cancelFetch();
   if (worker.joinable())
      worker.join();
}

} // namespace

void setPlugin(const std::string &id, const std::string &version) {
   Checker &c = checker();
   std::lock_guard<std::mutex> lock(c.mutex);
   const size_t dot = id.rfind('.');
   const std::string name = dot == std::string::npos ? id : id.substr(dot + 1);
   int v[3];
   // Only on a change, so a pointer from currentVersion() stays good across instances.
   if (validName(name) && parseVersion(version, v) && (name != c.name || version != c.version)) {
      c.name = name;
      c.version = version;
   }
}

const char *currentVersion() {
   Checker &c = checker();
   std::lock_guard<std::mutex> lock(c.mutex);
   return c.version.c_str();
}

bool isNewer(const std::string &a, const std::string &b) {
   int va[3], vb[3];
   if (!parseVersion(a, va) || !parseVersion(b, vb))
      return false;
   for (int i = 0; i < 3; ++i) {
      if (va[i] != vb[i])
         return va[i] > vb[i];
   }
   return false;
}

std::string parseLatestRelease(const std::string &json) {
   if (isTrue(json, "draft") || isTrue(json, "prerelease"))
      return {};
   const size_t p = valueOf(json, "tag_name");
   if (p == std::string::npos || json[p] != '"')
      return {};
   const size_t end = json.find('"', p + 1);
   if (end == std::string::npos)
      return {};
   std::string tag = json.substr(p + 1, end - p - 1);
   int v[3];
   if (!parseVersion(tag, v))
      return {};
   return std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]);
}

std::map<std::string, std::string> parsePluginVersions(const std::string &json) {
   std::map<std::string, std::string> versions;
   const std::string marker = "jclap-versions:";
   const size_t at = json.find(marker);
   if (at == std::string::npos)
      return versions;
   // name=x.y.z pairs split by spaces, up to the first thing that isn't one (the comment's end, or
   // the \n escape ending the line in the JSON string).
   size_t p = at + marker.size();
   while (true) {
      while (p < json.size() && json[p] == ' ')
         ++p;
      const size_t eq = json.find('=', p);
      if (eq == std::string::npos)
         break;
      const std::string name = json.substr(p, eq - p);
      size_t end = eq + 1;
      while (end < json.size() && ((json[end] >= '0' && json[end] <= '9') || json[end] == '.'))
         ++end;
      const std::string version = json.substr(eq + 1, end - eq - 1);
      int v[3];
      if (!validName(name) || !parseVersion(version, v))
         break;
      versions[name] = version;
      p = end;
   }
   return versions;
}

std::filesystem::path stateFile() { return userFolder("JClap") / "updates.txt"; }

State readState(const std::filesystem::path &file) {
   State s;
   std::ifstream in(file);
   std::string line;
   if (!in || !std::getline(in, line) || line != kFileHeader)
      return s;
   while (std::getline(in, line)) {
      std::istringstream fields(line);
      std::string key, value, version;
      if (!(fields >> key >> value))
         continue;
      int v[3];
      // Per-plugin lines, "version <plugin> <x.y.z>": builds from before plugins had versions read
      // them as a key with a bad value, and skip them.
      if (fields >> version) {
         if (!validName(value) || !parseVersion(version, v))
            continue;
         if (key == "version")
            s.versions[value] = version;
         else if (key == "dismissed")
            s.dismissedVersions[value] = version;
         continue;
      }
      if (key == "enabled")
         s.enabled = value != "0";
      else if (key == "checked")
         s.checkedAt = std::atoll(value.c_str());
      else if (key == "latest" && parseVersion(value, v))
         s.latest = value;
      else if (key == "dismissed" && parseVersion(value, v))
         s.dismissed = value;
      else if (key == "release" && parseVersion(value, v))
         s.release = value;
   }
   return s;
}

bool writeState(const std::filesystem::path &file, const State &state) {
   std::error_code ec;
   std::filesystem::create_directories(file.parent_path(), ec);
   // Written beside it and renamed over it, so a plugin reading at the same time sees all or nothing.
   std::filesystem::path temp = file;
   temp += ".tmp";
   {
      std::ofstream out(temp, std::ios::trunc);
      if (!out)
         return false;
      out << kFileHeader << "\n";
      out << "enabled " << (state.enabled ? 1 : 0) << "\n";
      out << "checked " << state.checkedAt << "\n";
      if (!state.latest.empty())
         out << "latest " << state.latest << "\n";
      if (!state.dismissed.empty())
         out << "dismissed " << state.dismissed << "\n";
      if (!state.release.empty())
         out << "release " << state.release << "\n";
      for (const auto &[name, version] : state.versions) {
         if (validName(name))
            out << "version " << name << " " << version << "\n";
      }
      for (const auto &[name, version] : state.dismissedVersions) {
         if (validName(name))
            out << "dismissed " << name << " " << version << "\n";
      }
      if (!out)
         return false;
   }
   std::filesystem::rename(temp, file, ec);
   if (ec) {
      std::filesystem::remove(temp, ec);
      return false;
   }
   return true;
}

bool checkDue(const State &state, long long now) {
   // A check time in the future means the clock moved back: check again.
   return state.enabled && (now - state.checkedAt >= kCheckEverySeconds || state.checkedAt > now);
}

std::optional<Release> available() {
   Checker &c = checker();
   std::lock_guard<std::mutex> lock(c.mutex);
   if (!c.loaded)
      loadLocked(c);
   const State &s = c.state;
   // Only while a window is open on screen, so screenshots and tests never show a notice.
   if (c.users <= 0 || !s.enabled || s.release.empty())
      return std::nullopt;
   // A release that lists versions but not this plugin no longer ships it.
   std::string version = s.release;
   if (!s.versions.empty()) {
      const auto it = s.versions.find(c.name);
      if (it == s.versions.end())
         return std::nullopt;
      version = it->second;
   }
   const auto dismissed = s.dismissedVersions.find(c.name);
   if (!isNewer(version, c.version) || (dismissed != s.dismissedVersions.end() && dismissed->second == version))
      return std::nullopt;
   return Release{version, kReleasePage + s.release};
}

void dismiss(const std::string &version) {
   Checker &c = checker();
   std::lock_guard<std::mutex> lock(c.mutex);
   if (!c.loaded)
      loadLocked(c);
   c.state.dismissedVersions[c.name] = version;
   saveLocked(c);
}

bool enabled() {
   Checker &c = checker();
   std::lock_guard<std::mutex> lock(c.mutex);
   if (!c.loaded)
      loadLocked(c);
   return c.state.enabled;
}

void setEnabled(bool on) {
   Checker &c = checker();
   std::lock_guard<std::mutex> lock(c.mutex);
   if (!c.loaded)
      loadLocked(c);
   c.state.enabled = on;
   saveLocked(c);
   if (on) {
      c.tried = false;
      startIfDueLocked(c);
   }
}

std::shared_ptr<void> startChecking() {
   Checker &c = checker();
   {
      std::lock_guard<std::mutex> lock(c.mutex);
      ++c.users;
      // Pick up what other plugins (or other hosts) have written since.
      if (!c.running)
         loadLocked(c);
      startIfDueLocked(c);
   }
   return std::shared_ptr<void>(&c, [](void *) { release(); });
}

void setStateFileForTesting(const std::filesystem::path &file) {
   Checker &c = checker();
   std::lock_guard<std::mutex> lock(c.mutex);
   c.testFile = file;
   c.loaded = false;
}

void openInBrowser(const std::string &url) {
   // Only web pages: this hands the string to the system to open.
   if (url.rfind("https://", 0) != 0)
      return;
#if defined(_WIN32)
   ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
   // Children from earlier calls that have finished; `open` and `xdg-open` exit quickly.
   static std::vector<pid_t> children;
   for (auto it = children.begin(); it != children.end();)
      it = waitpid(*it, nullptr, WNOHANG) != 0 ? children.erase(it) : it + 1;
#if defined(__APPLE__)
   const char *program = "/usr/bin/open";
#else
   const char *program = "xdg-open";
#endif
   std::string arg = url;
   char *argv[] = {const_cast<char *>(program), arg.data(), nullptr};
   pid_t pid = 0;
   if (posix_spawnp(&pid, program, nullptr, nullptr, argv, environ) == 0)
      children.push_back(pid);
#endif
}

} // namespace jclap::ui::updates
