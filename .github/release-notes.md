## Plugins

Each plugin has its own version, and only changes it when the plugin itself changes:

{{PLUGINS}}

## Download

The whole collection, one zip per system:

| System | All CLAP plugins |
|---|---|
| macOS | [jclap-macOS.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-macOS.zip) |
| Windows | [jclap-Windows.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-Windows.zip) |
| Linux | [jclap-Linux.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-Linux.zip) |

On a Mac, the quickest install is from Terminal (see below for what it does):

```sh
curl -fsSL https://raw.githubusercontent.com/jeffmhopkins/Jclap/main/install-macos.sh | bash -s {{VERSION}}
```

The single-plugin zips, named with the plugin's version, are listed under Assets at the bottom, along
with the VST3 zips. Only Polar XY has a VST3 build; every other plugin is CLAP only. Polar XY also
comes as a Max for Live device for Ableton Live, in `jclap-PolarXY-<version>-M4L.zip`.

## Install

Download the zip for your system below, unzip it, and copy the `.clap` files into your CLAP folder.
`jclap-<system>.zip` holds every plugin; `jclap-<Plugin>-<version>-<system>.zip` holds just that one.
Then restart your DAW, or rescan plug-ins (in Bitwig: Settings → Locations).

| System | CLAP folder |
|---|---|
| macOS | `~/Library/Audio/Plug-Ins/CLAP` |
| Windows | `C:\Program Files\Common Files\CLAP` |
| Linux | `~/.clap` |

Polar XY also comes as a VST3: `jclap-PolarXY-<version>-VST3-<system>.zip` holds `PolarXY.vst3` (as does
`jclap-VST3-<system>.zip`, which has every VST3). Copy it into your VST3 folder:

| System | VST3 folder |
|---|---|
| macOS | `~/Library/Audio/Plug-Ins/VST3` |
| Windows | `C:\Program Files\Common Files\VST3` |
| Linux | `~/.vst3` |

### macOS: one extra step

macOS puts a quarantine flag on anything downloaded through a web browser. A flagged plugin only loads
if Apple has notarized it, and notarizing requires a paid Apple Developer account, which this project
doesn't use. The plugins are signed, just not notarized. So after a browser download, macOS quietly
blocks them and Bitwig never lists them.

There are two ways around it. Pick one.

**Option A: install from Terminal (no quarantine).** Files downloaded with `curl` are never flagged.
This command downloads this release's CLAPs and VST3s and installs them into your plug-in folders.
It replaces older copies outright, since unzipping over an old bundle can leave stale files that stop
it loading, and clears Bitwig's plugin cache so the next scan starts fresh:

```sh
curl -fsSL https://raw.githubusercontent.com/jeffmhopkins/Jclap/main/install-macos.sh | bash -s {{VERSION}}
```

It's [install-macos.sh](https://github.com/jeffmhopkins/Jclap/blob/main/install-macos.sh) if you'd like to read it first.

**Option B: downloaded with a browser.** Copy the plugins into your CLAP folder as usual, then clear the
flag once:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/*.clap
```

The VST3 needs the same after a browser download: `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/PolarXY.vst3`.

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
