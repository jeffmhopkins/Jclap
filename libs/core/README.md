<div align="center">

# jclap::core

The CLAP boilerplate every JClap plugin needs, with no GUI dependencies.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![C++: 17](https://img.shields.io/badge/C%2B%2B-17-blue)](../../CMakeLists.txt) [![CLAP: 1.2.2](https://img.shields.io/badge/CLAP-1.2.2-blue)](https://github.com/free-audio/clap) [![tests: TestHost](https://img.shields.io/badge/tests-TestHost-blue)](tests)

</div>

Link it with `target_link_libraries(<name> PRIVATE jclap::core)`.

> [!NOTE]
> `jclap::ui` links `jclap::core` publicly, so a plugin with a GUI only needs `jclap::ui`.

| Header | What it is |
|---|---|
| `jclap/core/plugin.h` | `Plugin` base class. Every `clap_plugin_t` callback, and the params, state, note-port and audio-port extensions, become virtuals to override. Defaults: one MIDI note port and one stereo audio port each way |
| `jclap/core/entry.h` | `JCLAP_ENTRY(MyPlugin)` defines `clap_entry` and the factory, for one or more plugins |
| `jclap/core/helpers.h` | `ParamDef` + `fillParamInfo`, `pushParamValue` / `pushParamGesture` / `pushMidi`, `isNoteEvent`, `passAudioThrough`, `writeAll` / `readAll` for state streams |
| `jclap/core/edit_queue.h` | `EditQueue`: carries GUI edits to the audio thread and out to the host as gesture and value events |
| `jclap/core/spsc_queue.h` | `SpscQueue`: a lock-free queue for small messages between the GUI and the audio thread, such as notes from an on-screen keyboard |
| `jclap/core/sample_fifo.h` | `SampleFifo`: the audio thread writes samples, the GUI reads the newest ones for a scope |
| `jclap/core/test_host.h` | `TestHost`: an in-process host for tests. Send params, modulation and MIDI, run blocks with audio, read back events, values and value text |

A minimal plugin:

```cpp
class MyPlugin final : public jclap::core::Plugin {
public:
   static const clap_plugin_descriptor_t descriptor;
   explicit MyPlugin(const clap_host_t *host) : Plugin(&descriptor, host) {}

   clap_process_status process(const clap_process_t *p) override {
      jclap::core::passAudioThrough(p);
      return CLAP_PROCESS_CONTINUE;
   }
   const void *extension(const char *id) override {
      return !std::strcmp(id, CLAP_EXT_AUDIO_PORTS) ? &audioPortsExtension : nullptr;
   }
};

JCLAP_ENTRY(MyPlugin)
```

> [!TIP]
> `plugins/_template` shows the whole thing with params, state and a GUI.

`tests/test_core.cpp` checks the library itself.
