#pragma once

// The plugin's knob panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstdint>
#include <functional>
#include <memory>

namespace mm2 {

// What the panel needs from the plugin. Called on the main (GUI) thread.
class EditorHost {
public:
   virtual ~EditorHost() = default;
   // The knob's own setting for kCcParams[index].
   virtual double baseValue(int index) const = 0;
   // What is actually sent: the setting plus any host modulation.
   virtual double modulatedValue(int index) const = 0;
   virtual void beginEdit(int index) = 0;
   virtual void setValue(int index, double value) = 0;
   virtual void endEdit(int index) = 0;
   virtual int channel() const = 0; // 1-16
   virtual void setChannel(int channel) = 0;
   virtual void sendAll() = 0;
};

class Editor {
public:
   explicit Editor(EditorHost &host);
   ~Editor();

   void show(void *parentWindow);

   // Sizes in the units the CLAP host expects (logical points on macOS, pixels elsewhere).
   uint32_t width() const;
   uint32_t height() const;
   void setSize(uint32_t width, uint32_t height);
   void adjustSize(uint32_t *width, uint32_t *height) const;

   // Linux only: the X11 connection fd the host polls for us.
   int posixFd() const;
   void processFdEvents();

   // Called when the panel wants the host window resized.
   std::function<void()> onResizeRequest;

private:
   struct Impl;
   std::unique_ptr<Impl> impl_;
};

// Draws the panel offscreen at the given pixel size and saves it as a PNG (for docs and checks).
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath);

} // namespace mm2
