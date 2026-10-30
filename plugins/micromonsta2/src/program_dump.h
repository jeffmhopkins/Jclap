#pragma once

// The MicroMonsta 2's program dump, the same decoding as tools/mm2_bank.py, which documents the
// format. The synth sends one when asked for a part's sound (kSoundRequest, firmware 2.7 and later)
// and one per program for UTILITY U02. The plugin gets them through the Bitwig script (synth_link.h).
//
// Byte 6 says what the message is for: 01 / 02 the sound on part A / B, 03 a stored program. Byte 7
// varies too, so neither is checked. (From the Monstaguru librarian, which sends and checks the same.)

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "patches.h"

namespace mm2 {

inline constexpr int kProgramDumpSize = 176;
inline constexpr uint8_t kProgramDumpHeader[] = {0xF0, 0x00, 0x21, 0x22, 0x4D, 0x02};
// F0 00 21 22 4D 02 41 F7 asks for part A's sound, 42 for part B's; the reply is one program.
inline constexpr uint8_t kSoundRequest[] = {0xF0, 0x00, 0x21, 0x22, 0x4D, 0x02, 0x41, 0xF7};

namespace dump {

enum class Kind { Raw, Tune, Fine, Wide, Amount };

struct Field {
   int cc;
   int offset; // the high byte's, for the two-byte kinds
   Kind kind;
};

// LAYOUT in tools/mm2_bank.py, with the mod amounts (CC 52-61) at 85 + 4 * slot.
inline const std::vector<Field> &layout() {
   static const std::vector<Field> fields = [] {
      std::vector<Field> f = {
         {5, 30, Kind::Raw},     {33, 27, Kind::Raw},     {34, 28, Kind::Raw},     {35, 29, Kind::Raw},
         {36, 33, Kind::Raw},    {37, 34, Kind::Raw},     {39, 36, Kind::Raw},     {40, 37, Kind::Tune},
         {41, 38, Kind::Fine},   {42, 40, Kind::Raw},     {43, 41, Kind::Tune},    {44, 42, Kind::Fine},
         {45, 44, Kind::Raw},    {46, 45, Kind::Tune},    {47, 46, Kind::Fine},    {48, 47, Kind::Raw},
         {49, 48, Kind::Raw},    {50, 49, Kind::Raw},     {51, 50, Kind::Raw},     {74, 51, Kind::Wide},
         {71, 53, Kind::Raw},    {88, 54, Kind::Amount},  {89, 56, Kind::Raw},     {90, 57, Kind::Raw},
         {91, 58, Kind::Raw},    {73, 59, Kind::Raw},     {75, 60, Kind::Raw},     {79, 61, Kind::Raw},
         {72, 62, Kind::Raw},    {80, 63, Kind::Raw},     {81, 64, Kind::Raw},     {82, 65, Kind::Raw},
         {83, 66, Kind::Raw},    {84, 67, Kind::Raw},     {85, 68, Kind::Raw},     {86, 69, Kind::Raw},
         {87, 70, Kind::Raw},    {76, 73, Kind::Raw},     {77, 77, Kind::Raw},     {78, 81, Kind::Raw},
         {62, 124, Kind::Raw},   {25, 130, Kind::Amount}, {26, 133, Kind::Raw},    {27, 134, Kind::Raw},
         {28, 135, Kind::Raw},   {29, 136, Kind::Raw},    {30, 137, Kind::Raw},    {31, 138, Kind::Raw},
      };
      for (int slot = 0; slot < kModSlots; ++slot)
         f.push_back({52 + slot, 85 + 4 * slot, Kind::Amount});
      return f;
   }();
   return fields;
}

// What the synth stores for CC value v.
inline int encode(Kind k, int v) {
   switch (k) {
   case Kind::Raw: return v;
   case Kind::Tune: return 40 + v * 36 / 127;
   case Kind::Fine: return 39 + v * 50 / 127;
   case Kind::Wide: return v * 255 / 127;
   case Kind::Amount: return 29 + v * 198 / 127;
   }
   return v;
}

inline int stored(const uint8_t *p, const Field &f) {
   const bool wide = f.kind == Kind::Wide || f.kind == Kind::Amount;
   return wide ? p[f.offset] * 128 + p[f.offset + 1] : p[f.offset];
}

// The middle of the CC values the synth stores as this value (or the nearest one), as mm2_bank.py.
inline int ccValue(const uint8_t *p, const Field &f) {
   const int s = stored(p, f);
   int closest = 1 << 30;
   for (int v = 0; v < 128; ++v)
      closest = std::min(closest, std::abs(encode(f.kind, v) - s));
   std::vector<int> same;
   for (int v = 0; v < 128; ++v)
      if (std::abs(encode(f.kind, v) - s) == closest)
         same.push_back(v);
   return same[same.size() / 2];
}

inline const char *const kCategories[] = {"Bass", "Lead", "Pad",  "Keys", "Organ", "Strings", "Brass",  "Percussion",
                                          "Drone", "Noise", "SFX", "Arp",  "Misc",  "User 1",  "User 2", "User 3"};

} // namespace dump

inline bool isProgramDump(const uint8_t *bytes, size_t size) {
   if (size != kProgramDumpSize || bytes[size - 1] != 0xF7)
      return false;
   for (size_t i = 0; i < sizeof(kProgramDumpHeader); ++i)
      if (bytes[i] != kProgramDumpHeader[i])
         return false;
   return true;
}

// One program message into `out`, numbered `program` (1-300). False if it isn't one.
inline bool decodeProgram(const uint8_t *bytes, size_t size, int program, HardwarePatch &out) {
   if (!isProgramDump(bytes, size))
      return false;
   out = HardwarePatch{};
   out.program = program;
   std::string name(reinterpret_cast<const char *>(bytes + 8), 8);
   for (char &c : name)
      if (c < ' ' || c > '~')
         c = '?';
   while (!name.empty() && name.back() == ' ')
      name.pop_back();
   out.name = name;
   out.category = bytes[16] < 16 ? dump::kCategories[bytes[16]] : "Misc";
   for (const dump::Field &f : dump::layout())
      out.values.emplace_back(f.cc, dump::ccValue(bytes, f));
   std::sort(out.values.begin(), out.values.end());
   for (int k = 0; k < kModSlots; ++k)
      out.routes[k] = ModRoute{bytes[87 + 4 * k], bytes[84 + 4 * k]};
   return true;
}

// A U02 dump (one message per slot, P001 first) as the bank: every named program in P001-P300,
// skipping "INIT", as mm2_bank.py builds factory_bank.inc.
inline std::vector<HardwarePatch> decodeBank(const std::vector<std::vector<uint8_t>> &messages) {
   std::vector<HardwarePatch> bank;
   for (size_t i = 0; i < messages.size() && i < static_cast<size_t>(kProgramCount); ++i) {
      HardwarePatch p;
      if (decodeProgram(messages[i].data(), messages[i].size(), static_cast<int>(i) + 1, p) && p.name != "INIT")
         bank.push_back(std::move(p));
   }
   return bank;
}

} // namespace mm2
