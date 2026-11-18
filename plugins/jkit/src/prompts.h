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
        "reggaeton kick", "industrial kick", "synthwave kick", "kick drum hit", "felt beater kick"},
       {"punchy", "deep", "tight", "boomy", "clicky", "warm", "fat", "subby", "round", "hard hitting", "thumpy",
        "dry", "muffled", "crisp", "heavy", "dusty", "clean", "saturated", "snappy", "booming", "rumbling",
        "short", "long decaying", "low tuned", "high tuned", "soft", "aggressive", "vintage", "modern", "lo-fi",
        "compressed", "solid", "woody", "papery", "pillowy", "huge", "gritty", "bouncy"},
       {"a door slammed in a cathedral", "a heartbeat inside a whale", "a bowling ball dropped on a stage",
        "a fist on a wooden coffin", "a beanbag thrown at a wall", "a car door closing underwater",
        "a suitcase falling down stairs", "a pumpkin hitting the ground", "a thunderclap folded into one beat",
        "a giant stepping on a trampoline", "a pillow punched in a tunnel", "a medicine ball on a gym floor",
        "a cannon fired inside a mattress", "a timpani struck with a sledgehammer", "a sack of flour landing",
        "a submarine hull being knocked on", "an elephant's footstep", "a basketball bounced in a parking garage",
        "a dropped cast iron pan", "a rubber mallet on an oil drum", "a skateboard landing on concrete",
        "a tree stump hit with an axe", "a bass speaker pushed until it tears", "the boom of a distant quarry blast"}},
      // Snare
      {{"snare", "snare drum", "808 snare", "909 snare", "acoustic snare", "rimshot snare", "trap snare",
        "boom bap snare", "breakbeat snare", "piccolo snare", "brushed snare", "rock snare", "funk snare",
        "jungle snare", "house snare", "lo-fi snare", "gated snare", "marching snare", "dubstep snare",
        "drum and bass snare", "garage snare", "snare with snappy wires", "snare roll hit", "electro snare",
        "noise snare", "layered snare and clap", "vinyl snare", "reggae snare", "UK garage snare", "pop snare",
        "jazz snare", "ghost note snare", "cross stick", "synth snare", "metal snare", "Motown snare"},
       {"crisp", "fat", "tight", "snappy", "cracking", "dusty", "warm", "bright", "dark", "punchy", "papery",
        "ringing", "dead", "muffled", "loud", "dry", "wet", "vintage", "modern", "trashy", "crunchy", "airy",
        "deep", "tuned low", "tuned high", "short", "long", "rattling", "woody", "metallic", "clean", "smashed",
        "compressed", "lo-fi", "sharp", "full bodied", "thin", "explosive"},
       {"a filing cabinet slammed shut", "a stack of cardboard boxes falling", "a whip cracked in a stairwell",
        "a newspaper slapped on a table", "a bag of gravel dropped", "a gunshot in a canyon heard from far away",
        "a book closed hard in a library", "a tin roof hit by a stone", "a sheet of plywood snapping",
        "a firecracker in a metal bin", "a tennis racket hitting a fence", "a cymbal stand knocked over",
        "a balloon popped in a bathroom", "a rubber band snapped on a guitar", "a crate of bottles set down",
        "a karate chop on a wooden board", "an ice sheet cracking", "a typewriter's carriage return",
        "a chair dropped in an empty hall", "a cardboard tube hit on a desk", "rain hitting a snare in a storm",
        "a padlock slammed on a gate"}},
      // Hat
      {{"closed hi-hat", "open hi-hat", "hi-hat", "808 hi-hat", "909 hi-hat", "pedal hi-hat", "trap hi-hat",
        "shaker hat", "ride cymbal", "crash cymbal", "cymbal hit", "splash cymbal", "china cymbal", "tambourine hit",
        "house open hat", "techno hat", "jazz ride bell", "hat with a short decay", "half open hi-hat",
        "lo-fi hi-hat", "electro hi-hat", "garage hat", "disco open hat", "noise hat", "metallic tick",
        "brushed cymbal", "sizzle cymbal", "ride bell", "dusty hat", "acoustic hi-hat"},
       {"crisp", "sizzling", "shimmering", "tight", "airy", "bright", "dark", "trashy", "washy", "ticking",
        "short", "long", "sharp", "soft", "metallic", "noisy", "glassy", "clean", "dirty", "dusty", "vintage",
        "modern", "splashy", "thin", "dry", "ringing", "breathy", "hissy", "sparkling", "chopped", "filtered",
        "silky", "gritty", "delicate", "cutting"},
       {"keys shaken in a fist", "a bead curtain swept by hand", "sand thrown on a snare", "a cicada's click",
        "sparks from a grinder", "a spray can's hiss", "a bicycle bell's tick", "a sprinkler's ratchet",
        "coins spilling on glass", "a zipper pulled fast", "frost cracking on a window", "radio static cut short",
        "a can of soda opened", "a broom on a tiled floor", "rice falling on a cymbal", "a pocket watch ticking",
        "a crushed potato chip bag", "a matchstick struck", "steam escaping a valve", "a lock picked open",
        "a stapler closing", "wind chimes muted by hand"}},
      // Clap
      {{"clap", "hand clap", "808 clap", "909 clap", "group clap", "layered clap", "snap", "finger snap",
        "trap clap", "house clap", "gospel clap", "crowd clap", "single hand clap", "reverb clap", "lo-fi clap",
        "electro clap", "stomp and clap", "flamenco clap", "disco clap", "garage clap", "big room clap",
        "knock", "slap", "body slap", "thigh slap", "table slap", "noise clap", "vintage drum machine clap"},
       {"tight", "wide", "loud", "crisp", "dry", "roomy", "short", "long", "fat", "thin", "bright", "dark",
        "snappy", "human", "stacked", "smeared", "punchy", "warm", "crunchy", "dusty", "clean", "sloppy",
        "flammed", "echoing", "crowded", "lonely", "sharp", "soft", "papery", "fleshy"},
       {"a hundred pigeons taking off at once", "two books clapped together", "a wet towel snapped",
        "a hand slapping a car hood", "applause in an empty stadium", "bubble wrap stomped on",
        "flip flops slapping on tiles", "a ping pong ball hitting a paddle", "a fish flopping on a deck",
        "a sail filling with wind", "a pair of oven gloves slapped together", "a cardboard box stamped flat",
        "hailstones on a tent", "a cork popped in a cellar", "a mousetrap snapping shut", "a fan of cards shuffled",
        "an umbrella snapped open", "a belt cracked in a closet"}},
      // Tom
      {{"tom", "floor tom", "rack tom", "high tom", "low tom", "mid tom", "808 tom", "909 tom", "electronic tom",
        "Simmons tom", "concert tom", "roto tom", "tom fill hit", "tribal tom", "taiko drum", "surdo",
        "timpani", "jazz tom", "rock tom", "disco tom", "syndrum", "tuned tom", "deep floor tom",
        "lo-fi tom", "70s tom", "80s tom", "big tom with a pitch drop", "frame drum", "djembe bass"},
       {"deep", "boomy", "round", "punchy", "resonant", "dead", "tuned", "melodic", "warm", "fat", "short",
        "long", "dry", "roomy", "big", "small", "dark", "bright", "pitch dropping", "booming", "woody", "dusty",
        "clean", "vintage", "huge", "thunderous", "soft", "muted"},
       {"a water cooler bottle struck", "a hollow log hit with a club", "a rain barrel thumped",
        "a cardboard drum in a forest", "a pumpkin knocked on", "a wooden crate kicked", "a basketball dribbled once",
        "a sealed paint can hit", "a gourd floating in water", "a hot air balloon's burner thump",
        "a church door knocked on", "a ship's hull knocked from inside", "a giant's knuckle on a table",
        "a coconut cracked open", "a beer keg thumped"}},
      // Perc
      {{"percussion hit", "conga", "bongo", "cowbell", "rimshot", "woodblock", "claves", "shaker", "tambourine",
        "cabasa", "guiro scrape", "agogo bell", "timbale", "cajon hit", "tabla", "darbuka", "udu", "triangle",
        "temple block", "castanets", "maracas", "guiro", "log drum", "steel drum hit", "marimba hit", "kalimba hit",
        "vibraslap", "808 cowbell", "808 rim", "909 rim", "metal percussion", "glass percussion", "click", "blip",
        "zap", "anvil hit", "slit drum", "cuica", "berimbau", "pandeiro"},
       {"tuned", "wooden", "metallic", "bright", "dark", "short", "tight", "dry", "resonant", "clicky", "plucky",
        "glassy", "hollow", "ringing", "muted", "warm", "crisp", "dusty", "clean", "lo-fi", "playful", "organic",
        "rattling", "earthy", "tropical", "exotic", "staccato", "shiny", "sharp", "soft"},
       {"a spoon on a wine glass", "a marble dropped in a bowl", "a ruler twanged on a desk",
        "a tap on a fish tank", "a key on a radiator", "a coin spun on a table", "a cork pulled from a bottle",
        "a droplet into a bucket", "chopsticks on a teapot", "a pebble on a frozen lake",
        "a bottle cap flicked", "a xylophone made of bones", "a typewriter key", "a pencil on a skull",
        "a toy gun's click", "a light switch flicked", "a carabiner snapping shut", "a tongue click in a cave",
        "an abacus bead sliding", "a ceramic tile tapped", "a fork on a radiator pipe", "a clock spring released"}},
      // FX
      {{"riser", "downlifter", "impact", "sweep", "noise sweep", "reverse cymbal", "white noise riser",
        "sub drop", "boom impact", "laser zap", "glitch", "whoosh", "tape stop", "vinyl scratch", "vinyl crackle",
        "transition effect", "uplifter", "bass drop", "cinematic hit", "braam", "stutter", "radio tuning",
        "synth zap", "alarm stab", "siren", "air horn", "rewind effect", "swell", "metallic swell", "drone hit",
        "reverse snare", "pitch rise", "pitch dive", "dial up noise", "rumble", "atmosphere hit"},
       {"huge", "cinematic", "dark", "bright", "rising", "falling", "sweeping", "glitchy", "warped", "distorted",
        "wide", "long", "short", "eerie", "epic", "airy", "metallic", "noisy", "filtered", "evolving",
        "ominous", "euphoric", "tense", "spacey", "sci-fi", "retro", "lo-fi", "massive", "shimmering", "grainy",
        "sucking", "breathy"},
       {"a black hole swallowing a choir", "a jet engine spooling down in a cave", "a whale song sped up",
        "a modem falling in love", "a lightsaber dropped in a pool", "a train passing through a wormhole",
        "a glacier calving in slow motion", "a robot's sigh", "a satellite burning up", "a bee swarm in a tin can",
        "a cassette tape eaten by a deck", "a kettle reaching boil in a cathedral", "a firework seen from space",
        "a ghost dragging a chain", "a door into another dimension opening", "a vacuum cleaner played backwards",
        "the end of a dial tone", "a theremin melting", "a tornado heard through a wall",
        "a power station switching off", "a pinball machine tilting", "an elevator falling past floors"}},
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
                          "mixed", "unprocessed", "transient heavy"};
   return k;
}

