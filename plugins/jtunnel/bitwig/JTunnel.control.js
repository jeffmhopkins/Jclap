// JTunnel extension for Bitwig Studio: hands what Bitwig is doing (the clip launcher grid with every
// clip's state, the scenes, the transport, the mixer, meters, playing notes, each track's pitch
// bend and pressure by MIDI channel, the selected device and its controls, cue markers) to the
// JTunnel senders on this computer, which carry it beside
// their audio and MIDI as the tunnel's side channel. A receiver serves it to programs on its own
// computer (see the JTunnel README, "Side channel").
//
// Bitwig doesn't let plugins see the clip launcher, so this script reads it and talks to JTunnel
// over OSC on loopback: it listens on UDP port 47250, each JTunnel sender says hello every second
// from a port of 47251 to 47266, and the script sends that sender every key it has and then every
// change (/jtunnel/state key json), and /jtunnel/alive each second.
//
// Bitwig's API shows a track's playing notes only as pitch and velocity, so each JTunnel sender
// reads its own track's MIDI and tells this script the rest (/jtunnel/expression port track color
// json: the track's pitch bend, pressure and held notes, channel by channel, at most 15 times a
// second). The script finds the track in its grid by name (and colour, when two share a name) and
// sends them all on as the `expression` key.
//
// Install: copy this file to Documents/Bitwig Studio/Controller Scripts/, then in Settings >
// Controllers add "Jclap > JTunnel". It needs no MIDI ports. Its settings there pick the grid's size,
// whether it follows the selection, and which kinds of state go out.
//
// READ ONLY: it never changes anything in the project.

loadAPI(18);

host.defineController("Jclap", "JTunnel", "0.1", "4c235e29-7ea2-41aa-887f-27806841fbfd", "Jclap");
host.defineMidiPorts(0, 0);

var SCRIPT_PORT = 47250;
var CLIENT_PORT_FIRST = 47251;
var CLIENT_PORT_COUNT = 16;
// The most the grid can show; the settings pick how much of it is used.
var MAX_TRACKS = 32;
var MAX_SCENES = 32;
var MAX_FX = 8;
var MAX_DEVICES = 16;
var MAX_MARKERS = 32;
var PARAMS = 8;
var TICK_MS = 33; // 30 times a second
var SUBSCRIBER_TIMEOUT_MS = 4000;

var transport, application, arranger, markers, cursorTrack, trackBank, sceneBank, fxBank, master;
var cursorDevice, remote, deviceBank, cursorClip;
// Selection flags: Bitwig allows createEqualsValue only in init(), so each is made once there.
var trackSelected = [], deviceSelected = [];
var meters = {tracks: [], fx: [], master: [0, 0]};
var settings = {};
var osc, clientConns = {}, subscribers = {};
var lastSent = {}; // key -> the JSON last sent
var lastAlive = 0;
var expressions = {}; // sender port -> {track, color, value, seen}: what each sender's track plays

function init() {
   transport = host.createTransport();
   application = host.createApplication();
   arranger = host.createArranger();
   cursorTrack = host.createCursorTrack("JTUNNEL_CURSOR", "JTunnel", 0, MAX_SCENES, true);
   trackBank = host.createMainTrackBank(MAX_TRACKS, 0, MAX_SCENES);
   sceneBank = trackBank.sceneBank();
   fxBank = host.createEffectTrackBank(MAX_FX, 0);
   master = host.createMasterTrack(0);
   cursorDevice = cursorTrack.createCursorDevice();
   remote = cursorDevice.createCursorRemoteControlsPage(PARAMS);
   deviceBank = cursorTrack.createDeviceBank(MAX_DEVICES);
   cursorClip = cursorTrack.createLauncherCursorClip(16, 1);
   markers = arranger.createCueMarkerBank(MAX_MARKERS);

   readSettings();
   observe();
   startServer();
   tick();
}

function exit() {}
function flush() {}

// ---- settings ----

