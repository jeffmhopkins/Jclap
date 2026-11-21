#include "state_server.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "viewer_html.h" // generated from viewer.html by CMake: kViewerHtml, kViewerHtmlSize

namespace jtunnel {
namespace {

// ---- SHA-1 and base64, for the WebSocket handshake only ----

std::string sha1(const std::string &message) {
   uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
   std::string data = message;
   const uint64_t bits = static_cast<uint64_t>(message.size()) * 8;
   data += static_cast<char>(0x80);
   while (data.size() % 64 != 56)
      data += '\0';
   for (int i = 7; i >= 0; --i)
      data += static_cast<char>(bits >> (8 * i));
   const auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
   for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
      uint32_t w[80];
      for (int i = 0; i < 16; ++i) {
         const auto *p = reinterpret_cast<const unsigned char *>(data.data() + chunk + 4 * i);
         w[i] = static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
                static_cast<uint32_t>(p[2]) << 8 | p[3];
      }
      for (int i = 16; i < 80; ++i)
         w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
      for (int i = 0; i < 80; ++i) {
         uint32_t f, k;
         if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
         }
         else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
         }
         else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
         }
         else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
         }
         const uint32_t t = rol(a, 5) + f + e + k + w[i];
         e = d;
         d = c;
         c = rol(b, 30);
         b = a;
         a = t;
      }
      h[0] += a;
      h[1] += b;
      h[2] += c;
      h[3] += d;
      h[4] += e;
   }
   std::string digest;
   for (uint32_t v : h)
      for (int i = 3; i >= 0; --i)
         digest += static_cast<char>(v >> (8 * i));
   return digest;
}

std::string base64(const std::string &bytes) {
   static const char *const table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   std::string out;
   size_t i = 0;
   for (; i + 2 < bytes.size(); i += 3) {
      const uint32_t v = static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << 16 |
                         static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8 |
                         static_cast<unsigned char>(bytes[i + 2]);
      out += table[v >> 18];
      out += table[(v >> 12) & 63];
      out += table[(v >> 6) & 63];
      out += table[v & 63];
   }
   if (i < bytes.size()) {
      uint32_t v = static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << 16;
      if (i + 1 < bytes.size())
         v |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8;
      out += table[v >> 18];
      out += table[(v >> 12) & 63];
      out += i + 1 < bytes.size() ? table[(v >> 6) & 63] : '=';
      out += '=';
   }
   return out;
}

