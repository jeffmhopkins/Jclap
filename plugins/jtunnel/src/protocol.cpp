#include "protocol.h"

#include <cstring>

namespace jtunnel {
namespace {

constexpr uint8_t kMagic[4] = {'J', 'T', 'U', 'N'};
constexpr size_t kHeaderBytes = 6;

class Writer {
public:
   Writer(uint8_t *out, size_t capacity) : out_(out), capacity_(capacity) {}
   void u8(uint8_t v) { bytes(&v, 1); }
   void u16(uint16_t v) {
      const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
      bytes(b, 2);
   }
   void u32(uint32_t v) {
      const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                            static_cast<uint8_t>(v >> 24)};
      bytes(b, 4);
   }
   void bytes(const void *data, size_t n) {
      if (size_ + n > capacity_) {
         failed_ = true;
         return;
      }
      std::memcpy(out_ + size_, data, n);
      size_ += n;
   }
   size_t finish() const { return failed_ ? 0 : size_; }

private:
   uint8_t *out_;
   size_t capacity_, size_ = 0;
   bool failed_ = false;
};

class Reader {
public:
   Reader(const uint8_t *data, size_t size) : data_(data), size_(size) {}
   uint8_t u8() {
      uint8_t v = 0;
      bytes(&v, 1);
      return v;
   }
   uint16_t u16() {
      uint8_t b[2] = {};
      bytes(b, 2);
      return static_cast<uint16_t>(b[0] | b[1] << 8);
   }
   uint32_t u32() {
      uint8_t b[4] = {};
      bytes(b, 4);
      return static_cast<uint32_t>(b[0]) | static_cast<uint32_t>(b[1]) << 8 | static_cast<uint32_t>(b[2]) << 16 |
             static_cast<uint32_t>(b[3]) << 24;
   }
   void bytes(void *out, size_t n) {
      if (pos_ + n > size_) {
         failed_ = true;
         pos_ = size_;
         return;
      }
      std::memcpy(out, data_ + pos_, n);
      pos_ += n;
   }
   bool ok() const { return !failed_; }

private:
   const uint8_t *data_;
   size_t size_, pos_ = 0;
   bool failed_ = false;
};

void writeHeader(Writer &w, PacketType type) {
   w.bytes(kMagic, 4);
   w.u8(kProtocolVersion);
   w.u8(static_cast<uint8_t>(type));
}

} // namespace

uint8_t packetType(const uint8_t *data, size_t size) {
   if (size < kHeaderBytes || std::memcmp(data, kMagic, 4) || data[4] != kProtocolVersion)
      return 0;
   return data[5];
}

size_t writeAudio(uint8_t *out, size_t capacity, const AudioHeader &header, const Frame *const *frames, int count) {
   if (count < 1 || count > kMaxFramesPerPacket)
      return 0;
   Writer w(out, capacity);
   writeHeader(w, PacketType::Audio);
   w.u32(header.streamId);
   w.u32(header.seq);
   w.u32(header.sampleRate);
   w.u8(static_cast<uint8_t>(header.codec));
   w.u8(header.channels);
   w.u16(header.frameSamples);
   w.u8(static_cast<uint8_t>(count));
   for (int i = 0; i < count; ++i) {
      const Frame &f = *frames[i];
      const int midi = f.midiCount < kMaxMidiPerFrame ? f.midiCount : kMaxMidiPerFrame;
      w.u8(static_cast<uint8_t>(midi));
      for (int m = 0; m < midi; ++m) {
         w.u16(f.midi[m].offset);
         w.bytes(f.midi[m].data, 3);
      }
      if (f.audioBytes > kMaxFrameBytes)
         return 0;
      w.u16(f.audioBytes);
      w.bytes(f.audio, f.audioBytes);
   }
   return w.finish();
}

bool parseAudio(const uint8_t *data, size_t size, AudioHeader &header, Frame *frames, int &count) {
   if (packetType(data, size) != static_cast<uint8_t>(PacketType::Audio))
      return false;
   Reader r(data + kHeaderBytes, size - kHeaderBytes);
   header.streamId = r.u32();
   header.seq = r.u32();
   header.sampleRate = r.u32();
   const uint8_t codec = r.u8();
   header.channels = r.u8();
   header.frameSamples = r.u16();
   count = r.u8();
   if (!r.ok() || codec > static_cast<uint8_t>(Codec::Pcm24) || header.channels != kChannels ||
       header.frameSamples == 0 || header.frameSamples > kMaxFrameSamples || header.sampleRate < 8000 ||
       header.sampleRate > 384000 || count < 1 || count > kMaxFramesPerPacket)
      return false;
   header.codec = static_cast<Codec>(codec);
   if (header.codec == Codec::Opus && (header.sampleRate != kOpusSampleRate || header.frameSamples != kOpusFrameSamples))
      return false;
   if (header.codec == Codec::Pcm24 && header.frameSamples > kPcmFrameSamples)
      return false;
   for (int i = 0; i < count; ++i) {
      Frame &f = frames[i];
      f.seq = header.seq - static_cast<uint32_t>(i);
      f.midiCount = r.u8();
      if (f.midiCount > kMaxMidiPerFrame)
         return false;
      for (int m = 0; m < f.midiCount; ++m) {
         f.midi[m].offset = r.u16();
         r.bytes(f.midi[m].data, 3);
         if (f.midi[m].offset >= header.frameSamples)
            return false;
      }
      f.audioBytes = r.u16();
      if (f.audioBytes > kMaxFrameBytes)
         return false;
      if (header.codec == Codec::Pcm24 && f.audioBytes != header.frameSamples * kChannels * 3)
         return false;
      r.bytes(f.audio, f.audioBytes);
      if (!r.ok())
         return false;
   }
   return r.ok();
}

size_t writeSubscribe(uint8_t *out, size_t capacity, const char *tunnelName) {
   Writer w(out, capacity);
   writeHeader(w, PacketType::Subscribe);
   size_t n = std::strlen(tunnelName);
   if (n > kMaxTunnelName)
      n = kMaxTunnelName;
   w.u8(static_cast<uint8_t>(n));
   w.bytes(tunnelName, n);
   return w.finish();
}

bool parseSubscribe(const uint8_t *data, size_t size, char *name, size_t nameCapacity) {
   if (packetType(data, size) != static_cast<uint8_t>(PacketType::Subscribe) || nameCapacity == 0)
      return false;
   Reader r(data + kHeaderBytes, size - kHeaderBytes);
   const size_t n = r.u8();
   if (!r.ok() || n > kMaxTunnelName || n + 1 > nameCapacity)
      return false;
   r.bytes(name, n);
   name[r.ok() ? n : 0] = 0;
   return r.ok();
}

size_t writeBye(uint8_t *out, size_t capacity) {
   Writer w(out, capacity);
   writeHeader(w, PacketType::Bye);
   return w.finish();
}

} // namespace jtunnel
