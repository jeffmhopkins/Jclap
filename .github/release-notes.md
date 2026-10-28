## Install

Download the zip for your system below, unzip it, and copy the `.clap` files into your CLAP folder.
`jclap-<system>.zip` holds every plugin; `jclap-<Plugin>-<system>.zip` holds just that one.
Then restart your DAW, or rescan plug-ins (in Bitwig: Settings → Locations).

| System | CLAP folder |
|---|---|
| macOS | `~/Library/Audio/Plug-Ins/CLAP` |
| Windows | `C:\Program Files\Common Files\CLAP` |
| Linux | `~/.clap` |

### macOS: one extra step

macOS puts a quarantine flag on anything downloaded through a web browser. A flagged plugin only loads
if Apple has notarized it, and notarizing requires a paid Apple Developer account, which this project
doesn't use. The plugins are signed, just not notarized. So after a browser download, macOS quietly
blocks them and Bitwig never lists them.

There are two ways around it. Pick one.

**Option A: install from Terminal (no quarantine).** Files downloaded with `curl` are never flagged.
This command downloads this release and installs it into your CLAP folder. It replaces older copies
outright, since unzipping over an old bundle can leave stale files that stop it loading, and clears
Bitwig's plugin cache so the next scan starts fresh:

```sh
curl -sL https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-macOS.zip -o /tmp/jclap.zip && rm -rf /tmp/jclap && ditto -x -k /tmp/jclap.zip /tmp/jclap \
  && mkdir -p ~/Library/Audio/Plug-Ins/CLAP && (cd /tmp/jclap && for p in *.clap; do
    rm -rf ~/Library/Audio/Plug-Ins/CLAP/"$p" && mv "$p" ~/Library/Audio/Plug-Ins/CLAP/; done) \
  && find ~/Library/Caches/Bitwig -maxdepth 1 -name 'clap-metadata-*' -exec rm -rf {} + 2>/dev/null; true
```

**Option B: downloaded with a browser.** Copy the plugins into your CLAP folder as usual, then clear the
flag once:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/*.clap
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
