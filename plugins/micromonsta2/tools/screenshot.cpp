// Renders the panel to a PNG without a host: micromonsta2-screenshot out.png [width height [page]]

#include <cstdio>
#include <cstdlib>

#include "cc_map.h"
#include "editor.h"
#include "settings.h"

namespace {

// Shows a program from the bank, with some host modulation so every arc style shows up.
class DemoHost : public mm2::EditorHost {
public:
   static const mm2::HardwarePatch &patch() { return *mm2::findFactoryPatch("P013 OctaBass"); }

   // The program's values, and a spread for the header knobs it doesn't set.
   double paramValue(clap_id id) const override {
      if (const int k = mm2::indexOfSettingId(id); k >= 0) {
         const int nrpn = mm2::kSettings[k].nrpn;
         for (int s = 0; s < mm2::kModSlots; ++s) {
            if (nrpn == mm2::sourceNrpn(s))
               return patch().routes[s].source;
            if (nrpn == mm2::destinationNrpn(s))
               return patch().routes[s].destination;
         }
         for (auto [n, v] : patch().settings)
            if (n == nrpn)
               return v;
         return mm2::kSettings[k].defaultValue;
      }
      for (auto [cc, v] : patch().values)
         if (cc == static_cast<int>(id))
            return v;
      return (mm2::indexOfCc(static_cast<int>(id)) * 37) % 128;
   }
   // Every fifth knob shows some host modulation.
   double paramModulated(clap_id id) const override {
      return mm2::modulated(paramValue(id), mm2::indexOfCc(static_cast<int>(id)) % 5 == 0 ? 25.0 : 0.0);
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   int channel() const override { return 1; }
   void setChannel(int) override {}
   void sendAll() override {}
   void loadProgram(const mm2::HardwarePatch &) override {}
   void loadSound(const mm2::HardwarePatch &) override {}
   // A program from the bank, so the mod matrix labels show.
   int program() const override { return patch().program; }
   bool routingKnown() const override { return true; }
   mm2::ModRoute route(int slot) const override { return patch().routes[slot]; }
   std::string presetName() const override { return mm2::displayName(patch()); }
   void setPresetName(const std::string &) override {}
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height [page]]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 1640;
   const int height = argc > 3 ? std::atoi(argv[3]) : 1142;
   const int page = argc > 4 ? std::atoi(argv[4]) : 0;
   DemoHost host;
   return mm2::renderScreenshot(host, width, height, argv[1], page) ? 0 : 1;
}
