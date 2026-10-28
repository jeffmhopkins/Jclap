// The engine without a host or a network: packets, codecs, the resampler, and a sender and receiver
// on clocks 150 ppm apart, joined by a simulated Wi-Fi link with jitter, stalls and loss.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <queue>
#include <random>
#include <vector>

#include "codec.h"
#include "impair.h"
#include "protocol.h"
#include "receive_ring.h"
#include "receiver.h"
#include "resampler.h"
#include "sender.h"

using namespace jtunnel;

namespace {

constexpr double kPi = 3.14159265358979323846;

void testProtocol() {
   Frame a, b;
   a.seq = 11;
   a.audioBytes = 3;
   a.audio[0] = 1;
   a.audio[1] = 2;
   a.audio[2] = 3;
   a.midiCount = 1;
   a.midi[0] = {7, {0x90, 60, 100}};
   b.seq = 10;
   b.audioBytes = 1;
   b.audio[0] = 9;
   AudioHeader h;
   h.streamId = 1234;
   h.seq = 11;
   h.sampleRate = kOpusSampleRate;
   h.codec = Codec::Opus;
   h.frameSamples = kOpusFrameSamples;
   const Frame *frames[2] = {&a, &b};
   uint8_t packet[kMaxPacketBytes];
   const size_t size = writeAudio(packet, sizeof(packet), h, frames, 2);
   JCLAP_CHECK(size > 0);

   AudioHeader got;
   Frame out[kMaxFramesPerPacket];
   int count = 0;
   JCLAP_CHECK(parseAudio(packet, size, got, out, count));
   JCLAP_CHECK_EQ(count, 2);
   JCLAP_CHECK_EQ(got.streamId, 1234u);
   JCLAP_CHECK_EQ(out[0].seq, 11u);
   JCLAP_CHECK_EQ(out[1].seq, 10u);
   JCLAP_CHECK_EQ(out[0].audioBytes, 3);
   JCLAP_CHECK_EQ(out[0].audio[2], 3);
   JCLAP_CHECK_EQ(out[0].midiCount, 1);
   JCLAP_CHECK_EQ(out[0].midi[0].offset, 7);
   JCLAP_CHECK_EQ(out[0].midi[0].data[1], 60);
   JCLAP_CHECK_EQ(out[1].audio[0], 9);

   // Every truncation is refused, as is a wrong magic or an Opus stream at the wrong rate.
   for (size_t cut = 0; cut < size; ++cut)
      JCLAP_CHECK(!parseAudio(packet, cut, got, out, count));
   packet[0] = 'X';
   JCLAP_CHECK(!parseAudio(packet, size, got, out, count));
   h.sampleRate = 44100;
   JCLAP_CHECK(writeAudio(packet, sizeof(packet), h, frames, 2) > 0);
   JCLAP_CHECK(!parseAudio(packet, size, got, out, count));

   // The biggest packet (two full PCM frames with full MIDI) fits one MTU.
   Frame full;
   full.audioBytes = kMaxFrameBytes;
   full.midiCount = kMaxMidiPerFrame;
   h.codec = Codec::Pcm24;
   h.sampleRate = 48000;
   h.frameSamples = kPcmFrameSamples;
   const Frame *fulls[2] = {&full, &full};
   JCLAP_CHECK(writeAudio(packet, sizeof(packet), h, fulls, 2) > 0);

   char name[kMaxTunnelName + 1];
   const size_t sub = writeSubscribe(packet, sizeof(packet), "Drums");
   JCLAP_CHECK(parseSubscribe(packet, sub, name, sizeof(name)));
   JCLAP_CHECK(std::string(name) == "Drums");
   JCLAP_CHECK_EQ(packetType(packet, sub), static_cast<uint8_t>(PacketType::Subscribe));
   const size_t bye = writeBye(packet, sizeof(packet));
   JCLAP_CHECK_EQ(packetType(packet, bye), static_cast<uint8_t>(PacketType::Bye));
}

void testCodecs() {
   float in[kOpusFrameSamples * 2], out[kOpusFrameSamples * 2];
   for (int i = 0; i < kOpusFrameSamples; ++i) {
      in[2 * i] = 0.5f * static_cast<float>(std::sin(2 * kPi * 1000.0 * i / 48000.0));
      in[2 * i + 1] = -in[2 * i];
   }
   Encoder enc;
   Decoder dec;
   JCLAP_CHECK(enc.setup());
   JCLAP_CHECK(dec.setup());
   uint8_t bytes[kMaxFrameBytes];

   // PCM is exact to 24 bits.
   const int pcm = enc.encode(Codec::Pcm24, in, kPcmFrameSamples, bytes, sizeof(bytes));
   JCLAP_CHECK_EQ(pcm, kPcmFrameSamples * 6);
   JCLAP_CHECK(dec.decode(Codec::Pcm24, bytes, pcm, out, kPcmFrameSamples));
   float worst = 0.0f;
   for (int i = 0; i < kPcmFrameSamples * 2; ++i)
      worst = std::max(worst, std::abs(out[i] - in[i]));
   JCLAP_CHECK(worst < 1e-6f);

   // Opus: after a few frames (its startup), a 1 kHz tone comes back at about the same level.
   enc.setBitrate(192);
   double inEnergy = 0.0, outEnergy = 0.0;
   for (int frame = 0; frame < 20; ++frame) {
      const int n = enc.encode(Codec::Opus, in, kOpusFrameSamples, bytes, sizeof(bytes));
      JCLAP_CHECK(n > 0 && n <= kMaxFrameBytes);
      JCLAP_CHECK(dec.decode(Codec::Opus, bytes, n, out, kOpusFrameSamples));
      if (frame >= 10)
         for (int i = 0; i < kOpusFrameSamples * 2; ++i) {
            inEnergy += in[i] * in[i];
            outEnergy += out[i] * out[i];
         }
   }
   JCLAP_CHECK(outEnergy > 0.7 * inEnergy && outEnergy < 1.3 * inEnergy);
   // Concealment produces something, not garbage.
   dec.conceal(Codec::Opus, out, kOpusFrameSamples);
   for (float v : out)
      JCLAP_CHECK(std::isfinite(v) && std::abs(v) < 2.0f);
}

// Resamples a tone and checks it comes out at the right frequency and level, with little else.
void checkResample(double inRate, double outRate) {
   Resampler rs;
   const double ratio = inRate / outRate;
   rs.configureFor(ratio);
   rs.reset();
   const double freq = 1000.0;
   std::vector<float> outL, outR;
   uint64_t inPos = 0;
   float l[512], r[512];
   while (outL.size() < 48000) {
      for (int i = 0; i < 256; ++i, ++inPos) {
         l[i] = static_cast<float>(0.5 * std::sin(2 * kPi * freq * inPos / inRate));
         r[i] = l[i];
      }
      rs.push(l, r, 256);
      for (int n = rs.producible(ratio); n > 0; n = rs.producible(ratio)) {
         const int take = std::min(n, 512);
         rs.produce(l, r, take, ratio);
         outL.insert(outL.end(), l, l + take);
         outR.insert(outR.end(), r, r + take);
      }
   }
   // Fit the expected tone (least squares on sin and cos) over the second half and measure what's left.
   double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
   const size_t from = outL.size() / 2;
   for (size_t i = from; i < outL.size(); ++i) {
      const double phase = 2 * kPi * freq * static_cast<double>(i) / outRate;
      const double s = std::sin(phase), c = std::cos(phase);
      ss += s * s;
      cc += c * c;
      sc += s * c;
      ys += outL[i] * s;
      yc += outL[i] * c;
   }
   const double det = ss * cc - sc * sc;
   const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
   double residual = 0, signal = 0;
   for (size_t i = from; i < outL.size(); ++i) {
      const double phase = 2 * kPi * freq * static_cast<double>(i) / outRate;
      const double fit = a * std::sin(phase) + b * std::cos(phase);
      residual += (outL[i] - fit) * (outL[i] - fit);
      signal += fit * fit;
   }
   const double amplitude = std::sqrt(a * a + b * b);
   const double snrDb = 10 * std::log10(signal / std::max(residual, 1e-30));
   std::printf("resample %.0f -> %.0f: amplitude %.4f, SNR %.1f dB\n", inRate, outRate, amplitude, snrDb);
   JCLAP_CHECK(std::abs(amplitude - 0.5) < 0.005);
   JCLAP_CHECK(snrDb > 70.0);
}

void testResampler() {
   checkResample(48000, 48000);
   checkResample(44100, 48000);
   checkResample(48000, 44100);
   checkResample(96000, 48000);
   checkResample(48000 * 1.0002, 48000); // a clock 200 ppm fast
}

void testRing() {
   ReceiveRing ring;
   AudioHeader h;
   h.streamId = 5;
   h.sampleRate = 48000;
   h.codec = Codec::Pcm24;
   h.frameSamples = kPcmFrameSamples;
   Frame f;
   f.audioBytes = kPcmFrameSamples * 6;
   for (uint32_t seq : {100u, 102u, 101u}) {
      f.seq = seq;
      ring.store(h, f);
   }
   StreamInfo info = ring.info();
   JCLAP_CHECK(info.active && info.streamId == 5 && info.newest == 102);
   Frame out;
   JCLAP_CHECK(ring.take(5, 100, out) && out.seq == 100);
   JCLAP_CHECK(!ring.take(5, 100, out)); // taken already
   ring.setReadPosition(5, 101);
   // A frame from before the read position is too late to keep.
   f.seq = 99;
   ring.store(h, f);
   JCLAP_CHECK(!ring.take(5, 99, out));
   JCLAP_CHECK(ring.take(5, 101, out));
   // A new stream takes over, and the old one's frames are gone.
   h.streamId = 6;
   f.seq = 7;
   ring.store(h, f);
   info = ring.info();
   JCLAP_CHECK(info.streamId == 6 && info.newest == 7);
   JCLAP_CHECK(!ring.take(6, 102, out));
   JCLAP_CHECK(ring.take(6, 7, out));
   ring.clear();
   JCLAP_CHECK(!ring.info().active);
}

// ---- Simulated network ----

struct Arrival {
   double time;
   std::vector<uint8_t> bytes;
   bool operator>(const Arrival &o) const { return time > o.time; }
};

struct Network : PacketSink {
   std::priority_queue<Arrival, std::vector<Arrival>, std::greater<Arrival>> inFlight;
   std::mt19937 rng{1234};
   double now = 0.0;
   double lossRate = 0.02;
   double stallUntil = -1.0, nextStall = 4.0;
   bool stalls = true;
   bool down = false;
   uint32_t sent = 0, dropped = 0;

