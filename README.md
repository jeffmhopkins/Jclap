# JClap

A collection of small, single-purpose CLAP utility plugins. Each one does one job, usually with no GUI;
any settings show up as plain host parameters.

## Plugins

| Plugin | Type | What it does |
|---|---|---|
| [Orb Polar](plugins/orb-polar) | Note effect | Turns two XY CCs into angle/radius CCs for Omnisphere's Orb |

## Download

Grab the zip for your OS from the [latest release](https://github.com/jeffmhopkins/Jclap/releases/latest).
It contains every plugin. Copy the `.clap` files (bundles on macOS) to your CLAP folder:

- macOS: `~/Library/Audio/Plug-Ins/CLAP`
- Windows: `C:\Program Files\Common Files\CLAP`
- Linux: `~/.clap`

On macOS the plugins are only ad-hoc signed (not notarized), so files downloaded through a browser are
quarantined and Bitwig silently skips them. Files downloaded with `curl` aren't flagged, so the easiest
install is from Terminal:

```sh
curl -sL https://github.com/jeffmhopkins/Jclap/releases/latest/download/jclap-macOS.zip -o /tmp/jclap.zip && mkdir -p ~/Library/Audio/Plug-Ins/CLAP && ditto -x -k /tmp/jclap.zip ~/Library/Audio/Plug-Ins/CLAP
```

If you downloaded with a browser instead, clear the quarantine flag once after copying:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/*.clap
```

Then restart Bitwig (or rescan plug-ins under Settings → Locations).

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

All plugins land in `build/clap/`. CI builds Linux, macOS (universal) and Windows on every push.

## Adding a plugin

1. Create `plugins/<name>/` with its sources, tests and a short `README.md`.
2. Add `plugins/<name>/CMakeLists.txt`:

   ```cmake
   jclap_add_plugin(<name>
      OUTPUT_NAME "<Display Name>"
      BUNDLE_ID com.jeffmhopkins.jclap.<name>
      SOURCES src/plugin.cpp)

   jclap_add_test(<name>-tests SOURCES tests/test_<name>.cpp)
   ```

3. Add `add_subdirectory(plugins/<name>)` to the top-level `CMakeLists.txt` and a row to the table
   above.

Use `com.jeffmhopkins.jclap.<name>` as the CLAP plugin id too, so ids never collide.

## Releasing

In GitHub, open Actions → build → Run workflow and enter a version such as `v0.2.0`. The run builds
every platform, tags the commit and publishes a release with one zip per OS. Pushing a `v*` tag does
the same.
