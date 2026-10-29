// The engine without a host or a network: packets, codecs, the resampler, and a sender and receiver
// on clocks 150 ppm apart, joined by a simulated Wi-Fi link with jitter, stalls and loss.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <queue>
#include <random>
#include <vector>

#include "codec.h"
#include "impair.h"
#include "params.h"
#include "protocol.h"
#include "receive_ring.h"
#include "receiver.h"
#include "resampler.h"
#include "sender.h"

using namespace jtunnel;

namespace {

constexpr double kPi = 3.14159265358979323846;

void testProtocol() {
   // MIDI only frames have no audio; one that claims some isn't well formed.
   {
      uint8_t packet[kMaxPacketBytes];
      AudioHeader h;
      h.streamId = 7;
      h.seq = 3;
      h.sampleRate = 48000;
      h.codec = Codec::None;
      h.frameSamples = 48;
      Frame f;
      f.midiCount = 1;
      f.midi[0] = {5, 3, {0x90, 60, 100}};
      const Frame *frames[1] = {&f};
      size_t size = writeAudio(packet, sizeof(packet), h, frames, 1);
      AudioHeader back;
      Frame parsed[kMaxFramesPerPacket];
      int count = 0;
      JCLAP_CHECK(size > 0 && parseAudio(packet, size, back, parsed, count));
      JCLAP_CHECK(back.codec == Codec::None && count == 1 && parsed[0].midiCount == 1 && parsed[0].audioBytes == 0);
      f.audioBytes = 4;
      size = writeAudio(packet, sizeof(packet), h, frames, 1);
      JCLAP_CHECK(!parseAudio(packet, size, back, parsed, count));
   }
   Frame a, b;
   a.seq = 11;
   a.audioBytes = 3;
   a.audio[0] = 1;
   a.audio[1] = 2;
   a.audio[2] = 3;
   a.midiCount = 1;
   a.midi[0] = {7, 3, {0x90, 60, 100}};
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

   // The biggest packet (two full PCM frames with as much MIDI as a frame takes) fits one MTU.
   Frame full;
   full.audioBytes = kMaxFrameBytes;
   full.midiCount = 1;
   full.midi[0] = {0, kMaxEventBytes, {kTagSysex, kSysexFirst, 0, kSysexPieceBytes}};
   int bytes = 2 + kMaxEventBytes;
   for (; bytes + 2 + 3 <= kMaxMidiBytesPerFrame; bytes += 2 + 3)
      full.midi[full.midiCount++] = {0, 3, {0xB0, 1, 64}};
   for (; bytes + 2 + 1 <= kMaxMidiBytesPerFrame; bytes += 2 + 1)
      full.midi[full.midiCount++] = {0, 1, {0xF8}};
   JCLAP_CHECK(full.midiCount <= kMaxMidiPerFrame);
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

// Every kind of event goes on the wire and comes back: messages of each length, SysEx pieces and
// note expressions. Anything malformed is refused.
void testMidiEvents() {
   JCLAP_CHECK_EQ(midiMessageSize(0x90), 3);
   JCLAP_CHECK_EQ(midiMessageSize(0xC3), 2);
   JCLAP_CHECK_EQ(midiMessageSize(0xD0), 2);
   JCLAP_CHECK_EQ(midiMessageSize(0xF2), 3);
   JCLAP_CHECK_EQ(midiMessageSize(0xF3), 2);
   JCLAP_CHECK_EQ(midiMessageSize(0xF8), 1);
   JCLAP_CHECK_EQ(midiMessageSize(0xF0), 0);
   JCLAP_CHECK_EQ(midiMessageSize(0xF7), 0);
   JCLAP_CHECK_EQ(midiMessageSize(0x40), 0);

   // The MIDI filter: each kind's switch, and the channel switches for channel messages and
   // expressions. Note offs always go.
   {
      const std::vector<std::pair<std::vector<uint8_t>, clap_id>> kinds = {
         {{kTagSysex}, kParamSysex},           {{0xF8}, kParamClock},
         {{0xF1, 0}, kParamClock},             {{0xFA}, kParamTransport},
         {{0xF2, 0, 0}, kParamTransport},      {{0xFF}, kParamSystem},
         {{0x93, 60, 100}, kParamNotes},       {{0xB2, 1, 64}, kParamCc},
         {{0xE5, 0, 64}, kParamPitchBend},     {{0xD1, 40}, kParamChannelPressure},
         {{0xA0, 60, 40}, kParamPolyPressure}, {{0xC4, 3}, kParamProgram},
         {{kTagExpression, 6, 3, 60}, kParamExpressions},
      };
      for (const auto &[event, param] : kinds)
         JCLAP_CHECK_EQ(midiKindParam(event.data()), param);

      bool params[kParamCount];
      std::fill(std::begin(params), std::end(params), true);
      const auto on = [&](clap_id id) { return params[id]; };
      const uint8_t cc3[] = {0xB2, 1, 64}, cc4[] = {0xB3, 1, 64}, bend3[] = {0xE2, 0, 64}, clock[] = {0xF8},
                    noteOn3[] = {0x92, 60, 100}, noteOff3[] = {0x82, 60, 0}, zeroOn3[] = {0x92, 60, 0},
                    exprCh3[] = {kTagExpression, 6, 2, 60}, exprAll[] = {kTagExpression, 6, 255, 60};
      params[kParamChannel1 + 2] = false; // channel 3
      JCLAP_CHECK(!midiPasses(cc3, on) && midiPasses(cc4, on) && !midiPasses(bend3, on) && midiPasses(clock, on));
      JCLAP_CHECK(!midiPasses(noteOn3, on) && midiPasses(noteOff3, on) && midiPasses(zeroOn3, on));
      JCLAP_CHECK(!midiPasses(exprCh3, on) && midiPasses(exprAll, on));
      params[kParamChannel1 + 2] = true;
      params[kParamCc] = false;
      params[kParamClock] = false;
      JCLAP_CHECK(!midiPasses(cc4, on) && midiPasses(bend3, on) && !midiPasses(clock, on));
      params[kParamNotes] = false;
      JCLAP_CHECK(!midiPasses(noteOn3, on) && midiPasses(noteOff3, on));
   }

   Frame f;
   f.midiCount = 0;
   const auto add = [&](std::initializer_list<uint8_t> bytes) {
      MidiEvent &e = f.midi[f.midiCount++];
      e.offset = static_cast<uint16_t>(f.midiCount);
      e.size = static_cast<uint8_t>(bytes.size());
      std::copy(bytes.begin(), bytes.end(), e.data);
   };
   add({0xB2, 74, 127});                       // CC
   add({0xE0, 0x00, 0x40});                    // pitch bend
   add({0xA1, 60, 33});                        // poly aftertouch
   add({0xD1, 90});                            // channel pressure
   add({0xC0, 5});                             // program change
   add({0xF2, 0x10, 0x02});                    // song position
   add({0xF8});                                // clock
   add({0xFA});                                // start
   add({kTagSysex, kSysexFirst | kSysexLast, 0, 4, 0xF0, 0x7E, 0x01, 0xF7});
   add({kTagExpression, 6, 3, 61, 0, 0, 0, 0, 0, 0, 0xE0, 0x3F}); // pressure 0.5
   uint8_t packet[kMaxPacketBytes];
   AudioHeader h;
   h.sampleRate = 48000;
   h.codec = Codec::None;
   h.frameSamples = 48;
   const Frame *frames[1] = {&f};
   const size_t size = writeAudio(packet, sizeof(packet), h, frames, 1);
   AudioHeader back;
   Frame parsed[kMaxFramesPerPacket];
   int count = 0;
   JCLAP_CHECK(size > 0 && parseAudio(packet, size, back, parsed, count));
   JCLAP_CHECK_EQ(parsed[0].midiCount, f.midiCount);
   for (int i = 0; i < f.midiCount && i < parsed[0].midiCount; ++i) {
      JCLAP_CHECK_EQ(parsed[0].midi[i].offset, f.midi[i].offset);
      JCLAP_CHECK_EQ(parsed[0].midi[i].size, f.midi[i].size);
      JCLAP_CHECK(std::equal(f.midi[i].data, f.midi[i].data + f.midi[i].size, parsed[0].midi[i].data));
   }
   for (size_t cut = 0; cut < size; ++cut)
      JCLAP_CHECK(!parseAudio(packet, cut, back, parsed, count));

   // A data byte with its top bit set, a bare F0, and a SysEx piece that claims more than it has.
   for (const MidiEvent &bad : {MidiEvent{0, 3, {0x90, 0x80, 1}}, MidiEvent{0, 1, {0xF0}},
                                MidiEvent{0, 5, {kTagSysex, kSysexFirst, 0, 9, 0xF0}}}) {
      Frame one;
      one.midiCount = 1;
      one.midi[0] = bad;
      const Frame *ones[1] = {&one};
      JCLAP_CHECK_EQ(writeAudio(packet, sizeof(packet), h, ones, 1), size_t{0});
   }
}

// A sender straight into a receiver's ring, dropping the packets `drop` says to.
struct DirectLink : PacketSink {
   ReceiveRing &ring;
   std::function<bool(int)> drop;
   int packets = 0;
   explicit DirectLink(ReceiveRing &r) : ring(r) {}
   void send(const uint8_t *data, size_t size) override {
      if (drop && drop(packets++))
         return;
      AudioHeader h;
      Frame frames[kMaxFramesPerPacket];
      int count = 0;
      if (parseAudio(data, size, h, frames, count))
         for (int i = 0; i < count; ++i)
            ring.store(h, frames[i]);
   }
};

// Runs a MIDI-only link for `blocks` blocks with `events` going in at block 10 and returns what
// came out, SysEx messages whole.
struct Received {
   std::vector<std::vector<uint8_t>> events, sysex;
};
Received runMidi(const std::vector<TimedMidi> &events, int blocks, bool redundancy,
                 std::function<bool(int)> drop = nullptr) {
   Sender sender;
   Receiver receiver;
   ReceiveRing ring;
   DirectLink link(ring);
   link.drop = std::move(drop);
   sender.setup(48000.0, 7);
   receiver.setup(48000.0);
   receiver.setBuffer(2.0, false);
   sender.configure(Codec::None, 192, redundancy);
   const uint32_t block = 64;
   float in[block] = {}, outL[block], outR[block];
   OutMidi midi[256];
   Received got;
   for (int b = 0; b < blocks; ++b) {
      if (b == 10)
         sender.process(in, in, block, events.data(), static_cast<int>(events.size()), link);
      else
         sender.process(in, in, block, nullptr, 0, link);
      const int n = receiver.process(ring, outL, outR, block, midi, 256);
      for (int i = 0; i < n; ++i) {
         if (midi[i].sysex)
            got.sysex.emplace_back(midi[i].sysex, midi[i].sysex + midi[i].sysexSize);
         else
            got.events.emplace_back(midi[i].data, midi[i].data + midi[i].size);
      }
   }
   return got;
}

// Every kind of MIDI through a sender and receiver: in order, and SysEx far longer than a frame
// takes comes out whole. A SysEx message missing a piece never comes out.
void testAllMidi() {
   std::vector<uint8_t> dump(1000);
   dump.front() = 0xF0;
   for (size_t i = 1; i + 1 < dump.size(); ++i)
      dump[i] = static_cast<uint8_t>(i % 128);
   dump.back() = 0xF7;
   std::vector<TimedMidi> events;
   const auto add = [&](uint32_t time, std::initializer_list<uint8_t> bytes) {
      TimedMidi m{time, static_cast<uint8_t>(bytes.size()), {}};
      std::copy(bytes.begin(), bytes.end(), m.data);
      events.push_back(m);
   };
   add(0, {0xB0, 7, 100});
   add(1, {0xE3, 0x7F, 0x7F});
   add(2, {0xA0, 60, 20});
   add(3, {0xD0, 64});
   add(4, {0xC1, 12});
   add(5, {0xF8});
   add(5, {0xFA});
   add(6, {0xF2, 1, 0});
   add(7, {kTagExpression, 2, 255, 255, 0, 0, 0, 0, 0, 0, 0xF0, 0x3F});
   const int sysexAt = static_cast<int>(events.size());
   for (int i = 0; i * kSysexPieceBytes < static_cast<int>(dump.size()); ++i) {
      const int at = i * kSysexPieceBytes;
      const int n = std::min(kSysexPieceBytes, static_cast<int>(dump.size()) - at);
      const bool last = at + n == static_cast<int>(dump.size());
      TimedMidi m{8, static_cast<uint8_t>(4 + n), {kTagSysex, static_cast<uint8_t>((i == 0 ? kSysexFirst : 0) | (last ? kSysexLast : 0)),
                                                   static_cast<uint8_t>(i), static_cast<uint8_t>(n)}};
      std::copy(dump.begin() + at, dump.begin() + at + n, m.data + 4);
      events.push_back(m);
   }
   add(9, {0x90, 60, 100});

   const Received got = runMidi(events, 200, true);
   JCLAP_CHECK_EQ(got.events.size(), static_cast<size_t>(sysexAt + 1));
   for (int i = 0; i < sysexAt && i < static_cast<int>(got.events.size()); ++i)
      JCLAP_CHECK(got.events[i] == std::vector<uint8_t>(events[i].data, events[i].data + events[i].size));
   JCLAP_CHECK(!got.events.empty() && got.events.back() == std::vector<uint8_t>({0x90, 60, 100}));
   JCLAP_CHECK_EQ(got.sysex.size(), size_t{1});
   JCLAP_CHECK(!got.sysex.empty() && got.sysex[0] == dump);

   // Without redundancy, one lost packet in the middle of the SysEx loses it, and nothing else.
   const Received lossy = runMidi(events, 200, false, [](int packet) { return packet == 13; });
   JCLAP_CHECK(lossy.sysex.empty());
   JCLAP_CHECK(!lossy.events.empty() && lossy.events.back() == std::vector<uint8_t>({0x90, 60, 100}));
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
               ring.store(h, frames[i], static_cast<int64_t>(a.time * 1e6));
         inFlight.pop();
      }
   }
};