function readSettings() {
   var prefs = host.getPreferences();
   var grid = prefs.getNumberSetting("Tracks", "Grid", 1, MAX_TRACKS, 1, "", 16);
   var scenes = prefs.getNumberSetting("Scenes", "Grid", 1, MAX_SCENES, 1, "", 16);
   var follow = prefs.getEnumSetting("Follow", "Grid", ["Selection", "Off"], "Selection");
   var tunnel = prefs.getStringSetting("Only to tunnel", "Senders", 31, "");
   settings.tracks = 16;
   settings.scenes = 16;
   settings.follow = true;
   settings.tunnel = "";
   grid.addRawValueObserver(function (v) {
      settings.tracks = Math.max(1, Math.min(MAX_TRACKS, Math.round(v)));
      resize();
   });
   scenes.addRawValueObserver(function (v) {
      settings.scenes = Math.max(1, Math.min(MAX_SCENES, Math.round(v)));
      resize();
   });
   follow.addValueObserver(function (v) {
      settings.follow = String(v) === "Selection";
   });
   tunnel.addValueObserver(function (v) {
      settings.tunnel = String(v).toLowerCase();
   });
   // Each kind of state can be left out, to keep the tunnel's side channel small.
   var topics = [["grid", "Clip grid and scenes"], ["transport", "Transport"], ["mixer", "Mixer"],
                 ["meters", "Meters"], ["notes", "Playing notes"],
                 ["expression", "Pitch bend and pressure"], ["device", "Selected device"],
                 ["markers", "Cue markers"], ["clip", "Selected clip"], ["project", "Project"]];
   settings.topics = {};
   topics.forEach(function (t) {
      settings.topics[t[0]] = true;
      prefs.getBooleanSetting(t[1], "Send", true).addValueObserver(function (on) {
         settings.topics[t[0]] = on;
         if (!on)
            forget(t[0]);
      });
   });
}

function resize() {
   guarded(function () {
      trackBank.setSizeOfBank(settings.tracks);
      sceneBank.setSizeOfBank(settings.scenes);
      for (var t = 0; t < MAX_TRACKS; t++)
         trackBank.getItemAt(t).clipLauncherSlotBank().setSizeOfBank(settings.scenes);
   })();
   // Show which part of the launcher goes out, as a controller's frame in Bitwig.
   guarded(function () {
      trackBank.setShouldShowClipLauncherFeedback(true);
   })();
}

// ---- what's observed ----

function interested(values) {
   for (var i = 0; i < values.length; i++)
      if (values[i])
         values[i].markInterested();
}

function observeChannel(ch) {
   interested([ch.exists(), ch.name(), ch.color(), ch.mute(), ch.solo(), ch.isMutedBySolo(), ch.isActivated(),
               ch.volume().value(), ch.volume().displayedValue(), ch.pan().value(), ch.pan().displayedValue(),
               ch.playingNotes()]);
}

function meterObserver(list, index) {
   if (!list[index])
      list[index] = [0, 0];
   return [function (v) { list[index][0] = v / 127; }, function (v) { list[index][1] = v / 127; }];
}

