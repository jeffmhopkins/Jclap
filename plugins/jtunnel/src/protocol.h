// JTunnel's wire format: small UDP packets, little-endian.
//
// Audio packets carry one stream's frames, each a few ms of stereo audio plus the MIDI that falls
// in it (or, for MIDI only, a millisecond of no audio). With redundancy on, each packet also repeats the previous frame, so one lost packet costs
// nothing on Wi-Fi. A receiver asks for a tunnel with Subscribe packets (sent every half second as
// a keepalive) and the sender unicasts to everyone subscribed; Bye leaves at once.
//
//    header   magic "JTUN" (4), version (1), type (1)
//    audio    stream id (4), seq (4), sample rate (4), codec (1: Opus, PCM or none), channels (1), frame samples (2),
//             frame count (1), then per frame (frame i is seq - i):
//             midi count (1), midi count x [offset (2), status, data1, data2], audio bytes (2), audio
//    subscribe  name length (1), name (no terminator)
//    bye      nothing
#pragma once

#include <cstddef>
#include <cstdint>

namespace jtunnel {

enum class PacketType : uint8_t { Audio = 1, Subscribe = 2, Bye = 3 };
// None carries no audio: MIDI only, in short frames at the sender's rate, so it arrives sooner.
enum class Codec : uint8_t { Opus = 0, Pcm24 = 1, None = 2 };

constexpr uint8_t kProtocolVersion = 1;
constexpr int kChannels = 2;
// 5 ms at 48 kHz: short enough for low latency, long enough that Opus codes it well.
constexpr int kOpusFrameSamples = 240;
constexpr int kOpusSampleRate = 48000;
// 2 ms at 48 kHz. Two 24-bit stereo frames still fit one Ethernet MTU with room for MIDI.
constexpr int kPcmFrameSamples = 96;
constexpr int kMaxFrameSamples = 240;
// About 1 ms at the host's rate (48 at 48 kHz), for MIDI only.
constexpr int kMidiFrameMs = 1;
constexpr int kMaxFrameBytes = kPcmFrameSamples * kChannels * 3;
constexpr int kMaxMidiPerFrame = 24;
constexpr int kMaxFramesPerPacket = 2;
// Under the 1472 bytes of UDP payload a 1500-byte MTU leaves, so packets are never fragmented.
constexpr size_t kMaxPacketBytes = 1472;
constexpr size_t kMaxTunnelName = 31;

struct MidiEvent {
   uint16_t offset; // samples into the frame
   uint8_t data[3];
};

struct Frame {
   uint32_t seq = 0;
   uint16_t audioBytes = 0;
   uint8_t midiCount = 0;
   MidiEvent midi[kMaxMidiPerFrame];
   uint8_t audio[kMaxFrameBytes];
};

struct AudioHeader {
   uint32_t streamId = 0;
   uint32_t seq = 0; // the first frame's
   uint32_t sampleRate = 0;
   Codec codec = Codec::Opus;
   uint8_t channels = kChannels;
   uint16_t frameSamples = 0;
};

// The frames in time order of how a receiver should prefer them: frames[0] has header.seq,
// frames[1] (if any) header.seq - 1. Returns the packet's size, or 0 if it wouldn't fit.
size_t writeAudio(uint8_t *out, size_t capacity, const AudioHeader &header, const Frame *const *frames, int count);
// Fills header and up to kMaxFramesPerPacket frames (with their seq set). False for anything that
// isn't a well-formed audio packet of this version.
bool parseAudio(const uint8_t *data, size_t size, AudioHeader &header, Frame *frames, int &count);

size_t writeSubscribe(uint8_t *out, size_t capacity, const char *tunnelName);
// name gets the tunnel asked for, null-terminated (empty: whatever is at this address).
bool parseSubscribe(const uint8_t *data, size_t size, char *name, size_t nameCapacity);

size_t writeBye(uint8_t *out, size_t capacity);

// The type of a packet with a valid header, or 0.
uint8_t packetType(const uint8_t *data, size_t size);

// a - b for sequence numbers that wrap: negative when a is before b.
inline int32_t seqDiff(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b); }

} // namespace jtunnel