   void send(const uint8_t *data, size_t size) override {
      ++sent;
      if (down)
         return;
      std::uniform_real_distribution<double> uniform(0.0, 1.0);
      if (uniform(rng) < lossRate) {
         ++dropped;
         return;
      }
      // Wi-Fi-ish: 2 ms plus an exponential tail (mean 3 ms), and now and then a 60 ms stall that
      // holds every packet until it ends.
      std::exponential_distribution<double> tail(1.0 / 0.003);
      double arrive = now + 0.002 + tail(rng);
      if (stalls && now >= nextStall) {
         stallUntil = now + 0.06;
         nextStall = now + 4.0 + 3.0 * uniform(rng);
      }
      if (now < stallUntil)
         arrive = std::max(arrive, stallUntil + 0.001 * uniform(rng));
      inFlight.push({arrive, std::vector<uint8_t>(data, data + size)});
   }

   void deliver(ReceiveRing &ring, double until) {
      while (!inFlight.empty() && inFlight.top().time <= until) {
         const Arrival &a = inFlight.top();
         AudioHeader h;
         Frame frames[kMaxFramesPerPacket];
         int count = 0;
         if (parseAudio(a.bytes.data(), a.bytes.size(), h, frames, count))
            for (int i = 0; i < count; ++i)
               ring.store(h, frames[i]);
         inFlight.pop();
      }
   }
};

struct SimResult {
   double integral = 0.0;
   uint32_t lost = 0, concealed = 0, concealedLate = 0;
   double fillMs = 0.0, targetMs = 0.0;
   double outputRms = 0.0;
   int longestSilence = 0;
   std::vector<std::pair<double, OutMidi>> midi; // receive time (s), event
   int notesHeldAtEnd = 0;
   ReceiveState stateAtEnd = ReceiveState::Idle;
};

// The sender's clock runs `ppm` fast. Audio is a 440 Hz tone; a note goes on at 10 s and off at
// 10.5 s, and another goes on at 70 s and is never released before the sender goes quiet at 80 s.
SimResult simulate(Codec codec, double ppm, double seconds, double senderRate = 48000.0, bool stalls = true) {
   Sender sender;
   Receiver receiver;
   ReceiveRing ring;
   Network net;
   net.stalls = stalls;
   sender.setup(senderRate, 42);
   receiver.setup(48000.0);
   receiver.setBuffer(10.0, true);
   sender.configure(codec, 192, true);

   const uint32_t block = 256;
   float inL[block], inR[block], outL[block], outR[block];
   OutMidi midi[64];
   uint64_t sentFrames = 0, playedFrames = 0;
   SimResult result;
   double sumSquares = 0.0;
   uint64_t measured = 0;
   int silence = 0;
   bool noteOn1 = false, noteOff1 = false, noteOn2 = false;
   const double dt = 0.0005;
   const double captureAt = std::min(79.0, seconds - 1.0);
   for (double t = 0.0; t < seconds; t += dt) {
      net.now = t;
      net.down = t >= 80.0 && seconds > 80.0;
      // The sender produces blocks as its (fast) clock allows.
      while (!net.down && static_cast<double>(sentFrames + block) <= t * senderRate * (1.0 + ppm * 1e-6)) {
         for (uint32_t i = 0; i < block; ++i) {
            const double phase = 2 * kPi * 440.0 * static_cast<double>(sentFrames + i) / senderRate;
            inL[i] = inR[i] = static_cast<float>(0.5 * std::sin(phase));
         }
         TimedMidi events[2];
         int count = 0;
         const double blockStart = sentFrames / (senderRate * (1.0 + ppm * 1e-6));
         if (!noteOn1 && blockStart >= 10.0) {
            events[count++] = {17, {0x90, 60, 100}};
            noteOn1 = true;
         }
         if (!noteOff1 && blockStart >= 10.5) {
            events[count++] = {33, {0x80, 60, 0}};
            noteOff1 = true;
         }
         if (!noteOn2 && blockStart >= 70.0) {
            events[count++] = {0, {0x91, 64, 90}};
            noteOn2 = true;
         }
         sender.process(inL, inR, block, events, count, net);
         sentFrames += block;
      }
      net.deliver(ring, t);
      while (static_cast<double>(playedFrames + block) <= t * 48000.0) {
         const int n = receiver.process(ring, outL, outR, block, midi, 64);
         for (int i = 0; i < n; ++i)
            result.midi.push_back({(playedFrames + midi[i].time) / 48000.0, midi[i]});
         if (t > 20.0 && t < captureAt) {
            for (uint32_t i = 0; i < block; ++i) {
               sumSquares += outL[i] * outL[i];
               ++measured;
               silence = std::abs(outL[i]) < 1e-4f ? silence + 1 : 0;
               result.longestSilence = std::max(result.longestSilence, silence);
            }
         }
         if (t > 40.0 && t < 40.01)
            result.concealedLate = receiver.stats().concealed.load();
         playedFrames += block;
      }
      if (t < captureAt && t + dt >= captureAt) {
         result.integral = receiver.drift();
         result.fillMs = receiver.stats().fillMs.load();
         result.targetMs = receiver.stats().targetMs.load();
         result.lost = receiver.stats().lost.load();
         result.concealed = receiver.stats().concealed.load();
      }
   }
   result.outputRms = measured ? std::sqrt(sumSquares / measured) : 0.0;
   result.stateAtEnd = receiver.state();
   int held = 0;
   for (const auto &event : result.midi) {
      if ((event.second.data[0] & 0xF0) == 0x90 && event.second.data[2] > 0)
         ++held;
      else
         --held;
   }
   result.notesHeldAtEnd = held;
   result.concealedLate = result.concealed - result.concealedLate;
   std::printf("%s, %+.0f ppm: drift %+.1f ppm, buffer %.1f ms (target %.1f), lost %u, late %u (%u after 40 s), rms %.3f, "
               "longest silence %d, packets %u (dropped %u)\n",
               codec == Codec::Opus ? "opus" : "pcm", ppm, result.integral * 1e6, result.fillMs, result.targetMs,
               result.lost, result.concealed, result.concealedLate, result.outputRms, result.longestSilence, net.sent, net.dropped);
   return result;
}

void testSimulatedLink() {
   const double tone = 0.5 / std::sqrt(2.0);
   for (Codec codec : {Codec::Opus, Codec::Pcm24}) {
      const SimResult r = simulate(codec, 150.0, 90.0);
      // The receiver found the sender's clock offset...
      JCLAP_CHECK(std::abs(r.integral * 1e6 - 150.0) < 60.0);
      // ...kept the buffer near its target rather than drifting off...
      JCLAP_CHECK(r.fillMs < r.targetMs + 40.0);
      // 60 ms stalls need about 75 ms of lead to ride out.
      JCLAP_CHECK(r.targetMs <= 100.0);
      JCLAP_CHECK(r.concealedLate <= 4);
      // ...and played the tone at its level, without long gaps.
      JCLAP_CHECK(std::abs(r.outputRms - tone) < 0.05);
      JCLAP_CHECK(r.longestSilence < 48);
      // Redundancy covers almost all of 2% random loss.
      JCLAP_CHECK(r.lost < 200);

      // The note arrived, and its length survived the trip.
      double on = -1, off = -1;
      for (const auto &event : r.midi) {
         if (event.second.data[0] == 0x90 && event.second.data[1] == 60)
            on = event.first;
         if (event.second.data[0] == 0x80 && event.second.data[1] == 60)
            off = event.first;
      }
      JCLAP_CHECK(on > 10.0 && on < 10.3);
      JCLAP_CHECK(std::abs((off - on) - 0.5) < 0.01);
      // The note left hanging when the sender went quiet was released, and the receiver waits.
      JCLAP_CHECK_EQ(r.notesHeldAtEnd, 0);
      JCLAP_CHECK(r.stateAtEnd == ReceiveState::Buffering);
   }
   // Jitter but no stalls: the buffer stays small.
   {
      const SimResult r = simulate(Codec::Opus, 50.0, 60.0, 48000.0, false);
      JCLAP_CHECK(r.targetMs <= 30.0);
      JCLAP_CHECK(std::abs(r.integral * 1e6 - 50.0) < 30.0);
   }
   // A sender at 44.1 kHz into a 48 kHz receiver, with Opus (resampled at the sender) and PCM
   // (resampled at the receiver), and a slow clock.
   for (Codec codec : {Codec::Opus, Codec::Pcm24}) {
      const SimResult r = simulate(codec, -80.0, 60.0, 44100.0);
      JCLAP_CHECK(std::abs(r.integral * 1e6 + 80.0) < 60.0);
      JCLAP_CHECK(std::abs(r.outputRms - tone) < 0.05);
   }
}

} // namespace

