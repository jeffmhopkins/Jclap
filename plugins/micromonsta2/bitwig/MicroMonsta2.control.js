// MicroMonsta 2 program reader for Bitwig Studio.
//
// Bitwig doesn't pass SysEx to plugins, so this controller script talks to the synth and hands its
// program dumps to the MicroMonsta 2 CC plugin by OSC on localhost (see startServer):
//
//    - the sound on part A or B, which the synth sends when asked (firmware 2.7 or later);
//    - a whole bank, which the synth only sends from its own panel: UTILITY > U02.
//
// Install: copy this file to Documents/Bitwig Studio/Controller Scripts/, then in Settings >
// Controllers add "Audiothingies > MicroMonsta 2 programs" with the synth's port as its input and
// output, in place of any generic controller on that port: Bitwig gives a port to one controller, so
// this one also passes the synth's MIDI on to tracks.
//
// READ ONLY: the one message it sends is the sound request, F0 00 21 22 4D 02 41 F7 (part A) or
// ... 42 F7 (part B). Nothing on the synth is changed or stored.

loadAPI(18);

host.defineController("Audiothingies", "MicroMonsta 2 programs", "0.2", "5b0e4d0c-3f7a-4b6e-9a51-2c8d7e1f6a43", "Jclap");
host.defineMidiPorts(1, 1);
host.addDeviceNameBasedDiscoveryPair(["MicroMonsta 2"], ["MicroMonsta 2"]);

var HEADER = "f00021224d02"; // every program message starts so; byte 6 is 01/02 (a part) or 03 (stored)
var SIZE = 176;
var SOUND_TIMEOUT_MS = 2000;
var DUMP_IDLE_MS = 3000; // a U02 dump is over when nothing more comes for this long
var PORT = 58018;

var midiOut;
var asking = null; // {ticket, part, token}: a sound request waiting for its answer
var token = 0; // bumped on every request, so a stale timeout does nothing
var dump = null; // {lines, count, seq}: a U02 dump coming in
var lastDump = []; // the last complete dump's lines, for subscribers that come later
var logLines = [];
var osc = null;
var subscribers = {}; // by UDP port: {conn, log: wants the log, data: wants programs, seen: ms}
var SUBSCRIBER_TIMEOUT_MS = 10000;

// Clients on this computer talk to the script by OSC over UDP on PORT, as the MatrixBrute script
// does. (Bitwig's TCP RemoteConnection never handed this script the plugin's messages.)
//
//    /mm2/hello  ,is  port kind     subscribe the UDP `port` on 127.0.0.1 to "log" or "data", and
//                                   say it's still there: a subscriber quiet for 10 s is dropped, so
//                                   clients repeat this every few seconds. The script answers with
//                                   /mm2/hello ,i 1, and on the first hello a "log" subscriber gets
//                                   the log so far and a "data" one the last complete dump.
//    /mm2/sound  ,is  ticket part   asks the synth for part "A"'s (or "B"'s) sound
//
// The script sends each line as /mm2/line ,s text. The data lines, tab-separated:
//    SOUND      N  hex          the answer to sound request N: one 176-byte program
//    FAILED     N  why          request N got no answer (N = 0: the dump went wrong)
//    RECEIVING                  the synth started sending programs nobody asked for (U02)
//    PROGRAM    hex             one of them, in the order sent (P001 first for a whole bank)
//    DONE       count           nothing more came: the dump is over
function log(text) {
   println(text);
   var line = new Date().toISOString().substr(11, 12) + " " + text;
   if (logLines.length < 20000)
      logLines.push(line);
   send("log", line);
}

function data(line) {
   if (dump && line.indexOf("SOUND") !== 0 && line.indexOf("FAILED") !== 0)
      dump.lines.push(line);
   send("data", line);
}

function send(kind, line) {
   var now = Date.now();
   for (var port in subscribers) {
      var sub = subscribers[port];
      if (now - sub.seen > SUBSCRIBER_TIMEOUT_MS) {
         delete subscribers[port];
         continue;
      }
      if (sub[kind])
         sendTo(sub, line);
   }
}

function sendTo(sub, line) {
   try {
      sub.conn.sendMessage("/mm2/line", line);
   } catch (e) {
      println("send: " + e);
   }
}

