// Two whole plugins in one process, the way two tracks in a DAW would run them: one sends a tone and
// a note, the other finds the tunnel by name and plays them. The audio really goes over UDP
// (loopback); the blocks are paced in real time so the network threads keep up as they would.

#include <jclap/core/test_host.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "editor.h"
#include "params.h"
#include "net.h"
#include "protocol.h"
#include "script_link.h"
#include "state_server.h"

extern "C" const clap_plugin_entry_t clap_entry;

namespace {

constexpr const char *kId = "com.jeffmhopkins.jclap.jtunnel";
constexpr double kPi = 3.14159265358979323846;

std::vector<uint8_t> saveState(jclap::core::TestHost &h) {
   auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
   std::vector<uint8_t> bytes;
   clap_ostream_t os{&bytes, [](const clap_ostream_t *s, const void *d, uint64_t n) -> int64_t {
                        auto *b = static_cast<std::vector<uint8_t> *>(s->ctx);
                        b->insert(b->end(), static_cast<const uint8_t *>(d), static_cast<const uint8_t *>(d) + n);
                        return static_cast<int64_t>(n);
                     }};
   JCLAP_CHECK(state->save(h.plugin(), &os));
   return bytes;
}

bool loadState(jclap::core::TestHost &h, const std::vector<uint8_t> &bytes) {
   auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
   size_t pos = 0;
   std::pair<const std::vector<uint8_t> *, size_t *> ctx{&bytes, &pos};
   clap_istream_t is{&ctx, [](const clap_istream_t *s, void *d, uint64_t n) -> int64_t {
                        auto *c = static_cast<std::pair<const std::vector<uint8_t> *, size_t *> *>(s->ctx);
                        const size_t k = std::min<size_t>(n, c->first->size() - *c->second);
                        std::memcpy(d, c->first->data() + *c->second, k);
                        *c->second += k;
                        return static_cast<int64_t>(k);
                     }};
   return state->load(h.plugin(), &is);
}

// A GET to the local state server; the body, or "" if it didn't answer.
std::string httpGet(uint16_t port, const std::string &path, const std::string &extraHeaders = {}) {
   jtunnel::net::TcpSocket s;
   if (!s.connectLoopback(port, 1000))
      return {};
   const std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n" +
                               extraHeaders + "\r\n";
   s.write(request.data(), request.size());
   std::string reply;
   char buffer[4096];
   const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
   while (std::chrono::steady_clock::now() < until) {
      const int n = s.read(buffer, sizeof(buffer));
      if (n < 0)
         break;
      if (n == 0) {
         std::this_thread::sleep_for(std::chrono::milliseconds(5));
         continue;
      }
      reply.append(buffer, static_cast<size_t>(n));
   }
   return reply;
}

} // namespace

