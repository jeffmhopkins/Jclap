#pragma once

// The dice beside the prompt: a random prompt for a role, put together from a large bank of words.
// The STRANGE slider runs from plain studio drum sounds (0) to objects, places and processes no
// drum machine has (1). Halfway mixes both: a familiar sound with one odd twist.
//
// A prompt reads the way the model was trained to read them: the sound first, then its character,
// then how it was recorded or treated, comma separated, e.g.
//
//    "dusty lo-fi snare, tape saturation, short room tail, 90s boom bap"     (strange 0)
//    "snare made of a slammed filing cabinet, gated, plate reverb"           (strange 0.5)
//    "a glass of ice dropped on marble, granular smear, played backwards"    (strange 1)
//
// Every list is one role's own or shared by all; the same seed makes the same prompt.

#include <cstdint>
#include <string>
#include <vector>

namespace jkit {

enum Role : int { kKick, kSnare, kHat, kClap, kTom, kPerc, kFx, kRoleCount };
inline const char *const kRoleNames[kRoleCount] = {"Kick", "Snare", "Hat", "Clap", "Tom", "Perc", "FX"};

namespace prompts {

using List = std::vector<const char *>;

struct RoleWords {
   List sounds;  // the plain sound: "808 kick"
   List traits;  // what it's like: "punchy"
   List oddities; // the strange sound itself: "a dropped bowling ball"
};

inline const RoleWords &roleWords(Role role) {
   static const RoleWords kWords[kRoleCount] = {
      // Kick
      {{"kick drum", "808 kick", "909 kick", "acoustic kick", "bass drum", "techno kick", "house kick",
        "trap kick", "boom bap kick", "rock kick", "jazz kick drum", "sub kick", "distorted kick", "hardstyle kick",
        "lo-fi kick", "dub kick", "garage kick", "drum and bass kick", "electro kick", "analog kick",
        "kick with a long tail", "short kick", "layered kick", "vinyl kick", "funk kick", "metal kick drum",
        "breakbeat kick", "afrobeat kick", "disco kick", "minimal kick", "deep kick", "gabber kick", "UK drill kick",
        "reggaeton kick", "industrial kick", "synthwave kick", "kick drum hit", "felt beater kick",
        "Linn kick", "TR-707 kick", "TR-606 kick", "CR-78 kick", "DMX kick", "SP-1200 kick", "MPC kick", "Motown kick", "soul kick", "hip hop kick", "lo-fi hip hop kick", "jersey club kick", "Afro house kick", "amapiano log drum", "psytrance kick", "hardcore kick", "trance kick", "progressive house kick", "tech house kick", "dembow kick", "baile funk kick", "phonk kick", "Miami bass kick", "punk kick drum", "indie rock kick", "stadium rock kick", "kick with a beater click", "kick with a pitch sweep", "tuned 808 kick", "rumble kick", "muted kick drum", "ported kick drum", "double bass pedal kick", "marching bass drum", "orchestral bass drum", "gran cassa hit", "big band kick", "kick and sub layered"},
       {"punchy", "deep", "tight", "boomy", "clicky", "warm", "fat", "subby", "round", "hard hitting", "thumpy",
        "dry", "muffled", "crisp", "heavy", "dusty", "clean", "saturated", "snappy", "booming", "rumbling",
        "short", "long decaying", "low tuned", "high tuned", "soft", "aggressive", "vintage", "modern", "lo-fi",
        "compressed", "solid", "woody", "papery", "pillowy", "huge", "gritty", "bouncy",
        "knocking", "smacking", "sub heavy", "mid heavy", "thick", "chesty", "resonant", "rubbery", "pumping", "driving", "flat", "hollow", "plump", "dense", "bassy", "sharp attack", "soft attack", "clipped", "overdriven", "crunchy", "glued", "fluffy", "weighty", "rolling", "throbbing", "short tailed", "long tailed", "knuckly", "massive", "modest", "muted", "airy", "beefy", "tubby", "bulbous", "hefty", "knocky", "sculpted"},
       {"a door slammed in a cathedral", "a heartbeat inside a whale", "a bowling ball dropped on a stage",
        "a fist on a wooden coffin", "a beanbag thrown at a wall", "a car door closing underwater",
        "a suitcase falling down stairs", "a pumpkin hitting the ground", "a thunderclap folded into one beat",
        "a giant stepping on a trampoline", "a pillow punched in a tunnel", "a medicine ball on a gym floor",
        "a cannon fired inside a mattress", "a timpani struck with a sledgehammer", "a sack of flour landing",
        "a submarine hull being knocked on", "an elephant's footstep", "a basketball bounced in a parking garage",
        "a dropped cast iron pan", "a rubber mallet on an oil drum", "a skateboard landing on concrete",
        "a tree stump hit with an axe", "a bass speaker pushed until it tears", "the boom of a distant quarry blast",
        "a car crash muffled by snow", "a sofa dropped from a truck", "a hammer on a mattress", "a hippo's footstep in mud", "a ship's horn squeezed into one thump", "a church organ's lowest pedal stabbed", "a fridge door slammed", "a trunk lid slammed in a garage", "a meteor landing in a field", "a boxing bag hit in an empty gym", "a wrecking ball against a wall", "a sumo wrestler landing", "a log rolled off a truck", "a dumpster lid dropped", "a body falling on a gym mat", "a tractor tire dropped", "a water tower struck like a drum", "a wooden barrel kicked in a cellar", "a parachute bag landing", "a hay bale dropped", "a fist on a steel shipping container", "a pile driver far away", "a cow kicking a barn door", "a mattress dropped down a stairwell"}},
      // Snare
      {{"snare", "snare drum", "808 snare", "909 snare", "acoustic snare", "rimshot snare", "trap snare",
        "boom bap snare", "breakbeat snare", "piccolo snare", "brushed snare", "rock snare", "funk snare",
        "jungle snare", "house snare", "lo-fi snare", "gated snare", "marching snare", "dubstep snare",
        "drum and bass snare", "garage snare", "snare with snappy wires", "snare roll hit", "electro snare",
        "noise snare", "layered snare and clap", "vinyl snare", "reggae snare", "UK garage snare", "pop snare",
        "jazz snare", "ghost note snare", "cross stick", "synth snare", "metal snare", "Motown snare",
        "LinnDrum snare", "TR-707 snare", "DMX snare", "CR-78 snare", "SP-1200 snare", "MPC snare", "R&B snare", "neo soul snare", "trip hop snare", "phonk snare", "drill snare", "jersey club snare", "dembow snare", "amen break snare", "think break snare", "80s gated snare", "power ballad snare", "metal snare drum", "punk snare", "orchestral snare", "field drum", "snare with no wires", "snare flam", "rim click", "snare with a long reverb tail", "deep brass snare", "wooden snare", "steel snare", "chopped breakbeat snare", "pitched up snare", "stacked snare", "trap snare roll", "lo-fi hip hop snare", "afrobeat snare", "soca snare", "Bollywood snare"},
       {"crisp", "fat", "tight", "snappy", "cracking", "dusty", "warm", "bright", "dark", "punchy", "papery",
        "ringing", "dead", "muffled", "loud", "dry", "wet", "vintage", "modern", "trashy", "crunchy", "airy",
        "deep", "tuned low", "tuned high", "short", "long", "rattling", "woody", "metallic", "clean", "smashed",
        "compressed", "lo-fi", "sharp", "full bodied", "thin", "explosive",
        "fluffy", "wiry", "buzzy", "knocky", "clangy", "boxy", "honky", "fat bottomed", "sizzly", "cutting", "slappy", "whip-like", "dense", "poppy", "pingy", "choked", "open", "roomy", "distant", "close", "gritty", "overdriven", "clipped", "hard", "soft", "ghostly", "subtle", "aggressive", "gated", "wide", "mono", "splashy", "heavy", "tape worn", "loose wired", "tight wired", "midrangey", "bodied"},
       {"a filing cabinet slammed shut", "a stack of cardboard boxes falling", "a whip cracked in a stairwell",
        "a newspaper slapped on a table", "a bag of gravel dropped", "a gunshot in a canyon heard from far away",
        "a book closed hard in a library", "a tin roof hit by a stone", "a sheet of plywood snapping",
        "a firecracker in a metal bin", "a tennis racket hitting a fence", "a cymbal stand knocked over",
        "a balloon popped in a bathroom", "a rubber band snapped on a guitar", "a crate of bottles set down",
        "a karate chop on a wooden board", "an ice sheet cracking", "a typewriter's carriage return",
        "a chair dropped in an empty hall", "a cardboard tube hit on a desk", "rain hitting a snare in a storm",
        "a padlock slammed on a gate",
        "a laptop lid slammed", "a tape measure snapping back", "a cardboard box stomped on", "a car door slammed in a tunnel", "a starter pistol at a race", "a wet rag slapped on concrete", "a desk drawer slammed", "a crate of tin cans tipped over", "a window slapped with a palm", "a plastic chair kicked", "a soda can crushed underfoot", "a dictionary dropped on the floor", "a frozen branch snapping", "a stack of trays dropped in a cafeteria", "a garage door hitting the ground", "hail on a car roof in one burst", "a sheet of steel struck with a broom", "a tennis ball hit hard against a wall", "a hockey puck on the boards", "a piston firing in an engine", "a fly swatter on a table", "a cupboard door banged shut"}},
      // Hat
      {{"closed hi-hat", "open hi-hat", "hi-hat", "808 hi-hat", "909 hi-hat", "pedal hi-hat", "trap hi-hat",
        "shaker hat", "ride cymbal", "crash cymbal", "cymbal hit", "splash cymbal", "china cymbal", "tambourine hit",
        "house open hat", "techno hat", "jazz ride bell", "hat with a short decay", "half open hi-hat",
        "lo-fi hi-hat", "electro hi-hat", "garage hat", "disco open hat", "noise hat", "metallic tick",
        "brushed cymbal", "sizzle cymbal", "ride bell", "dusty hat", "acoustic hi-hat",
        "606 hi-hat", "707 hi-hat", "LinnDrum hi-hat", "CR-78 hat", "DMX hat", "trap hat roll", "drill hat", "afrobeat shaker", "hip hop hat", "boom bap hat", "funk hi-hat", "rock hi-hat", "jazz hi-hat", "foot hi-hat chick", "bell of a ride cymbal", "ride cymbal ping", "choked crash", "cymbal swell", "reverse cymbal", "egg shaker", "sleigh bells", "chimes", "cabasa", "tambourine shake", "dark ride", "flat ride", "stacked cymbals", "trashy hat", "techno open hat", "breakbeat hat"},
       {"crisp", "sizzling", "shimmering", "tight", "airy", "bright", "dark", "trashy", "washy", "ticking",
        "short", "long", "sharp", "soft", "metallic", "noisy", "glassy", "clean", "dirty", "dusty", "vintage",
        "modern", "splashy", "thin", "dry", "ringing", "breathy", "hissy", "sparkling", "chopped", "filtered",
        "silky", "gritty", "delicate", "cutting",
        "fizzy", "chippy", "ticky", "sibilant", "smooth", "pingy", "whispery", "biting", "clangy", "jangly", "tinny", "bell-like", "harsh", "mellow", "lush", "crispy", "sandy", "grainy", "feathery", "quick", "choked", "decaying", "resonant", "bouncy", "shuffled", "swung", "bright topped", "rolled off", "pitched", "sloshy", "sharp", "digital", "analog", "papery", "icy"},
       {"keys shaken in a fist", "a bead curtain swept by hand", "sand thrown on a snare", "a cicada's click",
        "sparks from a grinder", "a spray can's hiss", "a bicycle bell's tick", "a sprinkler's ratchet",
        "coins spilling on glass", "a zipper pulled fast", "frost cracking on a window", "radio static cut short",
        "a can of soda opened", "a broom on a tiled floor", "rice falling on a cymbal", "a pocket watch ticking",
        "a crushed potato chip bag", "a matchstick struck", "steam escaping a valve", "a lock picked open",
        "a stapler closing", "wind chimes muted by hand",
        "a sprinkler hitting a tin shed", "sizzling bacon cut short", "a lighter flicked", "an aerosol puff", "hair clippers starting", "a can of nails shaken", "gravel kicked across a road", "a deck of cards riffled", "a thumb across a comb", "a bottle of pills shaken", "a crisp packet scrunched", "a rattlesnake's warning", "the fizz of a fresh soda", "a tiny cymbal made of tinfoil", "a snare wire plucked", "a cash register drawer", "a seed pod shaken", "rain on a tarp in one gust", "a jar of buttons tipped", "a dentist's drill touch", "sleet on a window", "a keychain dropped on tile"}},
      // Clap
      {{"clap", "hand clap", "808 clap", "909 clap", "group clap", "layered clap", "snap", "finger snap",
        "trap clap", "house clap", "gospel clap", "crowd clap", "single hand clap", "reverb clap", "lo-fi clap",
        "electro clap", "stomp and clap", "flamenco clap", "disco clap", "garage clap", "big room clap",
        "knock", "slap", "body slap", "thigh slap", "table slap", "noise clap", "vintage drum machine clap",
        "LinnDrum clap", "DMX clap", "707 clap", "reverb tail clap", "big reverb clap", "tight clap", "R&B clap", "afrobeat clap", "dembow clap", "drill clap", "jersey club clap", "UK funky clap", "reggae clap", "trap snap", "finger snap with reverb", "double clap", "audience clap", "stadium clap", "studio clap", "chest slap", "palm slap", "knee slap", "rimshot clap", "clap and snap layered", "gated clap", "lo-fi hip hop clap", "pitched clap", "clap stack"},
       {"tight", "wide", "loud", "crisp", "dry", "roomy", "short", "long", "fat", "thin", "bright", "dark",
        "snappy", "human", "stacked", "smeared", "punchy", "warm", "crunchy", "dusty", "clean", "sloppy",
        "flammed", "echoing", "crowded", "lonely", "sharp", "soft", "papery", "fleshy",
        "spread", "doubled", "layered", "tight timed", "loose", "organic", "synthetic", "noisy", "airy", "glossy", "grainy", "smacky", "plasticky", "clicky", "gated", "echoey", "reverberant", "close", "distant", "big", "small", "hollow", "full", "quick", "vintage", "modern", "slappy", "bouncy", "lively", "juicy"},
       {"a hundred pigeons taking off at once", "two books clapped together", "a wet towel snapped",
        "a hand slapping a car hood", "applause in an empty stadium", "bubble wrap stomped on",
        "flip flops slapping on tiles", "a ping pong ball hitting a paddle", "a fish flopping on a deck",
        "a sail filling with wind", "a pair of oven gloves slapped together", "a cardboard box stamped flat",
        "hailstones on a tent", "a cork popped in a cellar", "a mousetrap snapping shut", "a fan of cards shuffled",
        "an umbrella snapped open", "a belt cracked in a closet",
        "pages of a book flipped at once", "a wet fish slapped on a counter", "a paddle slapping water", "two planks smacked together", "a bedsheet shaken out", "a wave hitting a pier", "a hand on a leather sofa", "a burst of popcorn", "two wet sponges slapped", "a gym mat slapped", "a beach ball smacked", "a high five in a hallway", "a stack of envelopes dropped", "a goose flapping its wings", "a raincoat snapped dry", "a crowd clapping in a cave", "a slap on a drum skin underwater", "a tarp snapping in the wind"}},
      // Tom
      {{"tom", "floor tom", "rack tom", "high tom", "low tom", "mid tom", "808 tom", "909 tom", "electronic tom",
        "Simmons tom", "concert tom", "roto tom", "tom fill hit", "tribal tom", "taiko drum", "surdo",
        "timpani", "jazz tom", "rock tom", "disco tom", "syndrum", "tuned tom", "deep floor tom",
        "lo-fi tom", "70s tom", "80s tom", "big tom with a pitch drop", "frame drum", "djembe bass",
        "LinnDrum tom", "DMX tom", "CR-78 tom", "707 tom", "tom with a long sustain", "concert bass drum", "taiko hit", "dhol", "tabla bass", "ashiko", "talking drum", "low conga", "tumba", "bass tom", "tom with a pitch bend", "rototom fill", "electronic drum pad tom", "disco syndrum", "boom bap tom", "funk tom", "rock floor tom", "metal tom", "jazz floor tom", "tribal drum", "war drum", "marching tenor drum", "bodhran", "low frame drum", "doumbek"},
       {"deep", "boomy", "round", "punchy", "resonant", "dead", "tuned", "melodic", "warm", "fat", "short",
        "long", "dry", "roomy", "big", "small", "dark", "bright", "pitch dropping", "booming", "woody", "dusty",
        "clean", "vintage", "huge", "thunderous", "soft", "muted",
        "ringing", "sustaining", "bending", "sweeping", "open", "closed", "muffled", "full", "throaty", "chesty", "rich", "hollow", "tribal", "primal", "mallet struck", "stick struck", "hand struck", "pitched down", "pitched up", "bouncy", "thick", "tight", "loose headed", "dense", "dusty", "lo-fi", "rumbling", "singing"},
       {"a water cooler bottle struck", "a hollow log hit with a club", "a rain barrel thumped",
        "a cardboard drum in a forest", "a pumpkin knocked on", "a wooden crate kicked", "a basketball dribbled once",
        "a sealed paint can hit", "a gourd floating in water", "a hot air balloon's burner thump",
        "a church door knocked on", "a ship's hull knocked from inside", "a giant's knuckle on a table",
        "a coconut cracked open", "a beer keg thumped",
        "a plastic bucket hit upside down", "a cardboard box thumped", "a guitar body knocked", "an oil drum half full of water", "a hollow tree trunk", "a pot struck with a wooden spoon", "a stone dropped down a well", "a bass drum in a canyon", "a door knocker on oak", "a kettle drum made of ice", "a gong muted with a blanket", "a water tank thumped", "a wardrobe knocked from inside", "a canoe hull drummed", "a bale of hay punched"}},
      // Perc
      {{"percussion hit", "conga", "bongo", "cowbell", "rimshot", "woodblock", "claves", "shaker", "tambourine",
        "cabasa", "guiro scrape", "agogo bell", "timbale", "cajon hit", "tabla", "darbuka", "udu", "triangle",
        "temple block", "castanets", "maracas", "guiro", "log drum", "steel drum hit", "marimba hit", "kalimba hit",
        "vibraslap", "808 cowbell", "808 rim", "909 rim", "metal percussion", "glass percussion", "click", "blip",
        "zap", "anvil hit", "slit drum", "cuica", "berimbau", "pandeiro",
        "shekere", "cajon slap", "cajon bass", "conga slap", "bongo slap", "timbale rim", "jam block", "metal pipe hit", "bell tree", "finger cymbals", "tingsha", "gamelan hit", "gong hit", "hang drum", "handpan", "glockenspiel hit", "vibraphone hit", "xylophone hit", "steel tongue drum", "mbira", "sidestick", "ratchet", "whip", "slapstick", "flexatone", "ocean drum", "rainstick", "spring hit", "jingle", "tabla dayan", "kanjira", "ghatam", "caxixi", "afuche", "rattle", "bell", "shaker loop hit", "triangle mute", "wood clap", "temple bell"},
       {"tuned", "wooden", "metallic", "bright", "dark", "short", "tight", "dry", "resonant", "clicky", "plucky",
        "glassy", "hollow", "ringing", "muted", "warm", "crisp", "dusty", "clean", "lo-fi", "playful", "organic",
        "rattling", "earthy", "tropical", "exotic", "staccato", "shiny", "sharp", "soft",
        "pingy", "knocky", "bouncy", "snappy", "springy", "jingly", "tinny", "resonating", "shimmery", "bell-like", "boxy", "rounded", "pointed", "pitched up", "pitched down", "clacky", "crackly", "sparkly", "rubbery", "plasticky", "folk", "tribal", "cinematic", "electronic", "analog", "digital", "quirky", "funky", "groovy", "latin"},
       {"a spoon on a wine glass", "a marble dropped in a bowl", "a ruler twanged on a desk",
        "a tap on a fish tank", "a key on a radiator", "a coin spun on a table", "a cork pulled from a bottle",
        "a droplet into a bucket", "chopsticks on a teapot", "a pebble on a frozen lake",
        "a bottle cap flicked", "a xylophone made of bones", "a typewriter key", "a pencil on a skull",
        "a toy gun's click", "a light switch flicked", "a carabiner snapping shut", "a tongue click in a cave",
        "an abacus bead sliding", "a ceramic tile tapped", "a fork on a radiator pipe", "a clock spring released",
        "a nail dropped in a glass", "a bike spoke plucked", "a bottle blown across", "a ping pong ball on concrete", "a lid spun on a jar", "dice rolled in a cup", "a seashell knocked", "a teaspoon in a mug", "a tuning fork struck", "a garden hose nozzle clicked", "a marble rolled on a xylophone", "a stone skipped on ice", "a whistle cut short", "a cassette clicked in", "a snapping turtle's jaw", "a ruler flicked off a table", "a drop of oil in a hot pan", "a glass bead dropped on wood", "a typewriter bell", "a fridge magnet snapping on", "a pen clicked", "a domino falling"}},
      // FX
      {{"riser", "downlifter", "impact", "sweep", "noise sweep", "reverse cymbal", "white noise riser",
        "sub drop", "boom impact", "laser zap", "glitch", "whoosh", "tape stop", "vinyl scratch", "vinyl crackle",
        "transition effect", "uplifter", "bass drop", "cinematic hit", "braam", "stutter", "radio tuning",
        "synth zap", "alarm stab", "siren", "air horn", "rewind effect", "swell", "metallic swell", "drone hit",
        "reverse snare", "pitch rise", "pitch dive", "dial up noise", "rumble", "atmosphere hit",
        "reverse crash", "cinematic boom", "trailer hit", "sub boom", "low end drop", "filter sweep", "noise burst", "white noise impact", "reverse reverb", "tape rewind", "vinyl brake", "digital glitch", "bitcrush sweep", "granular burst", "laser", "phaser sweep", "synth riser", "build up", "drum roll riser", "snare roll riser", "chord stab", "orchestra hit", "vocal chop", "reversed scream", "explosion", "thunder clap", "wind gust", "water splash", "bubble burst", "spaceship pass by", "robot voice", "dial tone", "radio noise", "UFO", "power down", "power up"},
       {"huge", "cinematic", "dark", "bright", "rising", "falling", "sweeping", "glitchy", "warped", "distorted",
        "wide", "long", "short", "eerie", "epic", "airy", "metallic", "noisy", "filtered", "evolving",
        "ominous", "euphoric", "tense", "spacey", "sci-fi", "retro", "lo-fi", "massive", "shimmering", "grainy",
        "sucking", "breathy",
        "swirling", "pulsing", "stuttering", "shattering", "exploding", "imploding", "dreamy", "hazy", "nostalgic", "futuristic", "dystopian", "mysterious", "dramatic", "aggressive", "gentle", "floating", "turbulent", "whispering", "roaring", "hypnotic", "psychedelic", "harsh", "icy", "warm", "glowing", "fractured", "smeared", "wobbling", "modulated", "resonant", "phased", "bitcrushed"},
       {"a black hole swallowing a choir", "a jet engine spooling down in a cave", "a whale song sped up",
        "a modem falling in love", "a lightsaber dropped in a pool", "a train passing through a wormhole",
        "a glacier calving in slow motion", "a robot's sigh", "a satellite burning up", "a bee swarm in a tin can",
        "a cassette tape eaten by a deck", "a kettle reaching boil in a cathedral", "a firework seen from space",
        "a ghost dragging a chain", "a door into another dimension opening", "a vacuum cleaner played backwards",
        "the end of a dial tone", "a theremin melting", "a tornado heard through a wall",
        "a power station switching off", "a pinball machine tilting", "an elevator falling past floors",
        "a cathedral organ collapsing", "a satellite dish humming in a storm", "a dial-up modem screaming in a cave", "a tidal wave in reverse", "a crowd gasping in slow motion", "a planet cracking open", "a possessed fax machine", "a jet flying through a tunnel", "a dying star", "a glass harmonica shattering", "a robot powering on after a century", "a portal slamming shut", "a magnetic storm on a radio", "a time machine landing", "a swarm of drones passing", "a tape loop melting", "lightning hitting a lake", "a gravity well opening", "a laser cutting through ice", "a choir inhaling", "sonar in an ocean trench", "an avalanche heard from inside"}},
   };
   return kWords[role < 0 || role >= kRoleCount ? 0 : role];
}

// How it was recorded or treated, for any role.
inline const List &treatments() {
   static const List k = {"tape saturation", "light compression", "heavy compression", "transient shaped",
                          "sidechained", "bus compressed", "parallel compressed", "analog warmth", "clean digital",
                          "vinyl crackle", "lo-fi sampler", "12-bit sampler", "SP-1200 grit", "MPC swing",
                          "overdriven console", "tube preamp", "subtle distortion", "soft clipped", "high passed",
                          "low passed", "EQ'd bright", "warm low end", "boosted sub", "cut low end",
                          "mono", "wide stereo", "close miked", "overhead miked", "recorded on cassette",
                          "recorded on reel to reel", "sampled from a record", "studio quality", "radio ready",
                          "mixed", "unprocessed", "transient heavy",
        "multiband compressed", "brickwall limited", "bus glued", "clipped converter", "SSL bus compression", "Neve console", "API preamp", "1176 compressed", "LA-2A smoothed", "optical compression", "FET compression", "tape head bump", "cassette warble", "wow and flutter", "slight bitcrush", "mild saturation", "exciter on top", "air boost", "presence boost", "mid scooped", "low mids cut", "band passed", "stereo widened", "Haas widened", "mid side processed", "room miked", "ribbon mic", "condenser mic", "dynamic mic", "sampled at 26 kHz", "resampled", "phase aligned", "layered with a sine", "punchy transient", "gentle limiting", "bright top end"};
   return k;
}

// The space it was in.
inline const List &spaces() {
   static const List k = {"dry", "short room tail", "small room", "big room", "plate reverb", "spring reverb",
                          "hall reverb", "gated reverb", "ambient tail", "slapback echo", "dub delay",
                          "short decay", "long decay", "tight", "studio booth", "live room", "garage",
                          "warehouse", "concert hall", "stairwell", "bathroom reverb", "chamber reverb",
        "tight room", "drum room", "wood room", "stone room", "dead room", "small chamber", "big hall", "cathedral", "church", "tunnel", "cave", "outdoors", "open field", "club", "arena", "basement", "attic", "kitchen", "locker room", "parking garage", "metal shed", "echo chamber"};
   return k;
}

// The music it's for.
inline const List &styles() {
   static const List k = {"90s boom bap", "modern trap", "techno", "deep house", "minimal techno", "drum and bass",
                          "jungle", "UK garage", "dubstep", "lo-fi hip hop", "funk", "disco", "80s pop",
                          "70s soul", "jazz", "rock", "indie", "synthwave", "electro", "acid house", "afrobeat",
                          "reggaeton", "dancehall", "drill", "footwork", "breakbeat", "industrial", "ambient",
                          "trip hop", "hyperpop", "R&B", "gospel", "Detroit techno", "Chicago house",
                          "Berlin techno", "Miami bass", "Baltimore club", "grime", "phonk", "city pop",
                          "120 bpm", "90 bpm", "140 bpm", "174 bpm",
        "neo soul", "jazz rap", "cloud rap", "emo rap", "plugg", "rage", "jersey club", "Afro house", "amapiano", "gqom", "kuduro", "baile funk", "dembow", "moombahton", "cumbia", "salsa", "bossa nova", "samba", "dub", "roots reggae", "ska", "punk", "post punk", "shoegaze", "math rock", "metal", "hardcore", "psytrance", "trance", "progressive house", "tech house", "big room", "future bass", "glitch hop", "IDM", "breakcore", "lo-fi house", "electroclash", "new wave", "italo disco", "Eurodance", "hardstyle", "100 bpm", "128 bpm"};
   return k;
}

// Strange words for the character: what it's made of or like.
inline const List &oddTraits() {
   static const List k = {"glassy", "rubbery", "crystalline", "liquid", "furry", "rusty", "frozen", "molten",
                          "wet", "hollow", "underwater", "alien", "haunted", "broken", "melting", "inflatable",
                          "microscopic", "gigantic", "slimy", "papery", "ceramic", "wooden", "plastic",
                          "magnetic", "electric", "insect-like", "mechanical", "bone dry", "feathery",
                          "cardboard", "spongy", "elastic", "creaky", "sparkling", "dusty", "ghostly", "toy-like",
                          "industrial", "volcanic", "cosmic",
        "velvety", "glowing", "fizzy", "sticky", "brittle", "waterlogged", "shattered", "bubbling", "radioactive", "prehistoric", "robotic", "clockwork", "fungal", "mossy", "icy", "sandy", "leathery", "silky", "porcelain", "copper", "brass", "tin", "granite", "marble", "bamboo", "rattan", "gelatinous", "smoky", "steamy", "dreamlike", "nightmarish", "celestial", "subterranean", "oceanic", "desert", "jungle", "arctic", "tiny", "colossal", "ancient"};
   return k;
}

// Strange processes and places.
inline const List &oddTreatments() {
   static const List k = {"granular smear", "played backwards", "bitcrushed to 4-bit", "pitched down two octaves",
                          "pitched up an octave", "time stretched", "through a broken speaker",
                          "through a telephone line", "through a walkie talkie", "recorded inside a water tank",
                          "recorded in a cave", "recorded in a car park", "recorded underwater",
                          "inside a tin can", "through a guitar amp", "frozen with spectral blur",
                          "ring modulated", "with a comb filter", "with a resonant filter sweep",
                          "folded through a wavefolder", "sent through a tape echo", "re-amped in a stairwell",
                          "as heard through a wall", "from the next room", "with infinite reverb",
                          "chopped and stuttered", "with a pitch dive", "with a tape stop", "with metallic resonance",
                          "glitched", "through a fax machine", "in a cathedral", "inside a cardboard box",
                          "inside a grain silo", "in a forest at night", "in a parking garage", "on a dying battery",
                          "sampled from a VHS tape", "through an old radio", "with a vocoder",
        "through a megaphone", "through a gramophone horn", "through a baby monitor", "through a voicemail", "through a laptop speaker", "through headphones on a table", "inside a mailbox", "inside a piano", "inside a washing machine", "inside a bathtub", "inside a car trunk", "inside a fridge", "under a bridge", "in a subway tunnel", "in a swimming pool", "in an empty theater", "in a canyon", "in a snowstorm", "in a greenhouse", "with a shimmer reverb", "with a freeze reverb", "with a pitch shifted echo", "with a ping pong delay", "with a flanger sweep", "with a chorus wobble", "with a formant shift", "with a spectral freeze", "with a reverse reverb swell", "smeared by paulstretch", "sliced into grains", "warped like a bent record", "through a bank of springs", "convolved with a bell", "convolved with a car interior", "stretched ten times longer", "sampled at 8 kHz", "through a dying tape machine", "re-recorded on a phone", "through a rotating speaker", "through a cracked vocoder"};
   return k;
}

// Stable Audio Open 1.0 makes good use of a longer prompt: its dice adds one or two of these, the
// layers and the recording, to what Small would get.
inline const List &layers() {
   static const List k = {"layered with a sub thump", "with a noise burst on top", "with a click on the attack",
                          "with a short metallic ring", "with a body of low mids", "with a breathy tail",
                          "with a hint of room noise", "with a pitched tone underneath", "with a crisp transient",
                          "with a soft shaker layer", "with a tape hiss bed", "with a long reverb tail behind it",
                          "doubled an octave down", "with a snap on top", "with a rattle in the tail",
                          "with a wooden knock in the attack", "with a grainy texture", "with a quiet echo"};
   return k;
}
inline const List &recordings() {
   static const List k = {"close miked", "room miked", "overhead miked", "recorded with a ribbon mic",
                          "recorded with a condenser mic", "recorded with a dynamic mic", "stereo pair",
                          "recorded at 24-bit", "high fidelity", "isolated one-shot", "single hit",
                          "clean recording", "professionally mixed", "sample pack quality", "warm analog chain"};
   return k;
}

// splitmix64: small, and the same on every system.
class Rng {
public:
   explicit Rng(uint64_t seed) : s_(seed) {}
   uint64_t next() {
      uint64_t z = (s_ += 0x9E3779B97F4A7C15ull);
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      return z ^ (z >> 31);
   }
   double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
   bool chance(double p) { return uniform() < p; }
   const char *pick(const List &l) { return l[static_cast<size_t>(next() % l.size())]; }

private:
   uint64_t s_;
};

inline bool contains(const std::vector<std::string> &v, const std::string &s) {
   for (const std::string &x : v)
      if (x == s)
         return true;
   return false;
}

} // namespace prompts

// A random prompt for `role`; `strange` 0..1 is the slider. `detailed` (for Stable Audio Open 1.0)
// adds a layer or the recording, or both. Same arguments, same prompt.
inline std::string randomPrompt(Role role, double strange, uint64_t seed, bool detailed = false) {
   using namespace prompts;
   strange = strange < 0 ? 0 : strange > 1 ? 1 : strange;
   Rng rng(seed * 7919u + static_cast<uint64_t>(role) * 104729u + static_cast<uint64_t>(strange * 1000.0));
   const RoleWords &w = roleWords(role);

   // The sound: the role's plain one, or (more often the stranger it is) an odd object.
   std::string head;
   const bool oddSound = rng.chance(strange * strange * 0.95 + (strange > 0.3 ? 0.15 * strange : 0.0));
   std::vector<std::string> traits;
   const int traitCount = 1 + (rng.chance(0.5) ? 1 : 0);
   for (int i = 0; i < traitCount; ++i) {
      const std::string t = rng.chance(strange * 0.8) ? rng.pick(oddTraits()) : rng.pick(w.traits);
      if (!contains(traits, t))
         traits.push_back(t);
   }
   if (oddSound) {
      head = rng.pick(w.oddities);
      // Now and then name the role too, so a strange sound still fits its pad.
      if (rng.chance(0.5 - 0.3 * strange))
         {
         static const char *const kNouns[kRoleCount] = {"kick", "snare", "hi-hat", "clap", "tom", "percussion", "effect"};
         head = std::string(kNouns[role]) + " like " + head;
      }
      // Its character after it: "a door slammed in a cathedral, hollow".
      if (rng.chance(0.6))
         head += ", " + traits.front();
   } else {
      head = rng.pick(w.sounds);
      std::string front;
      for (const std::string &t : traits)
         front += t + " ";
      head = front + head;
   }
   if (!oddSound && strange > 0.25 && rng.chance(strange * 0.9)) {
      // A familiar sound with an odd origin.
      static const char *const kMadeOf[] = {"made of", "made from", "sampled from", "built from"};
      head += std::string(", ") + kMadeOf[rng.next() % 4] + " " + rng.pick(w.oddities);
   }

   std::vector<std::string> parts{head};
   const auto add = [&](const List &l) {
      const std::string s = rng.pick(l);
      if (!contains(parts, s))
         parts.push_back(s);
   };
   // How it was treated: plain ones fade out as the strange ones come in.
   const int plain = strange < 0.5 ? 1 + (rng.chance(0.5 - strange) ? 1 : 0) : (rng.chance(1.0 - strange) ? 1 : 0);
   for (int i = 0; i < plain; ++i)
      add(treatments());
   const int odd = strange < 0.15 ? 0 : 1 + (rng.chance(strange - 0.5) ? 1 : 0);
   for (int i = 0; i < odd; ++i)
      add(oddTreatments());
   if (rng.chance(0.75 - 0.4 * strange))
      add(spaces());
   if (rng.chance(0.65 - 0.5 * strange))
      add(styles());
   const size_t before = parts.size();
   if (detailed) {
      // A layer, the recording, or both; and a space if it had none.
      const bool layer = rng.chance(0.7), recording = !layer || rng.chance(0.6);
      if (layer)
         add(layers());
      if (recording)
         add(recordings());
      bool spaced = false;
      for (const char *sp : spaces())
         spaced = spaced || contains(parts, sp);
      if (!spaced && rng.chance(0.6))
         add(spaces());
      // Always at least one more than Small's (a pick can repeat what's there).
      for (int tries = 0; tries < 8 && parts.size() == before; ++tries)
         add(layers());
   }

   std::string out;
   for (size_t i = 0; i < parts.size(); ++i)
      out += (i ? ", " : "") + parts[i];
   return out;
}

// ORB's random prompts. ORB can only make the 27 one-shot sounds its physical models play, and it
// reads plain sound-effect captions, so its dice picks from those, grouped into seven kinds in the
// role chips' places (a BOOM goes on a kick's pad, a HIT on a snare's...). Its slider runs from one
// sound to a few in a row, which ORB can plan: "a door slams, then glass shattering".
inline const char *const kOrbKindNames[kRoleCount] = {"Boom", "Hit", "Click", "Glass", "Water", "Metal", "Sweep"};

namespace prompts {

inline const List &orbSounds(Role role) {
   static const List kSounds[kRoleCount] = {
      // Boom: impact, explosion, heartbeat, sub drop
      {"a deep impact", "a heavy impact", "a cinematic impact", "a huge boom", "a low boom", "a massive impact hit",
       "an explosion", "a distant explosion", "a big explosion", "a muffled explosion", "a small explosion",
       "a heartbeat", "a slow heartbeat", "a loud heartbeat", "a fast heartbeat", "a sub drop", "a deep sub drop",
       "a bass drop", "a heavy thud", "a deep thump", "a body hitting the ground", "a low impact boom",
       "a trailer impact", "a dull thud"},
      // Hit: punch, gunshot, door slam, knock
      {"a punch", "a hard punch", "a body punch", "a face punch", "a quick punch", "a heavy punch",
       "a gunshot", "a distant gunshot", "a pistol shot", "a single gunshot", "a loud gunshot",
       "a door slams", "a heavy door slams shut", "a wooden door slamming", "a car door slamming",
       "a door slamming in a hallway", "a knock on a door", "a knock on wood", "a hard knock",
       "two knocks on a door", "a knock on a table", "a fist hitting a table", "a slap", "a hard slap"},
      // Click: click, mouse click, UI pop, camera shutter, glitch
      {"a click", "a short click", "a sharp click", "a mouse click", "a computer mouse clicking",
       "a button click", "a switch click", "a UI pop", "a soft pop", "a bubble pop", "a notification pop",
       "a tiny pop", "a camera shutter", "a camera click", "a camera shutter clicking", "a digital glitch",
       "a glitch", "a short glitch", "a data glitch", "a tick", "a light click", "a keyboard click"},
      // Glass: glass break, cork pop, can opening
      {"glass shattering", "a glass breaking", "a window smashing", "a bottle breaking", "a wine glass shattering",
       "glass breaking on a tile floor", "a glass dropped and shattering", "a mirror breaking",
       "a cork popping", "a champagne cork pop", "a bottle being uncorked", "a cork pulled from a bottle",
       "a can opening", "a soda can opening", "a can of beer cracked open", "a drink can popped open"},
      // Water: water drop, splash
      {"a water drop", "a drip", "water dripping", "a single droplet", "a drop falling into water",
       "a drop in a cave", "a splash", "a small splash", "a big splash", "something falling into water",
       "a stone thrown into a pond", "a splash in a pool", "a rock dropped in water", "a water splash"},
      // Metal: metal clang, bell, church bell, sword clash, coin drop
      {"a metal clang", "a metal pipe hit", "a hammer on metal", "metal clanging", "a pan hit with a spoon",
       "an anvil strike", "a bell", "a small bell", "a bell ring", "a bright bell", "a church bell",
       "a church bell toll", "a distant church bell", "a big bell tolling", "a sword clash",
       "swords clashing", "a blade strike", "a sword hitting a shield", "a coin drop", "a coin falling on a table",
       "coins dropping", "a coin spinning down"},
      // Sweep: whoosh, riser, swell, noise sweep
      {"a whoosh", "a fast whoosh", "a swoosh passing by", "a short whoosh", "an air swish", "a sword swing whoosh",
       "a riser", "a tension riser", "a rising sweep", "a cinematic riser", "a swell", "a slow swell",
       "a reverse swell", "a noise sweep", "a white noise sweep", "a filtered noise sweep", "a transition whoosh"},
   };
   return kSounds[role < 0 || role >= kRoleCount ? 0 : role];
}

// Where and how close: ORB plays its rooms and distances.
inline const List &orbPlaces() {
   static const List k = {"close up", "up close", "far away", "in the distance", "in a small room",
                          "in a large room", "in a big hall", "in a hallway", "outdoors", "in a stairwell",
                          "in a warehouse", "in a tiled bathroom"};
   return k;
}

} // namespace prompts

// A random ORB prompt for `kind` (a role's place); `layered` 0..1 is the slider. Same arguments, same
// prompt.
inline std::string randomOrbPrompt(Role kind, double layered, uint64_t seed) {
   using namespace prompts;
   layered = layered < 0 ? 0 : layered > 1 ? 1 : layered;
   Rng rng(seed * 6007u + static_cast<uint64_t>(kind) * 92821u + static_cast<uint64_t>(layered * 1000.0) + 17u);
   std::string out = rng.pick(orbSounds(kind));
   // More sounds after it the further right: mostly of the same kind, now and then another.
   const int more = (rng.chance(layered * 0.9) ? 1 : 0) + (rng.chance(layered * layered * 0.6) ? 1 : 0);
   static const char *const kJoin[] = {", then ", ", followed by ", " and ", ", then "};
   for (int i = 0; i < more; ++i) {
      const Role k = rng.chance(0.6) ? kind : static_cast<Role>(rng.next() % kRoleCount);
      std::string next = rng.pick(orbSounds(k));
      if (out.find(next) != std::string::npos)
         continue;
      out += kJoin[rng.next() % 4] + next;
   }
   if (rng.chance(0.35))
      out += std::string(", ") + rng.pick(orbPlaces());
   return out;
}

} // namespace jkit
