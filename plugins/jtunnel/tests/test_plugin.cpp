// Two whole plugins in one process, the way two tracks in a DAW would run them: one sends a tone and
// a note, the other finds the tunnel by name and plays them. The audio really goes over UDP
// (loopback); the blocks are paced in real time so the network threads keep up as they would.

#include <jclap/core/test_host.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "editor.h"
#include "params.h"

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

   const uint32_t block = 256;
   std::vector<float> inL(block), inR(block), outL(block), outR(block), rxL(block), rxR(block);
   float *txIn[2] = {inL.data(), inR.data()};
   float *txOut[2] = {outL.data(), outR.data()};
   float *rxIn[2] = {rxL.data(), rxR.data()};
   float *rxOut[2] = {rxL.data(), rxR.data()};
   uint64_t frame = 0;
   bool heard = false, noteSent = false, noteHeard = false;
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
         noteSent = true;
      }
      sender.process(block, txIn, txOut);
      const jclap::core::TestOutput out = receiver.process(block, rxIn, rxOut);
      for (const auto &m : out.midi)
         noteHeard = noteHeard || (m.status == 0x90 && m.data1 == 62 && m.data2 == 110);

      double sum = 0.0;
      for (uint32_t i = 0; i < block; ++i)
         sum += rxL[i] * rxL[i];
      rms = std::sqrt(sum / block);
      heard = heard || rms > 0.2;
      if (heard && noteHeard)
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
   JCLAP_CHECK_EQ(txStatus.listeners, 1);
   JCLAP_CHECK_EQ(rxStatus.connection, 3);
   // The sender's own output is its input (Thru on).
   JCLAP_CHECK(std::abs(outL[10] - inL[10]) < 1e-6f);
   // The receiver lists the tunnel it found.
   bool listed = false;
   for (const std::string &n : rx->tunnelNames())
      listed = listed || n == name;
   JCLAP_CHECK(listed);

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
   return jclap::core::testResult();
}