function observe() {
   interested([transport.isPlaying(), transport.isArrangerRecordEnabled(), transport.isArrangerOverdubEnabled(),
               transport.isClipLauncherOverdubEnabled(), transport.automationWriteMode(),
               transport.isArrangerAutomationWriteEnabled(), transport.isClipLauncherAutomationWriteEnabled(),
               transport.isAutomationOverrideActive(), transport.isArrangerLoopEnabled(),
               transport.arrangerLoopStart(), transport.arrangerLoopDuration(), transport.isPunchInEnabled(),
               transport.isPunchOutEnabled(), transport.isMetronomeEnabled(), transport.preRoll(),
               transport.tempo().value(), transport.tempo().displayedValue(), transport.playPosition(),
               transport.playPositionInSeconds(), transport.playStartPosition(), transport.timeSignature(),
               transport.timeSignature().numerator(), transport.timeSignature().denominator(),
               transport.defaultLaunchQuantization(), transport.isFillModeActive(), transport.crossfade().value(),
               transport.clipLauncherPostRecordingAction()]);
   interested([application.projectName(), application.panelLayout(), application.hasActiveEngine()]);
   interested([trackBank.scrollPosition(), trackBank.itemCount(), sceneBank.scrollPosition(), sceneBank.itemCount()]);
   interested([cursorTrack.position(), cursorTrack.name(), cursorTrack.color(), cursorTrack.exists()]);

   for (var t = 0; t < MAX_TRACKS; t++) {
      var track = trackBank.getItemAt(t);
      observeChannel(track);
      interested([track.position(), track.trackType(), track.isGroup(), track.arm(), track.monitorMode(),
                  track.isStopped(), track.isQueuedForStop()]);
      trackSelected[t] = track.createEqualsValue(cursorTrack);
      trackSelected[t].markInterested();
      var slots = track.clipLauncherSlotBank();
      for (var s = 0; s < MAX_SCENES; s++) {
         var slot = slots.getItemAt(s);
         interested([slot.exists(), slot.name(), slot.color(), slot.hasContent(), slot.isPlaying(),
                     slot.isPlaybackQueued(), slot.isRecording(), slot.isRecordingQueued(), slot.isStopQueued(),
                     slot.isSelected(), slot.sceneIndex()]);
      }
      var m = meterObserver(meters.tracks, t);
      track.addVuMeterObserver(128, 0, true, m[0]);
      track.addVuMeterObserver(128, 1, true, m[1]);
   }
   for (var s2 = 0; s2 < MAX_SCENES; s2++) {
      var scene = sceneBank.getItemAt(s2);
      interested([scene.exists(), scene.name(), scene.color(), scene.sceneIndex(), scene.clipCount()]);
   }
   for (var f = 0; f < MAX_FX; f++) {
      var fx = fxBank.getItemAt(f);
      observeChannel(fx);
      var fm = meterObserver(meters.fx, f);
      fx.addVuMeterObserver(128, 0, true, fm[0]);
      fx.addVuMeterObserver(128, 1, true, fm[1]);
   }
   observeChannel(master);
   master.addVuMeterObserver(128, 0, true, function (v) { meters.master[0] = v / 127; });
   master.addVuMeterObserver(128, 1, true, function (v) { meters.master[1] = v / 127; });

   interested([cursorDevice.exists(), cursorDevice.name(), cursorDevice.presetName(), cursorDevice.presetCategory(),
               cursorDevice.presetCreator(), cursorDevice.isEnabled(), cursorDevice.isPlugin(),
               cursorDevice.isWindowOpen(), cursorDevice.position(), cursorDevice.isPinned(),
               remote.pageNames(), remote.selectedPageIndex(), remote.pageCount()]);
   for (var p = 0; p < PARAMS; p++) {
      var param = remote.getParameter(p);
      interested([param.exists(), param.name(), param.value(), param.displayedValue(), param.modulatedValue()]);
   }
   for (var d = 0; d < MAX_DEVICES; d++) {
      var dev = deviceBank.getItemAt(d);
      interested([dev.exists(), dev.name(), dev.isEnabled()]);
      deviceSelected[d] = dev.createEqualsValue(cursorDevice);
      deviceSelected[d].markInterested();
   }
   interested([cursorClip.exists(), cursorClip.playingStep(), cursorClip.getPlayStart(), cursorClip.getLoopLength(),
               cursorClip.getLoopStart(), cursorClip.isLoopEnabled(), cursorClip.color(),
               cursorClip.clipLauncherSlot().sceneIndex(), cursorClip.clipLauncherSlot().name(),
               cursorClip.clipLauncherSlot().isPlaying()]);
   for (var k = 0; k < MAX_MARKERS; k++) {
      var marker = markers.getItemAt(k);
      interested([marker.exists(), marker.name(), marker.position(), marker.getColor()]);
   }
}

