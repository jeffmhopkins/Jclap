#include <cstdio>
#include <set>

#include "cc_map.h"

using namespace mm2;

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
   // Every sound CC named in manual v2.8 section 7.1, minus bank select, sustain and channel mode.
   const std::set<int> manual = {1,  5,  7,  25, 26, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 39,
                                 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
                                 56, 57, 58, 59, 60, 61, 62, 70, 71, 72, 73, 74, 75, 76, 77, 78,
                                 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91};
   std::set<int> seen;
   for (const CcParam &p : kCcParams) {
      CHECK_EQ(manual.count(p.cc), 1u);
      CHECK_EQ(seen.insert(p.cc).second, true);
   }
   CHECK_EQ(kCcParamCount, 61);
   CHECK_EQ(seen.size(), manual.size());
   CHECK_EQ(seen.count(static_cast<int>(kChannelParamId)), 0u);

   CHECK_EQ(indexOfCc(39), 0);
   CHECK_EQ(indexOfCc(2), -1);
   CHECK_EQ(toCcValue(-3.0), 0);
   CHECK_EQ(toCcValue(126.6), 127);
   CHECK_EQ(toCcValue(200.0), 127);

   CcSender sender;
   // Untouched parameters are never sent.
   CHECK_EQ(sender.update(0, 64.0, false), -1);
   // First touch sends, repeats of the same CC value don't.
   CHECK_EQ(sender.update(0, 64.0, true), 64);
   CHECK_EQ(sender.update(0, 64.2, true), -1);
   CHECK_EQ(sender.update(0, 64.6, true), 65);
   // After a resend request the current value goes out again.
   sender.resendAll();
   CHECK_EQ(sender.update(0, 64.6, true), 65);
   CHECK_EQ(sender.update(1, 0.0, true), 0);

   if (failures)
      std::printf("%d failure(s)\n", failures);
   return failures ? 1 : 0;
}
