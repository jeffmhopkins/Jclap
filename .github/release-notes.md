{{INTRO}}
## Install on a Mac

This installs every plugin, and replaces any older versions.

1. Quit Bitwig (or whichever music app you use).
2. Open **Terminal**: press **⌘ Command + Space**, type **Terminal**, and press **Return**.
3. Copy the line below (the copy button is at its right), paste it into Terminal with **⌘ Command + V**, and press **Return**:

```sh
curl -fsSL https://raw.githubusercontent.com/jeffmhopkins/Jclap/main/install-macos.sh | bash -s {{RELEASE}}
```

4. Wait until it says **Done**. It takes under a minute.
5. Open Bitwig again. The plugins are in its plugin list.

That's it. Please don't download the zips with a web browser on a Mac: macOS blocks plugins downloaded that way (see "Mac: installed from a browser download?" below if you already did).

## Install on Windows

1. Download [jclap-Windows.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-Windows.zip).
2. Right-click the downloaded file and choose **Extract All**.
3. Open the extracted folder. Copy everything in its **CLAP** folder into `C:\Program Files\Common Files\CLAP` (make that folder if it isn't there).
4. If your music app uses VST3, also copy everything in the **VST3** folder into `C:\Program Files\Common Files\VST3`.
5. Restart your music app.

## Install on Linux

Download [jclap-Linux.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-Linux.zip), unzip it, copy the `.clap` files to `~/.clap` and the `.vst3` folders to `~/.vst3`, and restart your music app.

## Ableton Live

Polar XY also comes as a Max for Live device, in the **Max for Live** folder of the Mac and Windows zips. See [Using it in Ableton Live](https://github.com/jeffmhopkins/Jclap/tree/main/plugins/polar-xy#using-it-in-ableton-live-max-for-live).

## All downloads

You don't need these if you used the steps above.

| System | Every plugin |
|---|---|
| macOS | [jclap-macOS.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-macOS.zip) |
| Windows | [jclap-Windows.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-Windows.zip) |
| Linux | [jclap-Linux.zip](https://github.com/jeffmhopkins/Jclap/releases/download/{{VERSION}}/jclap-Linux.zip) |

Or one plugin at a time (each zip has a macOS, Windows and Linux folder inside):

{{PLUGINS}}

<details>
<summary><b>Mac: installed from a browser download?</b></summary>

macOS blocks plugins downloaded with a web browser unless Apple has approved them (notarized them), which needs a paid Apple developer account this project doesn't have. Bitwig then quietly leaves them out of its list.

The easiest fix is to use the Terminal steps above instead. Or, after copying the plugins into `~/Library/Audio/Plug-Ins/CLAP` and `~/Library/Audio/Plug-Ins/VST3`, open Terminal, paste this line, and press Return:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/*.clap ~/Library/Audio/Plug-Ins/VST3/PolarXY.vst3
```

Then restart Bitwig.

</details>

<details>
<summary><b>What the Mac install line does</b></summary>

It runs [install-macos.sh](https://github.com/jeffmhopkins/Jclap/blob/main/install-macos.sh) (you can read it first). It downloads `jclap-macOS.zip` for this release, puts every plugin in `~/Library/Audio/Plug-Ins/CLAP` and `~/Library/Audio/Plug-Ins/VST3`, replacing older copies, and clears Bitwig's plugin list so Bitwig looks again.

</details>

<details>
<summary><b>Upgrading from Orb Polar (0.1.4 or earlier)</b></summary>

The plugin was renamed from **Orb Polar** to **Polar XY**, so Bitwig sees it as a new plugin. To remove the old copy and clear Bitwig's plugin list, paste these into Terminal:

```sh
find ~/Library/Audio/Plug-Ins/CLAP -maxdepth 1 -name 'Orb*Polar*.clap' -exec rm -rf {} +
find ~/Library/Caches/Bitwig -maxdepth 1 -name 'clap-metadata-*' -exec rm -rf {} +
```

Projects that used Orb Polar need the device swapped for Polar XY.

</details>