// ---- reading it out ----

function hex(color) {
   var c = function (v) {
      var n = Math.max(0, Math.min(255, Math.round(v * 255)));
      return (n < 16 ? "0" : "") + n.toString(16);
   };
   return "#" + c(color.red()) + c(color.green()) + c(color.blue());
}

function str(value) {
   return String(value.get());
}

function round(v, digits) {
   var f = Math.pow(10, digits || 3);
   return Math.round(v * f) / f;
}

function notes(ch) {
   var out = [];
   var playing = ch.playingNotes().get();
   for (var i = 0; playing && i < playing.length; i++)
      out.push([playing[i].pitch(), playing[i].velocity()]);
   return out;
}

// What the JTunnel sender on this track says it plays (see the top of this file), or null for a
// track without one. A sender is matched by its track's name, and by colour when more than one
// track has that name; with two senders on one track, the one heard from last.
function expressionFor(track) {
   if (!track.exists().get())
      return null;
   var name = str(track.name()), color = hex(track.color());
   var best = null, bestScore = 0;
   for (var port in expressions) {
      var e = expressions[port];
      if (e.track !== name)
         continue;
      var score = 1 + (e.color && sameColor(e.color, color) ? 1 : 0);
      if (score > bestScore || (score === bestScore && e.seen > best.seen)) {
         best = e;
         bestScore = score;
      }
   }
   return best ? best.value : null;
}

// Two "#rrggbb" colours, allowing for rounding between how CLAP and the API give them.
function sameColor(a, b) {
   for (var i = 1; i < 7; i += 2)
      if (Math.abs(parseInt(a.substr(i, 2), 16) - parseInt(b.substr(i, 2), 16)) > 2)
         return false;
   return true;
}

function channelState(ch) {
   return {
      exists: ch.exists().get(),
      name: str(ch.name()),
      color: hex(ch.color()),
      mute: ch.mute().get(),
      solo: ch.solo().get(),
      mutedBySolo: ch.isMutedBySolo().get(),
      active: ch.isActivated().get(),
      volume: round(ch.volume().value().get()),
      volumeText: str(ch.volume().displayedValue()),
      pan: round(ch.pan().value().get()),
      panText: str(ch.pan().displayedValue())
   };
}

function slotState(slot) {
   var state = "empty";
   if (slot.isRecording().get())
      state = "recording";
   else if (slot.isPlaying().get())
      state = "playing";
   else if (slot.hasContent().get())
      state = "stopped";
   var queued = null;
   if (slot.isRecordingQueued().get())
      queued = "record";
   else if (slot.isPlaybackQueued().get())
      queued = "play";
   else if (slot.isStopQueued().get())
      queued = "stop";
   return {
      scene: slot.sceneIndex().get(),
      name: str(slot.name()),
      color: hex(slot.color()),
      hasContent: slot.hasContent().get(),
      state: state,
      queued: queued,
      selected: slot.isSelected().get()
   };
}

function trackState(t) {
   var track = trackBank.getItemAt(t);
   var out = channelState(track);
   out.index = track.position().get();
   out.type = str(track.trackType());
   out.group = track.isGroup().get();
   out.selected = trackSelected[t].get();
   out.armed = track.arm().get();
   out.monitor = str(track.monitorMode());
   out.stopped = track.isStopped().get();
   out.stopQueued = track.isQueuedForStop().get();
   out.playing = -1;
   out.slots = [];
   var slots = track.clipLauncherSlotBank();
   for (var s = 0; s < settings.scenes; s++) {
      var slot = slotState(slots.getItemAt(s));
      if (slot.state === "playing" || slot.state === "recording")
         out.playing = slot.scene;
      out.slots.push(slot);
   }
   return out;
}