struct SimResult {
   double integral = 0.0;
   double driftAt20 = 0.0;     // the measurement 20 s in
   bool shownAt10 = true;      // whether the panel's figure was out 10 s in
   double targetAtEnd = 0.0;   // after the sender went quiet
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
SimResult simulate(Codec codec, double ppm, double seconds, double senderRate = 48000.0, bool stalls = true,
                   double bufferMs = 10.0) {
   Sender sender;
   Receiver receiver;
   ReceiveRing ring;
   Network net;
   net.stalls = stalls;
   sender.setup(senderRate, 42);
   receiver.setup(48000.0);
   receiver.setBuffer(bufferMs, true);
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
            events[count++] = {17, 3, {0x90, 60, 100}};
            noteOn1 = true;
         }
         if (!noteOff1 && blockStart >= 10.5) {
            events[count++] = {33, 3, {0x80, 60, 0}};
            noteOff1 = true;
         }
         if (!noteOn2 && blockStart >= 70.0) {
            events[count++] = {0, 3, {0x91, 64, 90}};
            noteOn2 = true;
         }
         sender.process(inL, inR, block, events, count, net);
         sentFrames += block;
      }
      net.deliver(ring, t);
      while (static_cast<double>(playedFrames + block) <= t * 48000.0) {
         const int n = receiver.process(ring, outL, outR, block, midi, 64,
                                        static_cast<int64_t>(t * 1e6));
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
         if (t > 10.0 && t < 10.01)
            result.shownAt10 = !std::isnan(receiver.stats().driftPpm.load());
         if (t > 20.0 && t < 20.01)
            result.driftAt20 = receiver.drift();
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
   result.targetAtEnd = receiver.stats().targetMs.load();
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
               codec == Codec::Opus ? "opus" : codec == Codec::Pcm24 ? "pcm" : "midi only", ppm, result.integral * 1e6, result.fillMs, result.targetMs,
               result.lost, result.concealed, result.concealedLate, result.outputRms, result.longestSilence, net.sent, net.dropped);
   return result;
}

