// MicroMonsta 2 program reader for Bitwig Studio.
//
// Bitwig doesn't pass SysEx to plugins, so this controller script talks to the synth and hands its
// program dumps to the MicroMonsta 2 CC plugin over a local connection (see startServer):
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

host.defineController("Audiothingies", "MicroMonsta 2 programs", "0.1", "5b0e4d0c-3f7a-4b6e-9a51-2c8d7e1f6a43", "Jclap");
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
var lastDump = []; // the last complete dump's lines, for clients that connect later
var logLines = [];
var clients = []; // {conn, log: wants the log, data: wants programs}

// A local TCP server (Bitwig's RemoteConnection) on PORT. Each message a client sends has a 4-byte
// big-endian length in front; what the script sends is plain lines.
//
//    "log"            the log so far, then every new line
//    "data"           the programs, for the MicroMonsta 2 CC plugin: the last complete dump's lines
//                     straight away, then everything new as it comes
//    "sound N A"      asks the synth for part A's (or B's) sound; N is the client's ticket
//
// The data lines, tab-separated:
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
   for (var i = 0; i < clients.length; i++)
      if (clients[i].log)
         sendLine(clients[i].conn, line);
}

function data(line) {
   if (dump && line.indexOf("SOUND") !== 0 && line.indexOf("FAILED") !== 0)
      dump.lines.push(line);
   for (var i = 0; i < clients.length; i++)
      if (clients[i].data)
         sendLine(clients[i].conn, line);
}

function sendLine(conn, line) {
   var bytes = [];
   for (var i = 0; i < line.length; i++)
      bytes.push(line.charCodeAt(i) & 0x7f);
   bytes.push(10);
   try {
      conn.send(bytes);
   } catch (e) {
      println("client: " + e);
   }
}

function startServer() {
   try {
      var server = host.createRemoteConnection("MicroMonsta 2 programs", PORT);
      server.setClientConnectCallback(function (conn) {
         var client = {conn: conn, log: false, data: false};
         clients.push(client);
         conn.setDisconnectCallback(function () {
            clients.splice(clients.indexOf(client), 1);
         });
         conn.setReceiveCallback(guarded(function (bytes) {
            var text = "";
            for (var i = 0; i < bytes.length; i++)
               text += String.fromCharCode(bytes[i] & 0xff);
            text = text.replace(/[^ -~]/g, "").trim();
            log("client sent: " + text);
            var words = text.split(" ");
            if (text === "log") {
               client.log = true;
               for (var j = 0; j < logLines.length; j++)
                  sendLine(conn, logLines[j]);
            } else if (text === "data") {
               client.data = true;
               for (var k = 0; k < lastDump.length; k++)
                  sendLine(conn, lastDump[k]);
            } else if (words[0] === "sound" && words.length === 3 && (words[2] === "A" || words[2] === "B")) {
               askSound(parseInt(words[1], 10), words[2]);
            }
         }));
      });
      log("server on port " + PORT);
   } catch (e) {
      println("no server: " + e);
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
   return function (a) {
      try {
         fn(a);
      } catch (e) {
         say("script error: " + e);
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
