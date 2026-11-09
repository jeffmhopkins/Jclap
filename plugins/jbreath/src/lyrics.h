#pragma once
// The Alien's words: what it sings, a syllable a note.
//
// - Typed: English words, looked up in the CMU Pronouncing Dictionary (or, for a word it doesn't
//   have, spelled out by a few letter-to-sound rules), or phones written out in ARPAbet between
//   brackets, [HH AH0 L OW1]. Split into syllables as English splits them (speech.h).
// - Alien: words of its own, made up from a seed. A small vocabulary (each word one to a few
//   syllables, from the consonants and vowels it's let use) and a run of them, the commoner words
//   coming back more often, as they do in a language.
//
// Either way it's a Book: the syllables in order, and where each one's word is in the text (for the
// panel). Building a typed book allocates (the main thread's job); an alien one doesn't, so the
// audio thread can make it whenever its settings change.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "cmudict_data.h"
#include "speech.h"

namespace jbreath::speech {

// ---- The dictionary ----------------------------------------------------------------------------

namespace detail {
// Every 64th word's place in the packed dictionary (which shares no letters with the word before),
// found once.
struct CmuIndex {
   struct At {
      int piece, offset;
   };
   std::vector<At> starts;
   CmuIndex() {
      int words = 0;
      for (int p = 0; p < kCmuDictPieces; ++p) {
         const char *t = kCmuDict[p];
         for (int i = 0; t[i];) {
            if (t[i] == '\n' || t[i] == '\r') {
               ++i;
               continue;
            }
            if (words % 64 == 0)
               starts.push_back({p, i});
            ++words;
            while (t[i] && t[i] != '\n')
               ++i;
         }
      }
   }
};
inline const CmuIndex &cmuIndex() {
   static const CmuIndex index;
   return index;
}
// The line at `at`: its word (with the shared letters of `last`), its phones' codes.
inline void cmuLine(const char *t, int &i, std::string &word, std::string_view &phones) {
   while (t[i] == '\n' || t[i] == '\r')
      ++i;
   const int shared = t[i++] - '0';
   word.resize(std::min<int>(shared, static_cast<int>(word.size())));
   while (t[i] && t[i] != ' ')
      word += t[i++];
   if (t[i] == ' ')
      ++i;
   const int from = i;
   while (t[i] && t[i] != '\n' && t[i] != '\r')
      ++i;
   phones = std::string_view(t + from, static_cast<size_t>(i - from));
}
inline std::string cmuPhones(std::string_view codes) {
   std::string out;
   const std::string_view alphabet(kCmuPhoneAlphabet);
   for (char c : codes) {
      const size_t k = alphabet.find(c);
      if (k == std::string_view::npos)
         continue;
      if (!out.empty())
         out += ' ';
      out += kCmuPhoneNames[k];
   }
   return out;
}
} // namespace detail

// The word's phones in ARPAbet (stress digits on the vowels), "" if the dictionary hasn't got it.
// `word` in lower case.
inline std::string lookUp(std::string_view word) {
   const auto &index = detail::cmuIndex();
   if (index.starts.empty() || word.empty())
      return {};
   // The last block that starts at or before the word.
   int lo = 0, hi = static_cast<int>(index.starts.size()) - 1;
   std::string w;
   std::string_view phones;
   while (lo < hi) {
      const int mid = (lo + hi + 1) / 2;
      int i = index.starts[mid].offset;
      w.clear();
      detail::cmuLine(kCmuDict[index.starts[mid].piece], i, w, phones);
      if (std::string_view(w) <= word)
         lo = mid;
      else
         hi = mid - 1;
   }
   int piece = index.starts[lo].piece, i = index.starts[lo].offset;
   w.clear();
   for (int n = 0; n < 64; ++n) {
      const char *t = kCmuDict[piece];
      while (t[i] == '\n' || t[i] == '\r')
         ++i;
      if (!t[i]) {
         if (++piece >= kCmuDictPieces)
            break;
         i = 0;
         --n;
         continue;
      }
      detail::cmuLine(t, i, w, phones);
      if (std::string_view(w) == word)
         return detail::cmuPhones(phones);
      if (std::string_view(w) > word)
         break;
   }
   return {};
}

// A word the dictionary hasn't got, as English spelling most likely says it: a handful of rules,
// the longest match first, the first vowel stressed. (A made-up word, an alien name, comes out as an
// English reader would say it.)
inline std::string spellOut(std::string_view word) {
   std::string w(word);
   w.erase(std::remove(w.begin(), w.end(), '\''), w.end());
   const int n = static_cast<int>(w.size());
   const auto vowel = [](char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; };
   const auto at = [&](int i) { return i >= 0 && i < n ? w[i] : '\0'; };
   // A final silent e ("make", "tone"): the vowel before the consonant before it says its name.
   int vowels = 0;
   for (int i = 0; i < n; ++i)
      vowels += vowel(w[i]) && !(i > 0 && vowel(w[i - 1])) ? 1 : 0;
   const bool silentE = n > 2 && w[n - 1] == 'e' && !vowel(w[n - 2]) && vowels > 1;
   std::vector<std::string> out;
   bool stressed = false;
   const auto v = [&](const char *name) {
      out.push_back(std::string(name) + (stressed ? "0" : "1"));
      stressed = true;
   };
   const auto c = [&](const char *name) {
      if (out.empty() || out.back() != name)
         out.push_back(name);
   };
   for (int i = 0; i < n;) {
      const char x = w[i], y = at(i + 1), z = at(i + 2);
      const auto starts = [&](const char *s) { return w.compare(i, std::strlen(s), s) == 0; };
      // (Is a vowel here followed by one consonant and the silent e?)
      const bool longVowel = silentE && i == n - 3;
      if (silentE && i == n - 1)
         break;
      if (starts("tch")) { c("CH"); i += 3; continue; }
      if (starts("ch")) { c("CH"); i += 2; continue; }
      if (starts("sh")) { c("SH"); i += 2; continue; }
      if (starts("th")) { c("TH"); i += 2; continue; }
      if (starts("ph")) { c("F"); i += 2; continue; }
      if (starts("wh")) { c("W"); i += 2; continue; }
      if (starts("ck")) { c("K"); i += 2; continue; }
      if (starts("ng")) { c("NG"); i += 2; continue; }
      if (starts("qu")) { c("K"); c("W"); i += 2; continue; }
      if (i == 0 && starts("kn")) { c("N"); i += 2; continue; }
      if (i == 0 && starts("wr")) { c("R"); i += 2; continue; }
      if (starts("gh")) { if (i == 0) c("G"); i += 2; continue; }
      if (starts("ee") || starts("ea") || starts("ie")) { v("IY"); i += 2; continue; }
      if (starts("oo")) { v("UW"); i += 2; continue; }
      if (starts("ou") || (starts("ow") && i + 2 < n)) { v("AW"); i += 2; continue; }
      if (starts("ow") || starts("oa") || starts("oe")) { v("OW"); i += 2; continue; }
      if (starts("oi") || starts("oy")) { v("OY"); i += 2; continue; }
      if (starts("ai") || starts("ay") || starts("ei") || starts("ey")) { v("EY"); i += 2; continue; }
      if (starts("au") || starts("aw")) { v("AO"); i += 2; continue; }
      if (starts("ue") || starts("ew")) { v("UW"); i += 2; continue; }
      if ((starts("ar")) && !vowel(z)) { v("AA"); c("R"); i += 2; continue; }
      if ((starts("or")) && !vowel(z)) { v("AO"); c("R"); i += 2; continue; }
      if ((starts("er") || starts("ir") || starts("ur")) && !vowel(z)) { v("ER"); i += 2; continue; }
      switch (x) {
      case 'a': longVowel ? v("EY") : v(i == n - 1 ? "AA" : "AE"); break;
      case 'e': longVowel || (i == n - 1 && vowels == 1) ? v("IY") : v(i == n - 1 ? "IY" : "EH"); break;
      case 'i': longVowel ? v("AY") : v(i == n - 1 ? "IY" : "IH"); break;
      case 'o': longVowel || i == n - 1 ? v("OW") : v("AA"); break;
      case 'u': longVowel || i == n - 1 ? v("UW") : v("AH"); break;
      case 'y':
         if (i == 0 || vowel(y))
            c("Y");
         else
            v(vowels <= 1 ? "AY" : "IY");
         break;
      case 'b': c("B"); break;
      case 'c': c(y == 'e' || y == 'i' || y == 'y' ? "S" : "K"); break;
      case 'd': c("D"); break;
      case 'f': c("F"); break;
      case 'g': c(y == 'e' || y == 'i' || y == 'y' ? "JH" : "G"); break;
      case 'h': c("HH"); break;
      case 'j': c("JH"); break;
      case 'k': c("K"); break;
      case 'l': c("L"); break;
      case 'm': c("M"); break;
      case 'n': c("N"); break;
      case 'p': c("P"); break;
      case 'q': c("K"); break;
      case 'r': c("R"); break;
      case 's': c(i > 0 && vowel(at(i - 1)) && vowel(y) ? "Z" : "S"); break;
      case 't': c("T"); break;
      case 'v': c("V"); break;
      case 'w': c("W"); break;
      case 'x': c("K"); c("S"); break;
      case 'z': c("Z"); break;
      default: break;
      }
      ++i;
   }
   std::string s;
   for (const auto &p : out)
      s += (s.empty() ? "" : " ") + p;
   return s;
}

// Fills `book` with what `text` says: each word's syllables in turn.
inline void readLyrics(std::string_view text, Book &book) {
   book.clear();
   size_t i = 0;
   while (i < text.size() && book.count < Book::kMax) {
      const char ch = text[i];
      if (ch == '[') {
         // Phones as they are, up to the closing bracket: one word.
         const size_t close = text.find(']', i + 1);
         const size_t end = close == std::string_view::npos ? text.size() : close;
         for (const Syllable &s : syllabify(text.substr(i + 1, end - i - 1)))
            if (!book.add(s, static_cast<int>(i), static_cast<int>(std::min(end + 1, text.size()))))
               break;
         i = end + 1;
         continue;
      }
      if (!std::isalpha(static_cast<unsigned char>(ch))) {
         ++i;
         continue;
      }
      size_t j = i;
      while (j < text.size() && (std::isalpha(static_cast<unsigned char>(text[j])) || text[j] == '\''))
         ++j;
      std::string word(text.substr(i, j - i));
      for (char &c : word)
         c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      while (!word.empty() && word.back() == '\'')
         word.pop_back();
      std::string phones = lookUp(word);
      if (phones.empty())
         phones = spellOut(word);
      for (const Syllable &s : syllabify(phones))
         if (!book.add(s, static_cast<int>(i), static_cast<int>(j)))
            break;
      i = j;
   }
}

// ---- Alien words --------------------------------------------------------------------------------

// The eight families of consonants (each can be turned off) and the five of vowels, as the panel
// names them: m n ng, l r, w y, v dh z zh, b d g j, h, f th s sh, p t k ch; and A E I O U.
inline constexpr const char *kAlienFamilyPhones[8][4] = {
   {"M", "N", "NG", nullptr}, {"L", "R", nullptr, nullptr}, {"W", "Y", nullptr, nullptr}, {"V", "DH", "Z", "ZH"},
   {"B", "D", "G", "JH"},     {"HH", nullptr, nullptr, nullptr}, {"F", "TH", "S", "SH"}, {"P", "T", "K", "CH"},
};
inline constexpr const char *kAlienVowelPhones[5][4] = {
   {"AA", "AE", "AY", "AW"}, {"EH", "EY", "ER", nullptr}, {"IY", "IH", nullptr, nullptr},
   {"OW", "AO", "OY", nullptr}, {"UW", "UH", "AH", nullptr},
};
// How the alien words run: short (a syllable each), mixed, long, or one word chanted over and over.
enum AlienLength { kAlienShort, kAlienMixed, kAlienLong, kAlienChant };

namespace detail {
struct Rng {
   uint32_t s;
   uint32_t next() {
      s ^= s << 13;
      s ^= s >> 17;
      s ^= s << 5;
      return s;
   }
   double unit() { return (next() >> 8) / 16777216.0; }
   int below(int n) { return n > 0 ? static_cast<int>(unit() * n) % n : 0; }
};
inline bool sameName(int phone, const char *name) { return phone >= 0 && name && std::strcmp(kPhones[phone].name, name) == 0; }
} // namespace detail

// How to write an alien phone, for the panel.
inline const char *alienSpelling(int phone) {
   static const char *const names[][2] = {
      {"IY", "ee"}, {"IH", "i"}, {"EY", "ay"}, {"EH", "e"}, {"AE", "a"}, {"AA", "ah"}, {"AO", "aw"}, {"OW", "o"},
      {"UH", "uu"}, {"UW", "oo"}, {"AH", "u"}, {"AX", "a"}, {"ER", "ur"}, {"AY", "ai"}, {"AW", "ow"}, {"OY", "oy"},
      {"W", "w"}, {"Y", "y"}, {"R", "r"}, {"L", "l"}, {"M", "m"}, {"N", "n"}, {"NG", "ng"}, {"F", "f"},
      {"V", "v"}, {"TH", "th"}, {"DH", "dh"}, {"S", "s"}, {"Z", "z"}, {"SH", "sh"}, {"ZH", "zh"}, {"HH", "h"},
      {"P", "p"}, {"B", "b"}, {"T", "t"}, {"D", "d"}, {"K", "k"}, {"G", "g"}, {"CH", "ch"}, {"JH", "j"},
   };
   for (const auto &n : names)
      if (detail::sameName(phone, n[0]))
         return n[1];
   return "";
}

// Makes up a book of alien words from `seed`, using only the consonant families in `sounds` and the
// vowels in `vowels` (a bit each, as the panel's buttons). If `text` is given, it's filled with the
// words as they'd be written, and the book's ranges point into it. Doesn't allocate without `text`.
inline void alienWords(uint32_t seed, uint32_t sounds, uint32_t vowels, int length, Book &book, std::string *text = nullptr) {
   book.clear();
   if (text)
      text->clear();
   detail::Rng rng{seed * 2654435761u + 0x9e3779b9u};
   for (int i = 0; i < 4; ++i)
      rng.next();
   // What it may say.
   int cons[32], consCount = 0, vow[16], vowCount = 0;
   for (int f = 0; f < 8; ++f)
      if (sounds & (1u << f))
         for (const char *name : kAlienFamilyPhones[f])
            if (name)
               cons[consCount++] = findPhone(name);
   for (int f = 0; f < 5; ++f)
      if (vowels & (1u << f))
         for (const char *name : kAlienVowelPhones[f])
            if (name)
               vow[vowCount++] = findPhone(name);
   if (vowCount == 0)
      vow[vowCount++] = findPhone("AA");
   const auto has = [&](const char *name) {
      for (int k = 0; k < consCount; ++k)
         if (detail::sameName(cons[k], name))
            return cons[k];
      return -1;
   };
   // One syllable: an onset (none, one, or a cluster English allows), a vowel, and a coda.
   const auto syllable = [&](bool first, bool last) {
      Syllable s;
      s.vowel = vow[rng.below(vowCount)];
      s.stress = first ? 1 : 0;
      if (consCount > 0) {
         const double r = rng.unit();
         int o = -1;
         for (int tries = 0; tries < 6 && o < 0; ++tries) {
            const int c = cons[rng.below(consCount)];
            // (Never ng or zh to start one, nor a y before ee or a w before oo.)
            if (detail::sameName(c, "NG") || detail::sameName(c, "ZH"))
               continue;
            if ((detail::sameName(c, "Y") && detail::sameName(s.vowel, "IY")) || (detail::sameName(c, "W") && detail::sameName(s.vowel, "UW")))
               continue;
            o = c;
         }
         if (r < 0.85 && o >= 0) {
            s.onset[0] = o;
            // Now and then a cluster: a stop or f with an l or r after it, or an s before a stop.
            const Kind k = kPhones[o].kind;
            if (rng.unit() < 0.18) {
               const int l = has(rng.unit() < 0.5 ? "L" : "R");
               const int st = has("S");
               if (l >= 0 && (k == kStop || detail::sameName(o, "F")) && !detail::sameName(o, "T") && !detail::sameName(o, "D"))
                  s.onset[1] = l;
               else if (st >= 0 && k == kStop && !kPhones[o].voiced) {
                  s.onset[1] = o;
                  s.onset[0] = st;
               }
            }
         }
         // A closing consonant: more often at a word's end.
         if (rng.unit() < (last ? 0.5 : 0.2)) {
            for (int tries = 0; tries < 6; ++tries) {
               const int c = cons[rng.below(consCount)];
               const Kind k = kPhones[c].kind;
               if (k == kGlide || k == kAspirate)
                  continue;
               s.coda[0] = c;
               break;
            }
         }
      }
      return s;
   };
   // The vocabulary: a dozen words.
   constexpr int kWords = 12, kLongest = 4;
   Syllable words[kWords][kLongest];
   int sizes[kWords];
   for (int w = 0; w < kWords; ++w) {
      int n = 1;
      const double r = rng.unit();
      switch (length) {
      case kAlienShort: n = 1; break;
      case kAlienLong: n = r < 0.4 ? 2 : r < 0.8 ? 3 : 4; break;
      case kAlienChant: n = r < 0.5 ? 2 : 3; break;
      default: n = r < 0.45 ? 1 : r < 0.85 ? 2 : 3; break;
      }
      sizes[w] = n;
      for (int k = 0; k < n; ++k)
         words[w][k] = syllable(k == 0, k == n - 1);
   }
   // A run of them, each picked more often the earlier it is in the vocabulary (as a language's
   // commonest words are), never the same twice in a row unless chanting.
   int last = -1;
   while (book.count < Book::kMax) {
      int w = 0;
      if (length == kAlienChant)
         w = 0;
      else {
         for (int tries = 0; tries < 4; ++tries) {
            const double r = rng.unit();
            w = std::min(kWords - 1, static_cast<int>(kWords * std::pow(r, 1.5)));
            if (w != last)
               break;
         }
      }
      last = w;
      if (book.count + sizes[w] > Book::kMax)
         break;
      int from = 0, to = 0;
      if (text) {
         if (!text->empty())
            *text += ' ';
         from = static_cast<int>(text->size());
         for (int k = 0; k < sizes[w]; ++k) {
            const Syllable &s = words[w][k];
            for (int c = 0; c < s.onsetCount(); ++c)
               *text += alienSpelling(s.onset[c]);
            *text += alienSpelling(s.vowel);
            for (int c = 0; c < s.codaCount(); ++c)
               *text += alienSpelling(s.coda[c]);
         }
         to = static_cast<int>(text->size());
      }
      for (int k = 0; k < sizes[w]; ++k)
         book.add(words[w][k], from, to);
      // (The panel shows the first few dozen words; the rest it only sings.)
      if (text && text->size() > 2000)
         text = nullptr;
   }
}

} // namespace jbreath::speech