// Every key and its message, for the kinds of state that are on.
function collect() {
   var keys = {};
   var on = settings.topics;
   if (on.project)
      keys.project = {
         name: str(application.projectName()),
         layout: str(application.panelLayout()),
         engine: application.hasActiveEngine().get(),
         host: String(host.getHostProduct()) + " " + String(host.getHostVersion()),
         api: 18
      };
   if (on.transport) {
      var tsig = transport.timeSignature();
      keys.transport = {
         playing: transport.isPlaying().get(),
         recording: transport.isArrangerRecordEnabled().get(),
         arrangerOverdub: transport.isArrangerOverdubEnabled().get(),
         launcherOverdub: transport.isClipLauncherOverdubEnabled().get(),
         automationWrite: {
            mode: str(transport.automationWriteMode()),
            arranger: transport.isArrangerAutomationWriteEnabled().get(),
            launcher: transport.isClipLauncherAutomationWriteEnabled().get(),
            overridden: transport.isAutomationOverrideActive().get()
         },
         loop: transport.isArrangerLoopEnabled().get(),
         loopStart: round(transport.arrangerLoopStart().get()),
         loopLength: round(transport.arrangerLoopDuration().get()),
         punchIn: transport.isPunchInEnabled().get(),
         punchOut: transport.isPunchOutEnabled().get(),
         metronome: transport.isMetronomeEnabled().get(),
         preRoll: str(transport.preRoll()),
         fill: transport.isFillModeActive().get(),
         tempo: round(transport.tempo().value().getRaw(), 3),
         tempoText: str(transport.tempo().displayedValue()),
         timeSignature: [tsig.numerator().get(), tsig.denominator().get()],
         position: round(transport.playPosition().get(), 2),
         positionText: String(transport.playPosition().getFormatted()),
         seconds: round(transport.playPositionInSeconds().get(), 2),
         playStart: round(transport.playStartPosition().get()),
         launchQuantization: str(transport.defaultLaunchQuantization()),
         postRecordingAction: str(transport.clipLauncherPostRecordingAction()),
         crossfade: round(transport.crossfade().value().get())
      };
   }
   if (on.grid) {
      keys.grid = {
         tracks: settings.tracks,
         scenes: settings.scenes,
         trackOffset: trackBank.scrollPosition().get(),
         sceneOffset: sceneBank.scrollPosition().get(),
         trackCount: trackBank.itemCount().get(),
         sceneCount: sceneBank.itemCount().get(),
         follow: settings.follow ? "selection" : "off",
         selectedTrack: cursorTrack.exists().get() ? cursorTrack.position().get() : -1,
         selectedScene: cursorClip.exists().get() ? cursorClip.clipLauncherSlot().sceneIndex().get() : -1
      };
      var items = [];
      for (var s = 0; s < settings.scenes; s++) {
         var scene = sceneBank.getItemAt(s);
         items.push({
            index: scene.sceneIndex().get(),
            exists: scene.exists().get(),
            name: str(scene.name()),
            color: hex(scene.color()),
            clips: scene.clipCount().get()
         });
      }
      keys.scenes = {items: items};
      for (var t = 0; t < settings.tracks; t++)
         keys["track/" + t] = trackState(t);
   }
   if (on.mixer) {
      var fx = [];
      for (var f = 0; f < MAX_FX; f++)
         if (fxBank.getItemAt(f).exists().get())
            fx.push(channelState(fxBank.getItemAt(f)));
      keys.mixer = {master: channelState(master), fx: fx};
   }
   if (on.meters) {
      var tm = [], fm = [];
      for (var i = 0; i < settings.tracks; i++)
         tm.push([round(meters.tracks[i][0], 2), round(meters.tracks[i][1], 2)]);
      for (var j = 0; j < MAX_FX; j++)
         if (fxBank.getItemAt(j).exists().get())
            fm.push([round(meters.fx[j][0], 2), round(meters.fx[j][1], 2)]);
      keys.meters = {tracks: tm, fx: fm, master: [round(meters.master[0], 2), round(meters.master[1], 2)]};
   }
   if (on.notes) {
      var tn = [];
      for (var n = 0; n < settings.tracks; n++)
         tn.push(notes(trackBank.getItemAt(n)));
      keys.notes = {tracks: tn, master: notes(master)};
   }
   if (on.expression) {
      var te = [];
      for (var x = 0; x < settings.tracks; x++)
         te.push(expressionFor(trackBank.getItemAt(x)));
      keys.expression = {tracks: te};
   }
   if (on.device) {
      var params = [];
      for (var p = 0; p < PARAMS; p++) {
         var param = remote.getParameter(p);
         params.push({
            exists: param.exists().get(),
            name: str(param.name()),
            value: round(param.value().get()),
            modulated: round(param.modulatedValue().get()),
            text: str(param.displayedValue())
         });
      }
      var chain = [];
      for (var d = 0; d < MAX_DEVICES; d++) {
         var dev = deviceBank.getItemAt(d);
         if (dev.exists().get())
            chain.push({name: str(dev.name()), enabled: dev.isEnabled().get(),
                        selected: deviceSelected[d].get()});
      }
      var pages = remote.pageNames().get();
      var pageNames = [];
      for (var pn = 0; pages && pn < pages.length; pn++)
         pageNames.push(String(pages[pn]));
      var page = remote.selectedPageIndex().get();
      keys.device = {
         exists: cursorDevice.exists().get(),
         track: str(cursorTrack.name()),
         trackColor: hex(cursorTrack.color()),
         name: str(cursorDevice.name()),
         preset: str(cursorDevice.presetName()),
         category: str(cursorDevice.presetCategory()),
         creator: str(cursorDevice.presetCreator()),
         enabled: cursorDevice.isEnabled().get(),
         plugin: cursorDevice.isPlugin().get(),
         windowOpen: cursorDevice.isWindowOpen().get(),
         pinned: cursorDevice.isPinned().get(),
         page: {index: page, name: page >= 0 && page < pageNames.length ? pageNames[page] : "", names: pageNames},
         params: params,
         chain: chain
      };
   }
   if (on.clip) {
      var slot = cursorClip.clipLauncherSlot();
      keys.clip = {
         exists: cursorClip.exists().get(),
         track: str(cursorTrack.name()),
         scene: slot.sceneIndex().get(),
         name: str(slot.name()),
         color: hex(cursorClip.color()),
         playing: slot.isPlaying().get(),
         playingStep: cursorClip.playingStep().get(),
         playStart: round(cursorClip.getPlayStart().get()),
         loopStart: round(cursorClip.getLoopStart().get()),
         loopLength: round(cursorClip.getLoopLength().get()),
         loop: cursorClip.isLoopEnabled().get()
      };
   }
   if (on.markers) {
      var list = [];
      for (var k = 0; k < MAX_MARKERS; k++) {
         var marker = markers.getItemAt(k);
         if (marker.exists().get())
            list.push({name: str(marker.name()), position: round(marker.position().get()), color: hex(marker.getColor())});
      }
      keys.markers = {items: list};
   }
   return keys;
}