std::string lower(std::string s) {
   for (char &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
   return s;
}

std::string trim(const std::string &s) {
   size_t a = 0, b = s.size();
   while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
      ++a;
   while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
      --b;
   return s.substr(a, b - a);
}

// A request's Host must be this computer, so a web page can't reach the server through a name it
// points at 127.0.0.1 (DNS rebinding). No Host at all (an HTTP/1.0 tool) is fine.
bool localHost(const std::string &host) {
   if (host.empty())
      return true;
   std::string name = lower(host);
   const size_t colon = name.rfind(':');
   if (colon != std::string::npos && name.find(']') == std::string::npos)
      name.resize(colon);
   return name == "127.0.0.1" || name == "localhost" || name == "[::1]";
}

std::string httpResponse(const char *status, const char *type, const std::string &body) {
   char head[320];
   std::snprintf(head, sizeof(head),
                 "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nAccess-Control-Allow-Origin: *\r\n"
                 "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                 status, type, body.size());
   return head + body;
}

constexpr size_t kMaxRequest = 16 * 1024;
// A client that stops reading is dropped once this much waits for it.
constexpr size_t kMaxPending = 8 * 1024 * 1024;

struct Client {
   net::TcpSocket socket;
   std::string in, out;
   bool websocket = false;
   bool closing = false; // close once `out` is sent
   uint64_t version = 0;
   uint32_t epoch = 0;
   bool needSnapshot = true;
};

} // namespace

std::string StateServer::acceptKey(const std::string &key) {
   return base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
}

std::string StateServer::textFrame(const std::string &payload) {
   std::string frame;
   frame += static_cast<char>(0x81); // final, text
   const uint64_t n = payload.size();
   if (n < 126)
      frame += static_cast<char>(n);
   else if (n < 65536) {
      frame += static_cast<char>(126);
      frame += static_cast<char>(n >> 8);
      frame += static_cast<char>(n);
   }
   else {
      frame += static_cast<char>(127);
      for (int i = 7; i >= 0; --i)
         frame += static_cast<char>(n >> (8 * i));
   }
   return frame + payload;
}

bool StateServer::start(StateStore &store) {
   stop();
   net::startup();
   bool listening = false;
   for (int i = 0; i < kPortCount && !listening; ++i)
      listening = listener_.listenLoopback(static_cast<uint16_t>(kFirstPort + i));
   if (!listening) {
      net::cleanup();
      return false;
   }
   store_ = &store;
   port_ = listener_.localPort();
   running_ = true;
   thread_ = std::thread([this] { run(); });
   return true;
}

void StateServer::stop() {
   if (!running_ && !listener_.isOpen())
      return;
   running_ = false;
   if (thread_.joinable())
      thread_.join();
   listener_.close();
   port_ = 0;
   clients_ = 0;
   net::cleanup();
}

void StateServer::run() {
   std::vector<std::unique_ptr<Client>> clients;
   std::vector<StateEntry> changes;
   char buffer[16384];
   while (running_) {
      std::vector<const net::TcpSocket *> readers{&listener_}, writers;
      for (const auto &c : clients) {
         readers.push_back(&c->socket);
         if (!c->out.empty())
            writers.push_back(&c->socket);
      }
      // Changes go out within this, which is well under a video frame.
      net::TcpSocket::wait(readers, writers, 15);

      net::TcpSocket accepted;
      while (clients.size() < 64 && listener_.accept(accepted)) {
         clients.push_back(std::make_unique<Client>());
         clients.back()->socket = std::move(accepted);
      }

      for (auto &cp : clients) {
         Client &c = *cp;
         for (;;) {
            const int n = c.socket.read(buffer, sizeof(buffer));
            if (n < 0) {
               c.socket.close();
               break;
            }
            if (n == 0)
               break;
            c.in.append(buffer, static_cast<size_t>(n));
            if (c.in.size() > kMaxRequest + 65536) {
               c.socket.close();
               break;
            }
         }
         if (!c.socket.isOpen())
            continue;

         if (!c.websocket && !c.closing) {
            const size_t end = c.in.find("\r\n\r\n");
            if (end == std::string::npos) {
               if (c.in.size() > kMaxRequest)
                  c.socket.close();
               continue;
            }
            // The request line and the headers this needs.
            const std::string head = c.in.substr(0, end);
            c.in.clear();
            std::string method, path, host, upgrade, key;
            size_t lineEnd = head.find("\r\n");
            const std::string requestLine = head.substr(0, lineEnd);
            const size_t sp1 = requestLine.find(' '), sp2 = requestLine.find(' ', sp1 + 1);
            if (sp1 != std::string::npos && sp2 != std::string::npos) {
               method = requestLine.substr(0, sp1);
               path = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
            }
            while (lineEnd != std::string::npos) {
               const size_t next = head.find("\r\n", lineEnd + 2);
               const std::string line = head.substr(lineEnd + 2, next == std::string::npos ? std::string::npos : next - lineEnd - 2);
               lineEnd = next;
               const size_t colon = line.find(':');
               if (colon == std::string::npos)
                  continue;
               const std::string name = lower(trim(line.substr(0, colon)));
               const std::string value = trim(line.substr(colon + 1));
               if (name == "host")
                  host = value;
               else if (name == "upgrade")
                  upgrade = lower(value);
               else if (name == "sec-websocket-key")
                  key = value;
            }
            const size_t query = path.find('?');
            if (query != std::string::npos)
               path.resize(query);
            c.closing = true;
            if (!localHost(host))
               c.out = httpResponse("403 Forbidden", "text/plain", "JTunnel answers this computer only\n");
            else if (method != "GET")
               c.out = httpResponse("405 Method Not Allowed", "text/plain", "GET only\n");
            else if (upgrade == "websocket" && !key.empty()) {
               c.out = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Sec-WebSocket-Accept: " +
                       acceptKey(key) + "\r\n\r\n";
               c.websocket = true;
               c.closing = false;
               c.needSnapshot = true;
            }
            else if (path == "/" || path == "/index.html")
               c.out = httpResponse("200 OK", "text/html; charset=utf-8",
                                    std::string(reinterpret_cast<const char *>(kViewerHtml), kViewerHtmlSize));
            else if (path == "/state")
               c.out = httpResponse("200 OK", "application/json", store_->snapshotJson());
            else if (path.rfind("/state/", 0) == 0) {
               std::string value;
               if (store_->get(path.substr(7), value))
                  c.out = httpResponse("200 OK", "application/json", value);
               else
                  c.out = httpResponse("404 Not Found", "application/json", "null");
            }
            else
               c.out = httpResponse("404 Not Found", "text/plain", "Not found\n");
         }

         if (c.websocket) {
            // What the client sends: only close and ping mean anything.
            while (c.in.size() >= 2) {
               const auto *p = reinterpret_cast<const unsigned char *>(c.in.data());
               const int opcode = p[0] & 0x0F;
               const bool masked = (p[1] & 0x80) != 0;
               uint64_t len = p[1] & 0x7F;
               size_t at = 2;
               if (len == 126) {
                  if (c.in.size() < 4)
                     break;
                  len = static_cast<uint64_t>(p[2]) << 8 | p[3];
                  at = 4;
               }
               else if (len == 127) {
                  if (c.in.size() < 10)
                     break;
                  len = 0;
                  for (int i = 0; i < 8; ++i)
                     len = len << 8 | p[2 + i];
                  at = 10;
               }
               if (len > 65536) {
                  c.socket.close();
                  break;
               }
               const size_t maskAt = at;
               if (masked)
                  at += 4;
               if (c.in.size() < at + len)
                  break;
               std::string payload = c.in.substr(at, static_cast<size_t>(len));
               if (masked)
                  for (size_t i = 0; i < payload.size(); ++i)
                     payload[i] = static_cast<char>(payload[i] ^ c.in[maskAt + (i & 3)]);
               c.in.erase(0, at + static_cast<size_t>(len));
               if (opcode == 0x8) {
                  c.out += std::string("\x88\x00", 2);
                  c.closing = true;
                  c.websocket = false;
                  break;
               }
               if (opcode == 0x9 && payload.size() < 126)
                  c.out += std::string(1, static_cast<char>(0x8A)) + static_cast<char>(payload.size()) + payload;
            }
            if (!c.socket.isOpen() || !c.websocket)
               continue;
            // What changed since this client last heard.
            const uint32_t epoch = store_->epoch();
            if (c.needSnapshot || epoch != c.epoch) {
               c.needSnapshot = false;
               c.epoch = epoch;
               c.version = store_->version();
               c.out += textFrame("{\"type\":\"snapshot\",\"state\":" + store_->snapshotJson() + "}");
            }
            else if (store_->version() != c.version) {
               changes.clear();
               uint32_t seenEpoch = 0;
               const uint64_t now = store_->changes(c.version, changes, &seenEpoch);
               if (seenEpoch != c.epoch) {
                  c.needSnapshot = true;
               }
               else {
                  c.version = now;
                  for (const StateEntry &e : changes)
                     c.out += textFrame("{\"type\":\"update\",\"key\":" + jsonQuote(e.key) + ",\"value\":" + e.value + "}");
               }
            }
            if (c.out.size() > kMaxPending)
               c.socket.close();
         }
      }

      // Send what's waiting, and let go of what's done.
      for (auto &cp : clients) {
         Client &c = *cp;
         while (c.socket.isOpen() && !c.out.empty()) {
            const int n = c.socket.write(c.out.data(), std::min<size_t>(c.out.size(), 1 << 20));
            if (n < 0)
               c.socket.close();
            if (n <= 0)
               break;
            c.out.erase(0, static_cast<size_t>(n));
         }
         if (c.closing && c.out.empty())
            c.socket.close();
      }
      clients.erase(std::remove_if(clients.begin(), clients.end(), [](const auto &c) { return !c->socket.isOpen(); }),
                    clients.end());
      clients_ = static_cast<int>(std::count_if(clients.begin(), clients.end(), [](const auto &c) { return c->websocket; }));
   }
}

} // namespace jtunnel
