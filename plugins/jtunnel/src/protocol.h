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
//             midi count (1), midi count x [offset (2), event], audio bytes (2), audio
//    event    a MIDI message: its status byte and its data bytes (one to three bytes in all, by status)
//             a SysEx piece: tag 1, flags (1: first, 2: last), index (1), size (1), that many bytes
//             a note expression: tag 2, expression id (1), channel (1), key (1), value (8, a double),
//             with 255 for a channel or key of -1
//    subscribe  name length (1), name (no terminator)
//    bye      nothing
//    state    stream id (4), message id (4), piece (1), piece count (1), key length (1), key,
//             data length (2), data
//    names    count (1), count x [slot (1), name length (1), name]
//
// State packets are the side channel: what the DAW is doing (its clip grid, transport, selected
// device...), as JSON messages under keys like "transport" or "track/3", sent beside the audio and
// MIDI (see state.h). A message too long for one packet goes in pieces; a receiver keeps the
// newest whole message for each key. Receivers that don't know the type ignore it.
//
// Names go the other way, from a receiver to the sender it subscribes to: what the receiver calls
// each of the sender's modulation params (params.h), such as "Camera 1 Pan" for the one it binds to
// a camera's pan. The list is whole each time: a slot it leaves out has no name. A receiver sends it
// beside its subscription; a sender that doesn't know the type ignores it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace jtunnel {

enum class PacketType : uint8_t { Audio = 1, Subscribe = 2, Bye = 3, State = 4, Names = 5 };
// None carries no audio: MIDI only, in short frames at the sender's rate, so it arrives sooner.
enum class Codec : uint8_t { Opus = 0, Pcm24 = 1, None = 2 };

constexpr uint8_t kProtocolVersion = 2;
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
// What a frame's events may take, so that two frames of the largest audio still fit one packet.
constexpr int kMaxMidiBytesPerFrame = 145;
constexpr int kMaxFramesPerPacket = 2;
// Under the 1472 bytes of UDP payload a 1500-byte MTU leaves, so packets are never fragmented.
constexpr size_t kMaxPacketBytes = 1472;
constexpr size_t kMaxTunnelName = 31;

// An event as it goes on the wire (see above): a MIDI message, a piece of a SysEx message, or a
// note expression. A SysEx message goes in pieces, numbered from 0 so a receiver can tell when one
// went missing.
constexpr uint8_t kTagSysex = 1, kTagExpression = 2;
constexpr uint8_t kSysexFirst = 1, kSysexLast = 2;
constexpr int kMaxEventBytes = 64;
constexpr int kSysexPieceBytes = kMaxEventBytes - 4;
// The longest SysEx message carried (256 pieces); longer ones are dropped whole.
constexpr int kMaxSysexBytes = 256 * kSysexPieceBytes;
constexpr int kExpressionBytes = 12;

struct MidiEvent {
   uint16_t offset; // samples into the frame
   uint8_t size;    // of data
   uint8_t data[kMaxEventBytes];
};

// The length of a MIDI message from its status byte (0 for SysEx's F0 and F7, which have none).
int midiMessageSize(uint8_t status);
// The length of the event at `data`, or 0 if it isn't a well-formed one in `available` bytes.
int eventSize(const uint8_t *data, size_t available);

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

// Names for the modulation params: kModSlots of them, each up to kMaxModName bytes of UTF-8 with no
// control characters, so all of them fit one packet.
constexpr int kModSlots = 32;
constexpr size_t kMaxModName = 40;
// True if `name` may be a slot's name: empty (no name), or valid UTF-8 of at most kMaxModName bytes
// with no control characters and no space at either end.
bool isValidModName(const char *name, size_t size);
// The names in `names[0..kModSlots)`, leaving out the empty ones. 0 if any is invalid.
size_t writeNames(uint8_t *out, size_t capacity, const std::string *names);
// Fills names[0..kModSlots), empty for each slot the packet leaves out. False for anything that
// isn't a well-formed names packet: a slot out of range or named twice, or an invalid name.
bool parseNames(const uint8_t *data, size_t size, std::string *names);

// The side channel's limits: a key such as "track/12", and a message's data, in up to 64 pieces.
constexpr size_t kMaxStateKey = 63;
constexpr size_t kStatePieceBytes = 1200;
constexpr int kMaxStatePieces = 64;
constexpr size_t kMaxStateBytes = kStatePieceBytes * kMaxStatePieces;

struct StatePiece {
   uint32_t streamId = 0;  // the sender's state stream: a new one means start again
   uint32_t messageId = 0; // the same for every piece of one message, new for every new message
   uint8_t piece = 0, pieces = 1;
   const char *key = nullptr; // not terminated
   uint8_t keySize = 0;
   const uint8_t *data = nullptr;
   uint16_t dataSize = 0;
};

// One piece of a state message. Returns the packet's size, or 0 if it doesn't fit or is malformed.
size_t writeState(uint8_t *out, size_t capacity, const StatePiece &piece);
// Points piece's key and data into `data`. False for anything that isn't a well-formed state packet.
bool parseState(const uint8_t *data, size_t size, StatePiece &piece);

// The type of a packet with a valid header, or 0.
uint8_t packetType(const uint8_t *data, size_t size);

// a - b for sequence numbers that wrap: negative when a is before b.
inline int32_t seqDiff(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b); }

} // namespace jtunnel