void testSimulatedLink() {
   const double tone = 0.5 / std::sqrt(2.0);
   for (Codec codec : {Codec::Opus, Codec::Pcm24}) {
      const SimResult r = simulate(codec, 150.0, 90.0);
      // The receiver found the sender's clock offset, close already 20 s in (the panel holds the
      // figure back until then)...
      JCLAP_CHECK(std::abs(r.integral * 1e6 - 150.0) < 25.0);
      JCLAP_CHECK(std::abs(r.driftAt20 * 1e6 - 150.0) < 50.0);
      JCLAP_CHECK(!r.shownAt10);
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
      // A sender going away isn't a network stall: Auto Buffer didn't grow for it.
      JCLAP_CHECK(r.targetAtEnd <= r.targetMs + 1.0);
   }
   // Jitter but no stalls: the buffer stays small.
   {
      const SimResult r = simulate(Codec::Opus, 50.0, 60.0, 48000.0, false);
      JCLAP_CHECK(r.targetMs <= 30.0);
      JCLAP_CHECK(std::abs(r.integral * 1e6 - 50.0) < 20.0);
   }
   // A sender at 44.1 kHz into a 48 kHz receiver, with Opus (resampled at the sender) and PCM
   // (resampled at the receiver), and a slow clock.
   for (Codec codec : {Codec::Opus, Codec::Pcm24}) {
      const SimResult r = simulate(codec, -80.0, 60.0, 44100.0);
      JCLAP_CHECK(std::abs(r.integral * 1e6 + 80.0) < 25.0);
      JCLAP_CHECK(std::abs(r.outputRms - tone) < 0.05);
   }
   // MIDI only: no audio, 1 ms frames and a 2 ms buffer, so notes arrive within a few ms of the
   // network's own delay (2 ms plus a 3 ms tail here).
   {
      const SimResult r = simulate(Codec::None, 50.0, 30.0, 48000.0, false, 2.0);
      double on = -1, off = -1;
      for (const auto &event : r.midi) {
         if (event.second.data[0] == 0x90 && event.second.data[1] == 60)
            on = event.first;
         if (event.second.data[0] == 0x80 && event.second.data[1] == 60)
            off = event.first;
      }
      std::printf("midi only: note on after %.1f ms, target %.1f ms\n", (on - 10.0) * 1000.0, r.targetMs);
      JCLAP_CHECK(on > 10.0 && on < 10.03);
      JCLAP_CHECK(std::abs((off - on) - 0.5) < 0.005);
      JCLAP_CHECK(r.outputRms < 1e-6);
      JCLAP_CHECK_EQ(r.notesHeldAtEnd, 0);
   }
}

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

} // namespace

int main() {
   testProtocol();
   testMidiEvents();
   testAllMidi();
   testCodecs();
   testResampler();
   testRing();
   testImpairment();
   testSimulatedLink();
   return jclap::core::testResult();
}
