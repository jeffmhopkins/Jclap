<div align="center">

# JTunnel

Sends a track's audio and MIDI to other JTunnel instances on the network, or plays what they send.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![CLAP: audio effect](https://img.shields.io/badge/CLAP-audio%20effect-blue)](https://github.com/free-audio/clap) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](#panel) [![codec: Opus](https://img.shields.io/badge/codec-Opus-blue)](https://opus-codec.org)

</div>

Put JTunnel on a track on one computer as a **sender**, give the tunnel a name, and put another
JTunnel on a track on another computer (or another DAW, or another track) as a **receiver**
listening to that name. The receiver plays the sender's audio and MIDI. Built for a home or studio
LAN, Wi-Fi included.

![Panel](docs/panel.png)

## Using it

1. On the sending track, add **JTunnel**, leave it on **SEND** and type a name under **SEND AS**
   (it starts as this computer's name and a number).
2. On the receiving track, add **JTunnel**, switch it to **RECEIVE**, and pick the tunnel from the
   **ON THE NETWORK** list, or type its name.
3. Audio starts within a second. The link turns green and **LATENCY** shows how much the receiver
   is buffering.

One sender can feed any number of receivers. MIDI (notes and channel messages) travels with the
audio, sample-locked to it, so a receiver can drive an instrument on the far machine.

If the list stays empty (see [macOS](#macos-local-network) below, or a router that blocks
multicast), type the sender's address into the **LISTEN TO** box instead: the sender's panel shows
it under **REACHABLE AT**, such as `192.168.1.20:47000`.

## How it works

- **Discovery:** senders announce `<name>._jtunnel._udp.local.` over mDNS, the multicast group
  Bonjour uses. Instances in the same process (two tracks in one DAW) also find each other directly.
- **Transport:** plain UDP, unicast to each receiver, which renews its subscription twice a second.
  Senders listen on port 47000 (or the next free one up to 47015).
- **Format:** Opus at 48 kHz in 5 ms frames by default (64 to 512 kbps), or uncompressed 24-bit PCM
  at the host's rate in 2 ms frames. With **REDUNDANT** on, every packet also carries the previous
  frame, so a single lost packet costs nothing.
- **Clock drift:** two machines' audio clocks never run at exactly the same speed. The receiver
  measures how far ahead of playback the freshest audio arrives and nudges its playback rate (by
  a few hundred ppm at most, inaudible) to hold that lead at the **BUFFER** setting. **CLOCK** shows
  the measured difference. Different sample rates on the two ends are resampled too.
- **Jitter and loss:** a lost frame is concealed (Opus fills it in convincingly; PCM fades) and
  skipped. A late one, as when Wi-Fi stalls, is concealed while playback waits for it. With **AUTO**
  on, each such dropout grows the buffer by the length of the stall, and after 20 quiet seconds it
  shrinks back by 1 ms every 5 s.
- **Threads:** the audio thread never touches the network. The sender codes frames and queues
  packets for a send thread; a receive thread puts frames in a lock-free ring the audio thread reads.

## Parameters

| Param | Range | What it does |
|---|---|---|
| Mode | Send, Receive | What this instance does |
| Format | Opus, PCM 24-bit | How a sender codes audio. Opus is the one to use on Wi-Fi |
| Bitrate | 64 to 512 kbps | Opus bitrate. 192 is transparent for most material |
| Redundancy | Off, On | Repeat each frame in the next packet (doubles the bandwidth) |
| Buffer | 2 to 250 ms | The receiver's lead over late packets. About 5 ms on Ethernet, 20 or more on Wi-Fi |
| Auto Buffer | Off, On | Grow the buffer after dropouts, and shrink it slowly when the network is quiet |
| Thru | Off, On | Sender: keep playing its input locally. Receiver: mix its input in with what it receives |
| Level | -inf to +12 dB | Receiver output level |

The tunnel name and the listen target are saved with the project, not as parameters.

## Latency

One way, on top of the host's own buffers: the sender's frame (5 ms for Opus, 2 ms for PCM),
Opus's 2.5 ms of lookahead, the network, and the receiver's buffer. On wired Ethernet with PCM and
a 3 ms buffer that is under 10 ms. On Wi-Fi with Opus expect 25 to 60 ms, more on a busy network.
Neither host can compensate for it, so JTunnel suits monitoring, jamming and feeding another
machine's effects, not sample-tight overdubs across machines.

> [!NOTE]
> A stream runs in real time. JTunnel tells the host so (CLAP's render extension); an offline
> bounce, faster than real time, won't capture what a receiver is receiving.

## macOS Local Network

Since macOS 15, apps need **Local Network** permission to talk to other machines, and a plugin
runs with its host's. The first time JTunnel runs, macOS asks whether your DAW may find devices on
the local network; allow it, or turn it on later in **System Settings > Privacy & Security > Local
Network**. Without it nothing reaches the network, though instances in the same DAW still work.

## Panel

**SEND / RECEIVE** in the header sets the mode, and the panel below follows it. **LINK** draws the
link, the sending end on the left and the receiving end on the right, in green while audio flows,
amber while waiting and red without a socket, with warnings under it. Its graph shows the last 8
seconds: a receiver's lead (what **LATENCY** shows) against its **TARGET**, with a red tick for each
lost or late frame; a sender's level going out.

- **Receiving:** **LISTEN TO** takes a tunnel's name or a typed address, and lists the tunnels
  **ON THE NETWORK**; click one to listen to it. The readouts are **LATENCY** (the buffer),
  **TARGET** (what it is aiming for), **CLOCK** (the sender's clock against this one) and
  **LOST / LATE** frame counts. **RECEIVE** holds the buffer and **AUTO**.
- **Sending:** **SEND AS** holds the name and the address receivers can type. The readouts are the
  receivers, the format, the rate and the upload they cost together. **SEND FORMAT** holds the
  codec, the bitrate (Opus only) and **REDUNDANT**.

**OUTPUT** has **THRU**, the receiver's **LEVEL**, and input and output meters.

> [!TIP]
> To regenerate `docs/panel.png`, configure with `-DJCLAP_BUILD_TOOLS=ON` and run
> `build/plugins/jtunnel/jtunnel-screenshot plugins/jtunnel/docs/panel.png`. Add `1440 960 send` for
> the sender's view, and `idle` after that to see either before anything connects.

## Tests

`jtunnel-engine-tests` checks the packet format, both codecs, the resampler (over 90 dB clean at
every rate pair), and a sender and receiver whose clocks run 150 ppm apart, joined by a simulated
Wi-Fi link with jitter, 60 ms stalls and 2% loss, for 90 simulated seconds: the receiver must find
the clock offset, stop dropping out, keep the tone at its level and deliver a note with its length
intact. `jtunnel-plugin-tests` runs two whole plugins in one process over real UDP on the loopback
interface, one sending a tone and a note, the other finding the tunnel by name and playing them.