int main() {
   using jtunnel::EditorHost;
   jclap::core::TestHost sender(clap_entry, kId);
   jclap::core::TestHost receiver(clap_entry, kId);
   EditorHost *tx = sender.object<EditorHost>();
   EditorHost *rx = receiver.object<EditorHost>();
   JCLAP_CHECK(tx && rx);
   if (!tx || !rx)
      return jclap::core::testResult();

   // Params read back as text and parse.
   JCLAP_CHECK(sender.paramText(jtunnel::kParamMode, 1.0) == "Receive");
   JCLAP_CHECK(sender.paramText(jtunnel::kParamBitrate, 192.0) == "192 kbps");
   JCLAP_CHECK(sender.paramText(jtunnel::kParamLevel, -60.0) == "-inf dB");
   double v = 0.0;
   JCLAP_CHECK(sender.textToValue(jtunnel::kParamCodec, "pcm", v) && v == 1.0);
   JCLAP_CHECK(sender.textToValue(jtunnel::kParamBuffer, "35 ms", v) && v == 35.0);

   // Names are cleaned up: no characters that don't survive mDNS.
   tx->setSendName("  Drums/Kit.1 ");
   JCLAP_CHECK(tx->sendName() == "DrumsKit1");
   // A unique name per run, so two CI jobs on one machine can't hear each other.
   std::mt19937 rng(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
   const std::string name = "CI " + std::to_string(rng() % 100000);
   tx->setSendName(name);
   rx->setListenTarget(name);
   receiver.setParam(jtunnel::kParamMode, jtunnel::kModeReceive);
   // The sender's filter holds transport back, and the receiver's holds back channel 10.
   sender.setParam(jtunnel::kParamTransport, 0.0);
   receiver.setParam(jtunnel::kParamChannel1 + 9, 0.0);

   const uint32_t block = 256;
   std::vector<float> inL(block), inR(block), outL(block), outR(block), rxL(block), rxR(block);
   float *txIn[2] = {inL.data(), inR.data()};
   float *txOut[2] = {outL.data(), outR.data()};
   float *rxIn[2] = {rxL.data(), rxR.data()};
   float *rxOut[2] = {rxL.data(), rxR.data()};
   uint64_t frame = 0;
   bool heard = false, noteSent = false, noteHeard = false;
   // Everything else a note port brings goes through too, in order.
   const std::vector<uint8_t> dump = {0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7};
   std::vector<jclap::core::TestMidi> otherMidi;
   std::vector<std::vector<uint8_t>> sysexHeard;
   std::vector<clap_event_note_expression_t> expressionsHeard;
   double rms = 0.0;
   const auto start = std::chrono::steady_clock::now();
   auto next = start;
   // Up to 6 s: discovery, subscription and the receive buffer take well under 2 s on loopback.
   while (std::chrono::steady_clock::now() - start < std::chrono::seconds(6)) {
      for (uint32_t i = 0; i < block; ++i) {
         inL[i] = inR[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 440.0 * static_cast<double>(frame + i) / 48000.0));
         rxL[i] = rxR[i] = 0.0f;
      }
      frame += block;
      if (heard && !noteSent) {
         sender.midi(0x90, 62, 110);
         sender.midi(0xB0, 74, 99, 1);
         sender.midi(0xE1, 0x00, 0x50, 2);
         sender.midi(0xF8, 0, 0, 3);
         sender.midi(0xFA, 0, 0, 3);
         sender.midi(0xA0, 62, 40, 3);   // poly aftertouch
         sender.midi(0xD0, 55, 0, 3);    // channel pressure
         sender.midi(0xC0, 7, 0, 3);     // program change
         // An MPE voice on channel 2: its own bend, slide (CC 74) and pressure.
         sender.midi(0xE1, 0x10, 0x48, 3);
         sender.midi(0xB1, 74, 80, 3);
         sender.midi(0xD1, 90, 0, 3);
         sender.midi(0xB9, 7, 100, 3);   // channel 10: held back by the receiver
         sender.sysex(dump, 4);
         sender.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 62, 0.75, -1, 0, 5);
         noteSent = true;
      }
      sender.process(block, txIn, txOut);
      const jclap::core::TestOutput out = receiver.process(block, rxIn, rxOut);
      for (const auto &m : out.midi) {
         noteHeard = noteHeard || (m.status == 0x90 && m.data1 == 62 && m.data2 == 110);
         if (m.status != 0x90 && m.status != 0x80)
            otherMidi.push_back(m);
      }
      sysexHeard.insert(sysexHeard.end(), out.sysex.begin(), out.sysex.end());
      expressionsHeard.insert(expressionsHeard.end(), out.expressions.begin(), out.expressions.end());

      double sum = 0.0;
      for (uint32_t i = 0; i < block; ++i)
         sum += rxL[i] * rxL[i];
      rms = std::sqrt(sum / block);
      heard = heard || rms > 0.2;
      if (heard && noteHeard && !sysexHeard.empty() && !expressionsHeard.empty())
         break;
      next += std::chrono::microseconds(5333);
      std::this_thread::sleep_until(next);
   }
   // The link reports "receiving" on its next housekeeping pass; keep the audio going until then.
   for (int i = 0; i < 200 && rx->status().connection != 3; ++i) {
      sender.process(block, txIn, txOut);
      receiver.process(block, rxIn, rxOut);
      next += std::chrono::microseconds(5333);
      std::this_thread::sleep_until(next);
   }
   const jtunnel::TunnelStatus txStatus = tx->status();
   const jtunnel::TunnelStatus rxStatus = rx->status();
   std::printf("heard %d (rms %.3f), note %d, listeners %d, connection %d, state %d, buffer %.1f ms\n", heard, rms,
               noteHeard, txStatus.listeners, rxStatus.connection, rxStatus.state, rxStatus.fillMs);
   JCLAP_CHECK(heard);
   JCLAP_CHECK(noteHeard);
   // The sender's MIDI filter holds transport back (see below), so start doesn't arrive.
   const std::vector<std::vector<int>> expected = {
      {0xB0, 74, 99}, {0xE1, 0x00, 0x50}, {0xF8, 0, 0},  {0xA0, 62, 40}, {0xD0, 55, 0},
      {0xC0, 7, 0},   {0xE1, 0x10, 0x48}, {0xB1, 74, 80}, {0xD1, 90, 0},
   };
   JCLAP_CHECK_EQ(otherMidi.size(), expected.size());
   for (size_t i = 0; i < otherMidi.size() && i < expected.size(); ++i)
      JCLAP_CHECK(otherMidi[i].status == expected[i][0] && otherMidi[i].data1 == expected[i][1] &&
                  otherMidi[i].data2 == expected[i][2]);
   JCLAP_CHECK(sysexHeard.size() == 1 && sysexHeard[0] == dump);
   JCLAP_CHECK(expressionsHeard.size() == 1 && expressionsHeard[0].expression_id == CLAP_NOTE_EXPRESSION_PRESSURE &&
               expressionsHeard[0].key == 62 && expressionsHeard[0].channel == 0 && expressionsHeard[0].value == 0.75);
   JCLAP_CHECK_EQ(txStatus.listeners, 1);
   JCLAP_CHECK_EQ(rxStatus.connection, 3);
   // The panels' MIDI lights saw all six go out and come in (the SysEx once), the last of them the
   // expression on the note's key.
   JCLAP_CHECK(txStatus.midiEvents >= 6 && (txStatus.lastMidi & 0xff) == jtunnel::kTagExpression);
   JCLAP_CHECK(rxStatus.midiEvents >= 6 && (rxStatus.lastMidi & 0xff) == jtunnel::kTagExpression &&
               (rxStatus.lastMidi >> 24) == 62);
   // The sender's own output is its input (Thru on).
   JCLAP_CHECK(std::abs(outL[10] - inL[10]) < 1e-6f);
   // The receiver lists the tunnel it found.
   bool listed = false;
   for (const std::string &n : rx->tunnelNames())
      listed = listed || n == name;
   JCLAP_CHECK(listed);

   // The side channel. A stand-in for the Bitwig extension listens where the real one would, takes
   // the sender's hello and sends it a track and the scenes; the host's transport plays. All of it
   // comes out of the receiver's local server, over HTTP and a WebSocket.
   {
      jtunnel::net::UdpSocket script;
      const bool scriptPort = script.open(jtunnel::ScriptLink::kScriptPort, true);
      if (!scriptPort)
         std::printf("port %d is taken (Bitwig running?): the extension part is skipped\n",
                     jtunnel::ScriptLink::kScriptPort);
      std::string clipNames;
      for (int i = 0; i < 64; ++i)
         clipNames += std::string(i ? "," : "") + "{\"name\":\"Clip " + std::to_string(i) + "\",\"state\":\"stopped\"}";
      const std::string track = "{\"name\":\"Drums\",\"slots\":[" + clipNames + "]}"; // several packets long
      const std::string scenes = R"({"items":[{"name":"Verse"},{"name":"Chorus"}]})";
      sender.setTransport(128.0, 0.0, true);
      bool helloHeard = false, gotTrack = false, gotClock = false, gotScenes = false;
      uint16_t rxPort = 0;
      const auto begin = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - begin < std::chrono::seconds(8)) {
         if (scriptPort) {
            uint8_t packet[2048];
            jtunnel::net::Address from;
            const int n = script.receive(packet, sizeof(packet), from, 0);
            std::string address, tags;
            std::vector<int32_t> ints;
            std::vector<std::string> strings;
            if (n > 0 && jtunnel::ScriptLink::parse(packet, static_cast<size_t>(n), address, tags, ints, strings) &&
                address == "/jtunnel/hello" && tags == "is" && strings[0] == name) {
               helloHeard = true;
               const std::string a[2] = {"track/0", track}, b[2] = {"scenes", scenes};
               for (const std::string &m : {jtunnel::ScriptLink::message("/jtunnel/state", "ss", nullptr, a),
                                            jtunnel::ScriptLink::message("/jtunnel/state", "ss", nullptr, b),
                                            jtunnel::ScriptLink::message("/jtunnel/alive", "", nullptr, nullptr)})
                  script.sendTo(jtunnel::net::Address::loopback(static_cast<uint16_t>(ints[0])), m.data(), m.size());
            }
         }
         sender.process(block, txIn, txOut);
         receiver.process(block, rxIn, rxOut);
         next += std::chrono::microseconds(5333);
         std::this_thread::sleep_until(next);
         rxPort = rx->status().statePort;
         if (rxPort && (frame += block) % (block * 40) == 0) {
            const std::string state = httpGet(rxPort, "/state");
            gotTrack = state.find("\"track/0\":" + track) != std::string::npos;
            gotScenes = state.find("\"scenes\":" + scenes) != std::string::npos;
            gotClock = state.find("\"clock\":{\"available\":true,\"playing\":true") != std::string::npos &&
                       state.find("\"tempo\":128.000") != std::string::npos;
            if (gotClock && (!scriptPort || (gotTrack && gotScenes)))
               break;
         }
      }
      std::printf("side channel: hello %d, track %d, scenes %d, clock %d, receiver's server on %u\n", helloHeard,
                  gotTrack, gotScenes, gotClock, rxPort);
      JCLAP_CHECK(rxPort >= jtunnel::StateServer::kFirstPort);
      JCLAP_CHECK(gotClock);
      if (scriptPort) {
         JCLAP_CHECK(helloHeard && gotTrack && gotScenes);
         JCLAP_CHECK(tx->status().extension);
      }
      // One key; a missing one; a request whose Host isn't this computer.
      const std::string one = httpGet(rxPort, "/state/clock");
      JCLAP_CHECK(one.find("200 OK") != std::string::npos && one.find("Access-Control-Allow-Origin: *") != std::string::npos);
      JCLAP_CHECK(httpGet(rxPort, "/state/nothing").find("404") != std::string::npos);
      JCLAP_CHECK(httpGet(rxPort, "/").find("text/html") != std::string::npos);
      {
         jtunnel::net::TcpSocket s;
         JCLAP_CHECK(s.connectLoopback(rxPort, 1000));
         const std::string evil = "GET /state HTTP/1.1\r\nHost: attacker.example\r\n\r\n";
         s.write(evil.data(), evil.size());
         std::string reply;
         char buffer[1024];
         for (int i = 0; i < 200 && reply.find("\r\n\r\n") == std::string::npos; ++i) {
            const int n = s.read(buffer, sizeof(buffer));
            if (n < 0)
               break;
            if (n > 0)
               reply.append(buffer, static_cast<size_t>(n));
            else
               std::this_thread::sleep_for(std::chrono::milliseconds(5));
         }
         JCLAP_CHECK(reply.find("403") != std::string::npos);
      }
      // A WebSocket: the handshake, a snapshot with everything, then updates as the clock moves.
      {
         jtunnel::net::TcpSocket ws;
         JCLAP_CHECK(ws.connectLoopback(rxPort, 1000));
         const std::string hello = "GET /ws HTTP/1.1\r\nHost: localhost:" + std::to_string(rxPort) +
                                   "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "
                                   "dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
         ws.write(hello.data(), hello.size());
         std::string got;
         char buffer[8192];
         const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
         while (std::chrono::steady_clock::now() < until && got.find("\"type\":\"update\"") == std::string::npos) {
            sender.process(block, txIn, txOut);
            receiver.process(block, rxIn, rxOut);
            next += std::chrono::microseconds(5333);
            std::this_thread::sleep_until(next);
            const int n = ws.read(buffer, sizeof(buffer));
            if (n > 0)
               got.append(buffer, static_cast<size_t>(n));
         }
         JCLAP_CHECK(got.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
         JCLAP_CHECK(got.find("{\"type\":\"snapshot\",\"state\":{") != std::string::npos);
         JCLAP_CHECK(got.find("{\"type\":\"update\",\"key\":\"clock\"") != std::string::npos);
         if (scriptPort)
            JCLAP_CHECK(got.find(track) != std::string::npos);
         // The viewer count reaches the status a moment after the server takes the connection (the
         // server counts it, then the link's state thread copies it over), so give it a while.
         int clients = 0;
         for (int i = 0; i < 200 && (clients = rx->status().stateClients) != 1; ++i) {
            sender.process(block, txIn, txOut);
            receiver.process(block, rxIn, rxOut);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
         }
         JCLAP_CHECK(clients == 1);
      }
      sender.clearTransport();
   }

   // A second sender with a name already in use doesn't take it: it waits, and gets it when the
   // first goes. (In one process the two find each other directly, network or not.)
   {
      auto first = std::make_unique<jclap::core::TestHost>(clap_entry, kId);
      jclap::core::TestHost second(clap_entry, kId);
      EditorHost *a = first->object<EditorHost>();
      EditorHost *b = second.object<EditorHost>();
      const std::string shared = name + " dup";
      a->setSendName(shared);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      b->setSendName(shared);
      std::string takenA, takenB;
      for (int i = 0; i < 60 && (takenB.empty() || !takenA.empty()); ++i) {
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
         takenA = a->status().nameTakenBy;
         takenB = b->status().nameTakenBy;
      }
      std::printf("same name: first \"%s\", second \"%s\"\n", takenA.c_str(), takenB.c_str());
      JCLAP_CHECK(takenA.empty());
      JCLAP_CHECK(takenB == "another track here");
      int listed = 0;
      for (const std::string &n : rx->tunnelNames())
         listed += n == shared;
      JCLAP_CHECK(listed <= 1);
      first.reset();
      for (int i = 0; i < 30 && !takenB.empty(); ++i) {
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
         takenB = b->status().nameTakenBy;
      }
      JCLAP_CHECK(takenB.empty());
   }

   // State carries the params and both names.
   {
      const std::vector<uint8_t> bytes = saveState(receiver);
      jclap::core::TestHost copy(clap_entry, kId);
      JCLAP_CHECK(loadState(copy, bytes));
      EditorHost *c = copy.object<EditorHost>();
      JCLAP_CHECK(c && c->listenTarget() == name);
      JCLAP_CHECK_EQ(copy.paramValue(jtunnel::kParamMode), 1.0);
      std::vector<uint8_t> broken = bytes;
      broken[0] = 'X';
      JCLAP_CHECK(!loadState(copy, broken));
   }

   // A project saved before Carry existed still loads, carrying both.
   {
      receiver.setParam(jtunnel::kParamCarry, jtunnel::kCarryMidi);
      receiver.process(block, rxIn, rxOut);
      std::vector<uint8_t> bytes = saveState(receiver);
      jclap::core::TestHost copy(clap_entry, kId);
      JCLAP_CHECK(loadState(copy, bytes));
      JCLAP_CHECK_EQ(copy.paramValue(jtunnel::kParamCarry), 2.0);
      bytes.erase(bytes.begin() + 12 + 8 * jtunnel::kParamCarry, bytes.begin() + 12 + 8 * jtunnel::kParamCount);
      bytes[8] = static_cast<uint8_t>(jtunnel::kParamCarry);
      jclap::core::TestHost old(clap_entry, kId);
      JCLAP_CHECK(loadState(old, bytes));
      JCLAP_CHECK_EQ(old.paramValue(jtunnel::kParamCarry), 0.0);
      JCLAP_CHECK_EQ(old.paramValue(jtunnel::kParamMode), 1.0);
      EditorHost *o = old.object<EditorHost>();
      JCLAP_CHECK(o && o->listenTarget() == name);
      char text[32];
      jtunnel::formatParam(jtunnel::kParamCarry, 2, text, sizeof(text));
      JCLAP_CHECK(std::string(text) == "MIDI");
      double value = 0;
      JCLAP_CHECK(jtunnel::parseParam(jtunnel::kParamCarry, "audio", value) && value == 1.0);
   }
   return jclap::core::testResult();
}
