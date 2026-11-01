## Download

**Everything for your system.** Every plugin, in a `CLAP` folder and a `VST3` folder (and on macOS and
Windows a `Max for Live` folder):

| System | Zip |
|---|---|
| macOS | [jclap-macOS.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-macOS.zip) |
| Windows | [jclap-Windows.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-Windows.zip) |
| Linux | [jclap-Linux.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-Linux.zip) |

**Or one plugin, for every system.** Each zip has a `macOS`, `Windows` and `Linux` folder; take the one
you need. Each plugin has its own version, and only changes it when the plugin itself changes:

{{PLUGINS}}

On a Mac, the quickest install is from Terminal (see below for why):

```sh
curl -fsSL https://raw.githubusercontent.com/jeffmhopkins/Jclap/main/install-macos.sh | bash -s {{VERSION}}
```

## Install

Unzip, copy the `.clap` files (bundles on macOS) into your CLAP folder, and the `.vst3` too if your host
needs VST3 (only Polar XY has one). Then restart your DAW, or rescan plug-ins (in Bitwig: Settings →
Locations).

| System | CLAP folder | VST3 folder |
|---|---|---|
| macOS | `~/Library/Audio/Plug-Ins/CLAP` | `~/Library/Audio/Plug-Ins/VST3` |
| Windows | `C:\Program Files\Common Files\CLAP` | `C:\Program Files\Common Files\VST3` |
| Linux | `~/.clap` | `~/.vst3` |

For Ableton Live, Polar XY comes as a Max for Live MIDI effect, in the `Max for Live` folder; see
[Using it in Ableton Live](https://github.com/jeffmhopkins/Jclap/tree/main/plugins/polar-xy#using-it-in-ableton-live-max-for-live).

### macOS: one extra step

macOS puts a quarantine flag on anything downloaded through a web browser. A flagged plugin only loads
if Apple has notarized it, and notarizing requires a paid Apple Developer account, which this project
doesn't use. The plugins are signed, just not notarized. So after a browser download, macOS quietly
blocks them and Bitwig never lists them. There are two ways around it. Pick one.

**Option A: install from Terminal (no quarantine).** Files downloaded with `curl` are never flagged.
The command above downloads `jclap-macOS.zip` and installs every CLAP and VST3 into your plug-in
folders. It replaces older copies outright, since unzipping over an old bundle can leave stale files
that stop it loading, and clears Bitwig's plugin cache so the next scan starts fresh. It's
[install-macos.sh](https://github.com/jeffmhopkins/Jclap/blob/main/install-macos.sh) if you'd like to
read it first.

**Option B: downloaded with a browser.** Copy the plugins in as usual, then clear the flag once:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/*.clap ~/Library/Audio/Plug-Ins/VST3/PolarXY.vst3
```

Either way, restart Bitwig afterwards.

### Upgrading from 0.1.4 or earlier

The plugin was renamed from **Orb Polar** to **Polar XY** (new file name and plugin ID), so Bitwig
treats it as a brand-new plugin and can't reuse an old cached scan. Delete the old copies so you
don't see two, and clear Bitwig's plugin cache:

```sh
find ~/Library/Audio/Plug-Ins/CLAP -maxdepth 1 -name 'Orb*Polar*.clap' -exec rm -rf {} +
find ~/Library/Caches/Bitwig -maxdepth 1 -name 'clap-metadata-*' -exec rm -rf {} +
```

Projects that used Orb Polar need the device swapped for Polar XY.