// Keeps the selected clip in view: scrolls the scenes when the selection leaves them. (Tracks follow
// the selected track by themselves; see followCursorTrack.) Bitwig doesn't tell scripts where its
// own window is scrolled, so the selection is the closest thing to "what's on screen".
function follow() {
   if (!settings.follow || !cursorClip.exists().get())
      return;
   var scene = cursorClip.clipLauncherSlot().sceneIndex().get();
   var first = sceneBank.scrollPosition().get();
   if (scene < 0)
      return;
   if (scene < first)
      sceneBank.scrollPosition().set(scene);
   else if (scene >= first + settings.scenes)
      sceneBank.scrollPosition().set(scene - settings.scenes + 1);
}

// JSON with everything past ASCII escaped, so names survive any OSC string handling.
function encode(value) {
   return JSON.stringify(value).replace(/[\u007f-￿]/g, function (c) {
      return "\\u" + ("0000" + c.charCodeAt(0).toString(16)).slice(-4);
   });
}

// ---- sending ----

function tick() {
   guarded(function () {
      follow();
      var now = Date.now();
      for (var port in subscribers)
         if (now - subscribers[port].seen > SUBSCRIBER_TIMEOUT_MS)
            delete subscribers[port];
      for (var e in expressions)
         if (now - expressions[e].seen > SUBSCRIBER_TIMEOUT_MS)
            delete expressions[e];
      var keys = collect();
      for (var key in keys) {
         var json = encode(keys[key]);
         if (lastSent[key] === json)
            continue;
         lastSent[key] = json;
         for (var p in subscribers)
            send(subscribers[p], "/jtunnel/state", key, json);
      }
      if (now - lastAlive >= 1000) {
         lastAlive = now;
         for (var q in subscribers)
            send(subscribers[q], "/jtunnel/alive");
      }
   })();
   later(tick, TICK_MS);
}

