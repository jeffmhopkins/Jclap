// MatrixBrute preset reader for Bitwig Studio.
//
// Bitwig doesn't pass SysEx to plugins, so this controller script reads the synth's presets (the
// same read-only requests as tools/mb_dump.py) and hands them to the MatrixBrute CC plugin over a
// local connection (see startServer). A read of all 256 slots takes about a minute.
//
// Install: copy this file to Documents/Bitwig Studio/Controller Scripts/, then in Settings >
// Controllers add "Arturia > MatrixBrute presets" with the synth's "MatrixBrute" port as its input
// and output, in place of any generic controller on that port: Bitwig gives a port to one controller,
// so this one also passes the synth's MIDI on to tracks. (The synth's second USB port, MIDIIN2 /
// MIDIOUT2, answers the identity request but not preset reads.)
//
// READ ONLY: it sends the preset header/name read (03 19 bb ss 00), the data read (03 19 bb ss 01)
// and the chunk poll (01 18 00), nothing else.

loadAPI(18);

host.defineController("Arturia", "MatrixBrute presets", "0.1", "01787193-1328-48d6-a1b0-b9b2a3aea2af", "Jclap");
host.defineMidiPorts(1, 1);
host.addDeviceNameBasedDiscoveryPair(["MatrixBrute"], ["MatrixBrute"]);

var ARTURIA = "f000206b0601";
var CHUNKS = 50;
var TIMEOUT_MS = 1000;
var STEP24 = 32767 / 24;
var STEP12 = 32767 / 12;

// [cc, offset, kind, flag offset]: the byte map from tools/mb_bank.py (LAYOUT there).
var LAYOUT = [
   [9, 1240, "level"],
   [10, 1236, "level"],
   [12, 1238, "level"],
   [13, 1242, "level"],
   [66, 1130, "semi24"],
   [65, 1128, "center"],
   [67, 1202, "level"],
   [68, 1234, "level"],
   [69, 1232, "level"],
   [70, 1158, "level"],
   [14, 1246, "level"],
   [15, 1254, "level"],
   [16, 1244, "level"],
   [17, 1248, "level"],
   [72, 1134, "semi24"],
   [71, 1132, "center"],
   [73, 1204, "level"],
   [74, 1252, "level"],
   [75, 1250, "level"],
   [76, 1160, "level"],
   [22, 1136, "semi24"],
   [77, 1206, "level"],
   [78, 1210, "split", 1262],
   [79, 1208, "split", 1260],
   [80, 1212, "split", 1266],
   [18, 1094, "level"],
   [19, 1096, "level"],
   [20, 1098, "level"],
   [21, 1100, "level"],
   [89, 1102, "level"],
   [23, 1126, "level"],
   [83, 1198, "level"],
   [82, 1194, "level"],
   [81, 1192, "level"],
   [24, 1200, "center"],
   [7, 1230, "level"],
   [25, 1222, "level"],
   [87, 1224, "level"],
   [86, 1172, "level"],
   [85, 1170, "level"],
   [26, 1178, "center"],
   [8, 1220, "level"],
   [27, 1082, "center"],
   [102, 1032, "level"],
   [103, 1034, "level"],
   [28, 1036, "level"],
   [104, 1038, "level"],
   [94, 1162, "level"],
   [105, 1040, "level"],
   [106, 1042, "level"],
   [29, 1044, "level"],
   [107, 1046, "level"],
   [95, 1164, "level"],
   [108, 1056, "level"],
   [109, 1048, "level"],
   [110, 1050, "level"],
   [30, 1052, "level"],
   [111, 1054, "level"],
   [91, 1062, "level"],
   [90, 1068, "center"],
   [93, 1072, "level"],
   [92, 1078, "level"],
   [31, 1168, "level"],
   [112, 1166, "level"],
   [113, 1214, "level"],
   [114, 1216, "level"],
   [115, 1218, "level"],
   [5, 1116, "level"],
   [119, 1322, "semi12"],
   [11, 1180, "center"],
   [2, 1182, "center"],
   [4, 1190, "level"],
   [3, 1188, "level"]
];

var midiOut;
var seq = 0;
var job = null; // the read in progress
var token = 0; // bumped on every request and reply, so a stale timeout does nothing
var PORT = 58017;
var logLines = []; // everything logged, for log clients that connect later
var clients = []; // {conn, log: wants the log, data: wants presets}
var lastRead = []; // the last complete read's PRESET lines, and its DONE line
var reading = []; // the read in progress's PRESET lines

