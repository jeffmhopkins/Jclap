#include "gen/fetch.h"

#include <cstdio>
#include <fstream>
#include <vector>

#include "gen/sha256.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#endif

namespace jkit::gen {
namespace {

// Writes the body to the .part file and hashes it.
struct Sink {
   std::FILE *file = nullptr;
   Sha256 hash;
   uint64_t received = 0;
   const std::function<void(uint64_t)> *progress = nullptr;
   const std::atomic<bool> *cancel = nullptr;
   bool failed = false;

   bool write(const void *data, size_t n) {
      if (cancel->load() || std::fwrite(data, 1, n, file) != n) {
         failed = true;
         return false;
      }
      hash.update(data, n);
      received += n;
      if (*progress)
         (*progress)(received);
      return true;
   }
};

std::FILE *openFile(const std::filesystem::path &p) {
#if defined(_WIN32)
   return _wfopen(p.wstring().c_str(), L"wb");
#else
   return std::fopen(p.c_str(), "wb");
#endif
}

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

bool get(const std::string &url, Sink &sink, std::string &error) {
   // https://host/path
   const std::string prefix = "https://";
   if (url.compare(0, prefix.size(), prefix) != 0) {
      error = "not an https address";
      return false;
   }
   const size_t slash = url.find('/', prefix.size());
   const std::wstring host = widen(url.substr(prefix.size(), slash - prefix.size()));
   const std::wstring path = widen(slash == std::string::npos ? "/" : url.substr(slash));
   HINTERNET session = WinHttpOpen(L"JClap-JKit", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
   if (!session)
      session = WinHttpOpen(L"JClap-JKit", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
   if (!session) {
      error = "couldn't start WinHTTP";
      return false;
   }
   WinHttpSetTimeouts(session, 30000, 30000, 60000, 60000);
   bool ok = false;
   HINTERNET connection = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
   HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                  : nullptr;
   DWORD status = 0, size = sizeof(status);
   if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
       WinHttpReceiveResponse(request, nullptr) &&
       WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                           &status, &size, WINHTTP_NO_HEADER_INDEX)) {
      if (status != 200) {
         error = "the server answered " + std::to_string(status);
      } else {
         ok = true;
         std::vector<char> buffer(1 << 16);
         for (;;) {
            DWORD available = 0, read = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
               ok = false;
               break;
            }
            if (available == 0)
               break;
            if (!WinHttpReadData(request, buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())),
                                 &read) ||
                !sink.write(buffer.data(), read)) {
               ok = false;
               break;
            }
         }
         if (!ok && error.empty())
            error = "the download broke off";
      }
   } else {
      error = "couldn't reach " + url.substr(prefix.size(), slash - prefix.size()) + " (error " +
              std::to_string(GetLastError()) + ")";
   }
   if (request)
      WinHttpCloseHandle(request);
   if (connection)
      WinHttpCloseHandle(connection);
   WinHttpCloseHandle(session);
   return ok;
}

#else

struct Curl {
   void *(*init)();
   int (*setopt)(void *, int, ...);
   int (*perform)(void *);
   int (*getinfo)(void *, int, ...);
   void (*cleanup)(void *);
   const char *(*strerror)(int);
};

enum : int {
   kOptWriteData = 10001,
   kOptUrl = 10002,
   kOptUserAgent = 10018,
   kOptWriteFunction = 20011,
   kOptFailOnError = 45,
   kOptFollowLocation = 52,
   kOptMaxRedirs = 68,
   kOptNoSignal = 99,
   kOptConnectTimeoutMs = 156,
   kOptLowSpeedLimit = 19,
   kOptLowSpeedTime = 20,
   kInfoResponseCode = 0x200000 + 2,
};

const Curl *loadCurl() {
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
         c.strerror = reinterpret_cast<const char *(*)(int)>(dlsym(lib, "curl_easy_strerror"));
         if (c.init && c.setopt && c.perform && c.getinfo && c.cleanup)
            return &c;
      }
      return nullptr;
   }();
   return curl;
}

size_t onData(char *data, size_t size, size_t count, void *user) {
   const size_t n = size * count;
   return static_cast<Sink *>(user)->write(data, n) ? n : 0;
}

bool get(const std::string &url, Sink &sink, std::string &error) {
   const Curl *curl = loadCurl();
   void *h = curl ? curl->init() : nullptr;
   if (!h) {
      error = "this system has no libcurl to download with";
      return false;
   }
   curl->setopt(h, kOptUrl, url.c_str());
   curl->setopt(h, kOptUserAgent, "JClap-JKit");
   curl->setopt(h, kOptWriteFunction, &onData);
   curl->setopt(h, kOptWriteData, static_cast<void *>(&sink));
   curl->setopt(h, kOptFollowLocation, 1L);
   curl->setopt(h, kOptMaxRedirs, 8L);
   curl->setopt(h, kOptFailOnError, 1L);
   curl->setopt(h, kOptNoSignal, 1L);
   curl->setopt(h, kOptConnectTimeoutMs, 30000L);
   // Give up when it stalls (under 1 kB/s for a minute), not after a fixed time: the model is 2 GB.
   curl->setopt(h, kOptLowSpeedLimit, 1024L);
   curl->setopt(h, kOptLowSpeedTime, 60L);
   const int code = curl->perform(h);
   long status = 0;
   curl->getinfo(h, kInfoResponseCode, &status);
   curl->cleanup(h);
   if (code != 0) {
      error = status >= 400 ? "the server answered " + std::to_string(status)
                            : std::string(curl->strerror ? curl->strerror(code) : "the download failed");
      return false;
   }
   return true;
}

#endif

} // namespace

bool fetch(const Download &d, const std::function<void(uint64_t)> &progress, const std::atomic<bool> &cancel,
           std::string &error) {
   std::error_code ec;
   std::filesystem::create_directories(d.file.parent_path(), ec);
   std::filesystem::path part = d.file;
   part += ".part";
   Sink sink;
   sink.file = openFile(part);
   sink.progress = &progress;
   sink.cancel = &cancel;
   if (!sink.file) {
      error = "couldn't write " + part.u8string();
      return false;
   }
   const bool got = get(d.url, sink, error);
   const bool closed = std::fclose(sink.file) == 0;
   if (cancel.load()) {
      error = "cancelled";
   } else if (got && !closed) {
      error = "couldn't finish writing " + part.u8string();
   } else if (got && !d.sha256.empty() && sink.hash.hex() != d.sha256) {
      error = d.file.filename().u8string() + " didn't match its checksum";
   } else if (got) {
      std::filesystem::rename(part, d.file, ec);
      if (!ec)
         return true;
      error = "couldn't move " + d.file.filename().u8string() + " into place";
   } else if (sink.failed && error.empty()) {
      error = "couldn't write " + part.u8string();
   }
   std::filesystem::remove(part, ec);
   return false;
}

std::string fileSha256(const std::filesystem::path &file) {
   std::ifstream in(file, std::ios::binary);
   if (!in)
      return {};
   Sha256 h;
   std::vector<char> buffer(1 << 20);
   while (in) {
      in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      h.update(buffer.data(), static_cast<size_t>(in.gcount()));
   }
   return h.hex();
}

} // namespace jkit::gen
