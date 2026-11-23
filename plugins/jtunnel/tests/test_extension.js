// The Bitwig extension's scene-launch and track-selection spotting (bitwig/JTunnel.control.js,
// launchState and selectState), run in node with Bitwig's API stubbed out.
// Usage: node test_extension.js <path to JTunnel.control.js>
const vm = require('vm'), fs = require('fs'), assert = require('assert');
const src = fs.readFileSync(process.argv[2], 'utf8');
let t = 1000;
const ctx = { loadAPI(){}, host: { defineController(){}, defineMidiPorts(){} }, Date: { now: () => t }, JSON, Math, String };
vm.createContext(ctx); vm.runInContext(src, ctx);
ctx.settings.tracks = 2;
const items = [{index:0,name:"Intro"},{index:1,name:"Drop"}];
function keys(states){ // states: [[trackIndex, [[scene,state,queued],...]], ...]
  const k = {};
  states.forEach((tr,i)=>{ k["track/"+i] = {exists:true,index:tr[0],slots:tr[1].map(s=>({scene:s[0],state:s[1],queued:s[2]}))}; });
  return k;
}
const L = (st)=>ctx.launchState(keys(st), items);
// baseline: nothing fires
let r = L([[0,[[0,"stopped",null],[1,"stopped",null]]],[1,[[0,"stopped",null],[1,"stopped",null]]]]);
assert.strictEqual(r.count,0); assert.strictEqual(r.scene,-1);
// launch scene 1: both tracks queued
t+=33; r = L([[0,[[0,"stopped",null],[1,"stopped","play"]]],[1,[[0,"stopped",null],[1,"stopped","play"]]]]);
assert.deepStrictEqual([r.scene,r.name,r.count,r.clips],[1,"Drop",1,2]);
// playing starts: no new launch
t+=500; r = L([[0,[[0,"stopped",null],[1,"playing",null]]],[1,[[0,"stopped",null],[1,"playing",null]]]]);
assert.deepStrictEqual([r.scene,r.name,r.count,r.lastName],[-1,"",1,"Drop"]);
// relaunch same scene while playing (quantized): queued again -> fires again
t+=500; r = L([[0,[[0,"stopped",null],[1,"playing","play"]]],[1,[[0,"stopped",null],[1,"playing","play"]]]]);
assert.deepStrictEqual([r.scene,r.name,r.count],[1,"Drop",2]);
// scroll: a new track index appears already playing -> no launch
t+=500; r = L([[1,[[0,"stopped",null],[1,"playing",null]]],[2,[[0,"stopped",null],[1,"playing",null]]]]);
assert.strictEqual(r.count,2);
// unquantized launch of scene 0: idle -> playing
t+=33; r = L([[1,[[0,"playing",null],[1,"stopped",null]]],[2,[[0,"playing",null],[1,"stopped",null]]]]);
assert.deepStrictEqual([r.scene,r.name,r.count],[0,"Intro",3]);
// split over two ticks joins
t+=500; L([[1,[[0,"playing",null],[1,"stopped","play"]]],[2,[[0,"playing",null],[1,"stopped",null]]]]);
t+=33; r = L([[1,[[0,"playing",null],[1,"stopped","play"]]],[2,[[0,"playing",null],[1,"stopped","play"]]]]);
assert.deepStrictEqual([r.scene,r.count,r.clips],[1,4,2]);
// Track selection: the first tick notes, a change fires and pulses, reselecting later fires again.
let sel = ctx.selectState(2, "Bass");
assert.deepStrictEqual([sel.track, sel.count], [-1, 0]);
t += 33; sel = ctx.selectState(4, "Keys");
assert.deepStrictEqual([sel.track, sel.name, sel.count], [4, "Keys", 1]);
t += 500; sel = ctx.selectState(4, "Keys");
assert.deepStrictEqual([sel.track, sel.name, sel.lastTrack, sel.lastName], [-1, "", 4, "Keys"]);
t += 33; sel = ctx.selectState(2, "Bass");
t += 500; ctx.selectState(2, "Bass");
t += 33; sel = ctx.selectState(4, "Keys");
assert.deepStrictEqual([sel.track, sel.name, sel.count], [4, "Keys", 3]);
// Nothing selected fires nothing.
t += 500; sel = ctx.selectState(-1, "");
assert.deepStrictEqual([sel.track, sel.count], [-1, 3]);
console.log("ok");