// A local TCP server (Bitwig's RemoteConnection) on PORT, for two kinds of client. Each message a
// client sends has a 4-byte big-endian length in front; what the script sends is plain lines.
//
//    "log"   the log so far, then every new line (tools/mb_bitwig_log.py): Bitwig only shows a
//            script's own output in its console
//    "data"  the presets, for the MatrixBrute CC plugin: the last complete read's lines straight
//            away, then every new read's as it goes
//    "read"  starts a read, as the Read button does; "stop" abandons one
//
// The data lines, tab-separated:
//    READING                          a read started
//    PRESET  n  name  cc:value,...    one named preset; n is 0-255 (A01-P16), values 0-127
//    DONE    found  failed            the read finished
//    FAILED  why                      the read stopped
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
   if (line.indexOf("PRESET") === 0)
      reading.push(line);
   else if (line.indexOf("READING") === 0)
      reading = [];
   else if (line.indexOf("DONE") === 0)
      lastRead = reading.concat([line]);
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
      var server = host.createRemoteConnection("MatrixBrute presets", PORT);
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
            if (text === "log") {
               client.log = true;
               for (var j = 0; j < logLines.length; j++)
                  sendLine(conn, logLines[j]);
            } else if (text === "data") {
               client.data = true;
               for (var k = 0; k < lastRead.length; k++)
                  sendLine(conn, lastRead[k]);
            } else if (text === "read") {
               readPresets(0, 255);
            } else if (text === "stop" && job) {
               job = null;
               data("FAILED\tstopped");
            }
         }));
      });
      log("server on port " + PORT);
   } catch (e) {
      println("no server: " + e);
   }
}

// Popups are the only output a user sees without opening the script console.
function say(text) {
   log(text);
   host.showPopupNotification("MatrixBrute: " + text);
}

// Every request and reply, for the log only: a read is about 30 of each a second.
function trace(text) {
   log(text);
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
         if (job)
            data("FAILED\tscript error: " + e);
         job = null;
      }
   };
}

function init() {
   midiOut = host.getMidiOutPort(0);
   var midiIn = host.getMidiInPort(0);
   midiIn.setSysexCallback(guarded(onSysex));
   // Bitwig gives a port to one controller only, so this one passes the synth's keys and knobs on
   // to tracks (as "MatrixBrute presets" in a track's input list), as a generic controller would.
   midiIn.createNoteInput("MatrixBrute", "??????");
   var state = host.getDocumentState();
   state.getSignalSetting("Read all presets", "MatrixBrute", "Read").addSignalObserver(guarded(function () {
      readPresets(0, 255);
   }));
   startServer();
   log("MatrixBrute presets ready");
}

function exit() {}

function hex2(v) {
   return (v < 16 ? "0" : "") + v.toString(16);
}

function bytesOf(hex) {
   var out = [];
   for (var i = 0; i < hex.length; i += 2)
      out.push(parseInt(hex.substr(i, 2), 16));
   return out;
}

// Sends cmd (command byte, then its arguments) with a fresh seq unless one is given.
function request(cmd, s) {
   if (s === undefined) {
      seq = (seq + 1) & 0x7f;
      s = seq;
   }
   var msg = ARTURIA + hex2(s) + hex2(cmd.length - 1);
   for (var i = 0; i < cmd.length; i++)
      msg += hex2(cmd[i]);
   job.seq = s;
   var mine = ++token;
   trace("sent " + msg + "f7");
   midiOut.sendSysex(msg + "f7");
   later(guarded(function () {
      if (job && token === mine) {
         log("timeout in stage " + job.stage + ", seq " + job.seq);
         fail("no reply");
      }
   }), TIMEOUT_MS);
}

function readPresets(first, last) {
   // Pressing Read again starts over, in case a read got stuck.
   token++;
   job = {preset: first, last: last, retried: false, found: 0, failed: 0};
   say("reading...");
   data("READING");
   startPreset();
}

function startPreset() {
   job.stage = "header";
   job.chunks = [];
   job.name = "";
   request([0x19, job.preset >> 4, job.preset & 15, 0x00]);
}

function fail(why) {
   if (!job.retried) {
      job.retried = true;
      startPreset();
      return;
   }
   log("MatrixBrute " + code(job.preset) + ": " + why + ", skipped");
   job.failed++;
   if (job.found === 0 && job.failed >= 3) {
      say("no answer from the synth" + (job.heard ? " (it sent " + job.heard + ")" : ""));
      data("FAILED\tno answer from the synth");
      job = null;
      return;
   }
   next();
}

function next() {
   job.retried = false;
   if (job.preset >= job.last) {
      say("read " + job.found + " named presets" + (job.failed ? ", " + job.failed + " got no answer" : ""));
      data("DONE\t" + job.found + "\t" + job.failed);
      job = null;
      return;
   }
   job.preset++;
   startPreset();
}