function startServer() {
   try {
      osc = host.getOscModule();
      var space = osc.createAddressSpace();
      space.registerMethod("/mm2/hello", ",is", "Subscribe a UDP port to the log or the programs", guarded(function (source, msg) {
         var port = msg.getInt(0), kind = String(msg.getString(1));
         if (port <= 0 || port > 65535 || (kind !== "log" && kind !== "data"))
            return;
         var sub = subscribers[port];
         var fresh = !sub || !sub[kind];
         if (!sub) {
            sub = subscribers[port] = {conn: osc.connectToUdpServer("127.0.0.1", port, osc.createAddressSpace()),
                                       log: false, data: false, seen: 0};
         }
         sub.seen = Date.now();
         sub[kind] = true;
         try {
            sub.conn.sendMessage("/mm2/hello", 1);
         } catch (e) {
            println("hello: " + e);
         }
         if (!fresh)
            return;
         var lines = kind === "log" ? logLines : lastDump;
         for (var i = 0; i < lines.length; i++)
            sendTo(sub, lines[i]);
      }));
      space.registerMethod("/mm2/sound", ",is", "Ask the synth for a part's sound", guarded(function (source, msg) {
         var ticket = msg.getInt(0), part = String(msg.getString(1));
         log("asked for part " + part + "'s sound (" + ticket + ")");
         if (part === "A" || part === "B")
            askSound(ticket, part);
      }));
      space.registerDefaultMethod(function (source, msg) {
         host.errorln("MicroMonsta 2 programs: unexpected OSC message " + msg.getAddressPattern() + " " + msg.getTypeTag());
      });
      osc.createUdpServer(PORT, space);
      log("OSC on UDP port " + PORT);
      host.errorln("MicroMonsta 2 programs: OSC on UDP port " + PORT); // errorln reaches Bitwig's log file
   } catch (e) {
      host.errorln("MicroMonsta 2 programs: no OSC server: " + e);
   }
}

function say(text) {
   log(text);
   host.showPopupNotification("MicroMonsta 2: " + text);
}

function later(fn, ms) {
   try {
      host.scheduleTask(fn, ms);
   } catch (e) {
      host.scheduleTask(fn, null, ms); // older API signature
   }
}

// Errors in callbacks are otherwise silent.
function guarded(fn) {
   return function () {
      try {
         fn.apply(null, arguments);
      } catch (e) {
         say("script error: " + e);
         host.errorln("MicroMonsta 2 programs: script error: " + e);
      }
   };
}

function init() {
   midiOut = host.getMidiOutPort(0);
   var midiIn = host.getMidiInPort(0);
   midiIn.setSysexCallback(guarded(onSysex));
   // Bitwig gives a port to one controller only, so this one passes the synth's keys and knobs on
   // to tracks (as "MicroMonsta 2 programs" in a track's input list), as a generic controller would.
   midiIn.createNoteInput("MicroMonsta 2", "??????");
   var state = host.getDocumentState();
   state.getSignalSetting("Read part A's sound", "MicroMonsta 2", "Read A").addSignalObserver(guarded(function () {
      askSound(0, "A");
   }));
   startServer();
   log("MicroMonsta 2 programs ready");
}

function exit() {}

function askSound(ticket, part) {
   var mine = ++token;
   asking = {ticket: ticket, part: part, token: mine};
   var msg = HEADER + (part === "A" ? "41" : "42") + "f7";
   log("sent " + msg);
   midiOut.sendSysex(msg);
   later(guarded(function () {
      if (asking && asking.token === mine) {
         asking = null;
         say("no answer from the synth for part " + part);
         data("FAILED\t" + ticket + "\tno answer from the synth (it needs firmware 2.7 or later)");
      }
   }), SOUND_TIMEOUT_MS);
}

function onSysex(hex) {
   hex = hex.replace(/ /g, "").toLowerCase();
   log("got " + hex.substr(0, 40) + (hex.length > 40 ? "..." : "") + " (" + hex.length / 2 + " bytes)");
   if (hex.substr(0, 12) !== HEADER || hex.length !== 2 * SIZE || hex.substr(hex.length - 2) !== "f7")
      return;
   // A program while a request waits (and no dump is coming in) is its answer; anything else is a
   // dump from the synth's panel.
   if (asking && !dump) {
      var a = asking;
      asking = null;
      log("part " + a.part + "'s sound: " + name(hex));
      data("SOUND\t" + a.ticket + "\t" + hex);
      return;
   }
   if (!dump) {
      dump = {lines: [], count: 0, seq: 0};
      data("RECEIVING");
      say("receiving programs...");
   }
   dump.count++;
   data("PROGRAM\t" + hex);
   if (dump.count % 50 === 0)
      host.showPopupNotification("MicroMonsta 2: " + dump.count + " programs...");
   var d = dump, mine = ++d.seq;
   later(guarded(function () {
      if (dump === d && d.seq === mine)
         finishDump();
   }), DUMP_IDLE_MS);
}

function finishDump() {
   var d = dump;
   dump = null;
   data("DONE\t" + d.count);
   lastDump = d.lines.concat(["DONE\t" + d.count]);
   say("received " + d.count + " programs");
}

function name(hex) {
   var s = "";
   for (var i = 8; i < 16; i++)
      s += String.fromCharCode(parseInt(hex.substr(2 * i, 2), 16));
   return s.trim();
}