// The test impairment: parsing, and what it does to a 5 ms packet stream over a minute.
void testImpairment() {
   const ImpairSettings s = ImpairSettings::parse("jitter=6,stall=80,every=10,loss=2,bogus=1,jitter2=x");
   JCLAP_CHECK(s.enabled());
   JCLAP_CHECK_EQ(s.jitterMs, 6.0);
   JCLAP_CHECK_EQ(s.stallMs, 80.0);
   JCLAP_CHECK_EQ(s.lossPercent, 2.0);
   JCLAP_CHECK(s.describe() == "jitter 6 ms, 80 ms stalls every 10 s, 2% loss");
   JCLAP_CHECK(!ImpairSettings::parse("").enabled());
   JCLAP_CHECK(!ImpairSettings::parse("loss=-5,jitter=abc").enabled());

   Impairment impairment(s, 7);
   using Clock = Impairment::Clock;
   const Clock::time_point start{};
   Clock::time_point last = start;
   int sent = 0, lost = 0, stalled = 0;
   double delaySum = 0;
   for (int i = 0; i < 12000; ++i) {
      const Clock::time_point now = start + std::chrono::milliseconds(5 * i);
      Clock::time_point release;
      if (!impairment.schedule(now, release)) {
         ++lost;
         continue;
      }
      ++sent;
      JCLAP_CHECK(release >= now && release >= last); // never early, never out of order
      last = release;
      const double delay = std::chrono::duration<double, std::milli>(release - now).count();
      delaySum += delay;
      stalled += delay > 60.0;
   }
   std::printf("impairment: %d sent, %d lost, mean delay %.1f ms, %d held over 60 ms\n", sent, lost,
               delaySum / sent, stalled);
   JCLAP_CHECK(lost > 150 && lost < 350);   // about 2% of 12000
   JCLAP_CHECK(stalled > 20);               // about 6 stalls in a minute, a dozen packets each
   JCLAP_CHECK(delaySum / sent > 5.0 && delaySum / sent < 15.0);
}

int main() {
   testProtocol();
   testCodecs();
   testResampler();
   testRing();
   testImpairment();
   testSimulatedLink();
   return jclap::core::testResult();
}