// Takes a kind of state off: tells the senders to drop its keys.
function forget(topic) {
   var dropped = [];
   for (var key in lastSent)
      if (key === topic || (topic === "grid" && (key === "scenes" || key.indexOf("track/") === 0)))
         dropped.push(key);
   dropped.forEach(function (key) {
      delete lastSent[key];
      for (var p in subscribers)
         send(subscribers[p], "/jtunnel/remove", key);
   });
}

function send(sub) {
   try {
      var args = Array.prototype.slice.call(arguments, 1);
      sub.conn.sendMessage.apply(sub.conn, args);
   } catch (e) {
      println("send: " + e);
   }
}

function startServer() {
   try {
      osc = host.getOscModule();
      var space = osc.createAddressSpace();
      space.registerMethod("/jtunnel/hello", ",is", "A JTunnel sender asking for the state", guarded(function (source, msg) {
         var port = msg.getInt(0), tunnel = String(msg.getString(1));
         if (!clientConns[port])
            return;
         if (settings.tunnel && tunnel.toLowerCase() !== settings.tunnel)
            return;
         var sub = subscribers[port];
         if (sub) {
            sub.seen = Date.now();
            return;
         }
         sub = subscribers[port] = {conn: clientConns[port], seen: Date.now()};
         // A newcomer gets everything at once.
         for (var key in lastSent)
            send(sub, "/jtunnel/state", key, lastSent[key]);
         send(sub, "/jtunnel/alive");
      }));
      // Every sender's track, whichever tunnel the state goes to: a receiver of one tunnel sees
      // every track's.
      space.registerMethod("/jtunnel/expression", ",isss", "A JTunnel sender's track's pitch bend, pressure and notes",
                           guarded(function (source, msg) {
         var port = msg.getInt(0);
         if (!clientConns[port])
            return;
         var value;
         try {
            value = JSON.parse(String(msg.getString(3)));
         } catch (e) {
            return;
         }
         expressions[port] = {track: String(msg.getString(1)), color: String(msg.getString(2)), value: value,
                              seen: Date.now()};
      }));
      space.registerDefaultMethod(function () {});
      osc.createUdpServer(SCRIPT_PORT, space);
      for (var p = CLIENT_PORT_FIRST; p < CLIENT_PORT_FIRST + CLIENT_PORT_COUNT; p++)
         clientConns[p] = osc.connectToUdpServer("127.0.0.1", p, osc.createAddressSpace());
      host.errorln("JTunnel: OSC on UDP port " + SCRIPT_PORT); // errorln reaches Bitwig's log file
   } catch (e) {
      host.errorln("JTunnel: no OSC server: " + e);
   }
}

// ---- helpers ----

function later(fn, ms) {
   try {
      host.scheduleTask(fn, ms);
   } catch (e) {
      host.scheduleTask(fn, null, ms); // older API signature
   }
}

function guarded(fn) {
   return function () {
      try {
         return fn.apply(this, arguments);
      } catch (e) {
         host.errorln("JTunnel: script error: " + e);
      }
   };
}
