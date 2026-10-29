#include <cmath>
#include <cstdio>
#include <set>

#include "cc_map.h"

using namespace mb;

static int failures = 0;

#define CHECK_EQ(a, b)                                                                             \
   do {                                                                                            \
      const auto va = (a);                                                                         \
      const auto vb = (b);                                                                         \
      if (va != vb) {                                                                              \
         std::printf("%s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #a, (int)va, (int)vb); \
         ++failures;                                                                               \
      }                                                                                            \
   } while (0)

int main() {
   // Every sound CC in manual v2.0.3 section 7.1, minus the sustain pedal (64). The 7/14-bit ones are
   // all below 32, so their fine bytes (cc + 32) never land on another parameter.
   const std::set<int> manual = {1,  2,  3,  4,  5,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,  18,  19,  20, 21,
                                 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 65, 66, 67, 68, 69, 70,  71,  72,  73, 74,
                                 75, 76, 77, 78, 79, 80, 81, 82, 83, 85, 86, 87, 89, 90, 91, 92,  93,  94,  95, 102,
                                 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119};
   std::set<int> seen;
   for (const CcParam &p : kCcParams) {
      CHECK_EQ(manual.count(p.cc), 1u);
      CHECK_EQ(seen.insert(p.cc).second, true);
      CHECK_EQ(p.fine, p.cc < 32);
      if (p.fine)
         CHECK_EQ(indexOfCc(p.cc + kFineOffset), -1);
   }
   CHECK_EQ(kCcParamCount, 77);
   CHECK_EQ(seen.size(), manual.size());
   CHECK_EQ(seen.count(static_cast<int>(kChannelParamId)), 0u);

   CHECK_EQ(indexOfCc(9), 0);
   CHECK_EQ(indexOfCc(64), -1);
   CHECK_EQ(indexOfFineCc(55), indexOfCc(23));
   CHECK_EQ(indexOfFineCc(97), -1); // 65 has no fine byte
   CHECK_EQ(indexOfFineCc(23), -1);
   CHECK_EQ(toCcValue(-3.0), 0);
   CHECK_EQ(toCcValue(126.6), 127);
   CHECK_EQ(toCcValue(std::nan("")), 0);

   // 14 bits: the coarse byte is the whole part, the fine byte the fraction in 128ths.
   CHECK_EQ(toFineValue(100.5), 100 * 128 + 64);
   CHECK_EQ(toFineValue(127.0), 127 * 128);
   CHECK_EQ(toFineValue(200.0), 127 * 128);
   CHECK_EQ(toFineValue(-1.0), 0);
   CHECK_EQ(fromWire(100 * 128 + 64), 100.5);
   CHECK_EQ(fromWire(127 * 128 + 127), 127.0);
   CHECK_EQ(toWire(100.4, false), 100 << 7);
   CHECK_EQ(toWire(100.4, true), toFineValue(100.4));

   // A coarse byte alone is the value with a fine byte of 0; a fine byte refines the last coarse one.
   CcReceiver rx;
   CHECK_EQ(rx.coarse(3, 90), 90 << 7);
   CHECK_EQ(rx.fine(3, 64), 90 << 7 | 64);
   CHECK_EQ(rx.coarse(3, 91), 91 << 7);

   // The sender only sends touched knobs, once, and stays quiet over a value the synth heard.
   CcSender tx;
   CHECK_EQ(tx.pending(0, 5 << 7, false), -1);
   CHECK_EQ(tx.pending(0, 5 << 7, true), 5 << 7);
   tx.sent(0, 5 << 7);
   CHECK_EQ(tx.pending(0, 5 << 7, true), -1);
   tx.heard(0, 9 << 7);
   CHECK_EQ(tx.pending(0, 5 << 7, true), -1);
   CHECK_EQ(tx.pending(0, 6 << 7, true), 6 << 7);

   if (failures)
      std::printf("%d failure(s)\n", failures);
   return failures ? 1 : 0;
}
