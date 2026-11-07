#include "editor.h"
#include <jclap/ui/design_frame.h>
namespace jbreath {
namespace {
class Panel : public jclap::ui::DesignFrame {
public:
   explicit Panel(EditorHost &) : DesignFrame(1648, 940) {}
};
} // namespace
std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<jclap::ui::Window>(std::make_unique<Panel>(host), jclap::ui::WindowOptions{1648, 940});
}
bool renderScreenshot(EditorHost &host, int width, int height, int, const char *pngPath) {
   Panel panel(host);
   return jclap::ui::renderToPng(panel, width, height, pngPath);
}
} // namespace jbreath