function onSysex(hex) {
   if (!job)
      return;
   hex = hex.replace(/ /g, "").toLowerCase();
   var r = bytesOf(hex);
   trace("got " + hex.substr(0, 30) + (hex.length > 30 ? "..." : ""));
   if (!job.heard)
      job.heard = hex.substr(0, 24); // shown if the read gives up: what did come back
   if (hex.substr(0, 12) !== ARTURIA || r[6] !== job.seq) {
      log("ignored (waiting for seq " + job.seq + "): " + hex.substr(0, 30));
      return;
   }
   // Matched on the reply code (r[8]) alone, as mb_dump.py does; r[7] is a length.
   var known = (job.stage === "header" && r[8] === 0x52) || (job.stage === "ready" && r[8] === 0x15) ||
               (job.stage === "chunks" && (r[8] === 0x16 || r[8] === 0x17));
   if (!known) {
      say("unexpected reply " + hex.substr(0, 30));
      return; // the request's timeout still runs
   }
   token++; // the timeout for this request is void
   if (job.stage === "header") {
      var name = "";
      for (var i = 21; i < r.length - 1 && r[i] !== 0; i++)
         name += String.fromCharCode(r[i]);
      job.name = name.trim();
      if (!job.name) {
         next(); // an empty slot
         return;
      }
      job.stage = "ready";
      request([0x19, job.preset >> 4, job.preset & 15, 0x01]);
   } else if (job.stage === "ready") {
      job.stage = "chunks";
      request([0x18, 0x00], job.seq);
   } else {
      job.chunks.push(r.slice(9, r.length - 1));
      if (r[8] === 0x16) {
         request([0x18, 0x00]);
         return;
      }
      if (job.chunks.length !== CHUNKS) {
         fail(job.chunks.length + " chunks");
         return;
      }
      // MCC sends one more poll after the last chunk, which the synth doesn't answer. Without it the
      // synth seems to stay in the transfer and ignore the next preset's requests.
      seq = (seq + 1) & 0x7f;
      midiOut.sendSysex(ARTURIA + hex2(seq) + "011800f7");
      job.found++;
      log(code(job.preset) + " " + job.name);
      if (job.found % 10 === 0)
         host.showPopupNotification("MatrixBrute: " + job.found + " presets...");
      deliver(job.preset, job.name, decode(unpack([].concat.apply([], job.chunks))));
      next();
   }
}

// 7-bit MIDI data packed 8 bytes to 7, top bits first (see tools/mb_bank.py).
function unpack(data) {
   var out = [];
   for (var g = 0; g + 8 <= data.length; g += 8)
      for (var i = 0; i < 7; i++)
         out.push(data[g + 1 + i] | (((data[g] >> (6 - i)) & 1) << 7));
   return out;
}

function word(u, offset, signed) {
   var w = u[offset] | (u[offset + 1] << 8);
   return signed && w >= 32768 ? w - 65536 : w;
}

// The middle CC value of those that round to the same semitone, as mb_bank.py picks it.
function middleOf(semis, range, offset) {
   var vs = [];
   for (var v = 0; v < 128; v++)
      if (Math.round((v - offset) * range / 127) === semis)
         vs.push(v);
   return vs[Math.floor(vs.length / 2)];
}

// Matches mb_bank.py's decode(). (Python rounds halves to even, Math.round up; no knob value lands on
// a half here, so they agree.)
function decode(u) {
   var values = [];
   for (var i = 0; i < LAYOUT.length; i++) {
      var cc = LAYOUT[i][0], offset = LAYOUT[i][1], kind = LAYOUT[i][2], v;
      if (kind === "level")
         v = word(u, offset) * 127 / 32767;
      else if (kind === "center")
         v = (word(u, offset, true) + 32768) / 516;
      else if (kind === "split") {
         var m = word(u, offset);
         v = (32768 + (word(u, LAYOUT[i][3]) !== 0 ? m : -m)) / 516;
      } else if (kind === "semi24")
         v = middleOf(Math.max(-24, Math.min(24, Math.round(word(u, offset, true) / STEP24))), 48, 63.5);
      else
         v = middleOf(Math.max(0, Math.min(12, Math.round(word(u, offset) / STEP12))), 12, 0);
      values.push([cc, Math.round(Math.min(127, Math.max(0, v)) * 128) / 128]);
   }
   return values;
}

function code(preset) {
   return String.fromCharCode(65 + (preset >> 4)) + ((preset & 15) < 9 ? "0" : "") + ((preset & 15) + 1);
}

function deliver(preset, name, values) {
   var cells = [];
   for (var i = 0; i < values.length; i++)
      cells.push(values[i][0] + ":" + values[i][1]);
   log(code(preset) + " " + name + ": " + values.length + " knobs");
   data("PRESET\t" + preset + "\t" + name.replace(/[\t\r\n]/g, " ") + "\t" + cells.join(","));
}