// The space it was in.
inline const List &spaces() {
   static const List k = {"dry", "short room tail", "small room", "big room", "plate reverb", "spring reverb",
                          "hall reverb", "gated reverb", "ambient tail", "slapback echo", "dub delay",
                          "short decay", "long decay", "tight", "studio booth", "live room", "garage",
                          "warehouse", "concert hall", "stairwell", "bathroom reverb", "chamber reverb"};
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
                          "120 bpm", "90 bpm", "140 bpm", "174 bpm"};
   return k;
}

// Strange words for the character: what it's made of or like.
inline const List &oddTraits() {
   static const List k = {"glassy", "rubbery", "crystalline", "liquid", "furry", "rusty", "frozen", "molten",
                          "wet", "hollow", "underwater", "alien", "haunted", "broken", "melting", "inflatable",
                          "microscopic", "gigantic", "slimy", "papery", "ceramic", "wooden", "plastic",
                          "magnetic", "electric", "insect-like", "mechanical", "bone dry", "feathery",
                          "cardboard", "spongy", "elastic", "creaky", "sparkling", "dusty", "ghostly", "toy-like",
                          "industrial", "volcanic", "cosmic"};
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
                          "sampled from a VHS tape", "through an old radio", "with a vocoder"};
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

// A random prompt for `role`; `strange` 0..1 is the slider. Same arguments, same prompt.
inline std::string randomPrompt(Role role, double strange, uint64_t seed) {
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

   std::string out;
   for (size_t i = 0; i < parts.size(); ++i)
      out += (i ? ", " : "") + parts[i];
   return out;
}

} // namespace jkit
