# Capturing how MIDI Control Center reads presets

Arturia doesn't publish the MatrixBrute's SysEx, but MIDI Control Center (MCC) reads presets from the
synth over USB without any front panel steps, so the synth answers some request. Recording the USB
cable while MCC does that shows the request and the preset format. If the request is plain SysEx, the
plugin can send it itself and build its preset library on demand.

This is for Windows. Nothing sits between MCC and the synth: USBPcap only listens to the USB bus.

## 1. Install the tools (once)

1. Wireshark, with **USBPcap** ticked in its installer (or `winget install WiresharkFoundation.Wireshark`
   and USBPcap from <https://desowin.org/usbpcap/>). USBPcap is a driver, so reboot afterwards.
2. Python 3, and for step 4 `pip install mido python-rtmidi`.
3. MIDI Control Center from Arturia's MatrixBrute resources page.

## 2. Capture

Plug the MatrixBrute in over USB and turn it on. Then, from `plugins/matrixbrute/tools`:

```powershell
powershell -ExecutionPolicy Bypass -File mb_capture.ps1 -Seconds 120 -Out capture.pcapng
```

While it runs (two minutes), open MCC, let it connect to the MatrixBrute, and receive or sync its
presets into MCC's library. Also useful, each as its own capture:

- one preset after **turning a few knobs** to known places (Steiner cutoff fully down, the rest left
  alone), so the bytes for each parameter can be found, as the MicroMonsta 2's `cal-*.syx` were;
- MCC **sending** a preset to the synth;
- MCC reading the synth's **settings** (MIDI channel, pot mode, 14-bit, Local).

## 3. Pull out the MIDI

```powershell
python mb_usb_extract.py capture.pcapng out
```

`out/summary.txt` lists each distinct message MCC sent the synth and the shape of the replies.
`out/to-synth.syx` and `out/from-synth.syx` hold the SysEx each way, and `out/messages.txt` the timeline.

## 4. Ask the synth without MCC

Close MCC (it holds the port), then replay one of MCC's requests:

```powershell
python mb_request.py --hex "F0 00 20 6B ... F7" reply.syx
```

If `reply.syx` matches what MCC got, the plugin can request presets on demand.
