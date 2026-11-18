#include "gen/tokenizer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace jkit::gen {
namespace {

// Appends the UTF-8 for code point `cp`.
void putUtf8(std::string &out, unsigned cp) {
   if (cp < 0x80) {
      out += static_cast<char>(cp);
   } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
   } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
   } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
   }
}

// Reads a JSON string starting just after its opening quote at `i`; leaves `i` after the closing one.
bool readString(const std::string &s, size_t &i, std::string &out) {
   out.clear();
   while (i < s.size()) {
      const char c = s[i++];
      if (c == '"')
         return true;
      if (c != '\\') {
         out += c;
         continue;
      }
      if (i >= s.size())
         return false;
      const char e = s[i++];
      switch (e) {
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      case 'r': out += '\r'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'u': {
         if (i + 4 > s.size())
            return false;
         unsigned cp = static_cast<unsigned>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
         i += 4;
         // A surrogate pair.
         if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            const unsigned lo = static_cast<unsigned>(std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16));
            if (lo >= 0xDC00 && lo < 0xE000) {
               cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
               i += 6;
            }
         }
         putUtf8(out, cp);
         break;
      }
      default: out += e; break;
      }
   }
   return false;
}

void skipSpace(const std::string &s, size_t &i) {
   while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
      ++i;
}

bool isContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }

const std::string kMeta = "\xE2\x96\x81"; // ▁

} // namespace

bool Tokenizer::load(const std::string &json) {
   pieces_.clear();
   scores_.clear();
   maxPieceBytes_ = 0;
   // The model's vocab: [["piece", score], ...].
   const size_t model = json.find("\"model\"");
   if (model == std::string::npos)
      return false;
   size_t i = json.find("\"vocab\"", model);
   if (i == std::string::npos)
      return false;
   i = json.find('[', i);
   if (i == std::string::npos)
      return false;
   ++i;
   std::string piece;
   float minScore = 0.0f;
   for (;;) {
      skipSpace(json, i);
      if (i >= json.size())
         return false;
      if (json[i] == ']')
         break;
      if (json[i] == ',') {
         ++i;
         continue;
      }
      if (json[i] != '[')
         return false;
      ++i;
      skipSpace(json, i);
      if (i >= json.size() || json[i] != '"')
         return false;
      ++i;
      if (!readString(json, i, piece))
         return false;
      skipSpace(json, i);
      if (i >= json.size() || json[i] != ',')
         return false;
      ++i;
      char *end = nullptr;
      const float score = std::strtof(json.c_str() + i, &end);
      if (end == json.c_str() + i)
         return false;
      i = static_cast<size_t>(end - json.c_str());
      skipSpace(json, i);
      if (i >= json.size() || json[i] != ']')
         return false;
      ++i;
      const int id = static_cast<int>(scores_.size());
      scores_.push_back(score);
      minScore = std::min(minScore, score);
      // The special tokens (<pad>, </s>, <unk>, <extra_id_N>) never come out of text.
      const bool special = piece.size() > 2 && piece.front() == '<' && piece.back() == '>';
      if (!special && !piece.empty()) {
         pieces_.emplace(piece, id);
         maxPieceBytes_ = std::max(maxPieceBytes_, piece.size());
      }
   }
   unkScore_ = minScore - 10.0f;
   return !pieces_.empty();
}

void Tokenizer::encodeWord(const std::string &word, std::vector<int64_t> &out) const {
   const size_t n = word.size();
   // best[i]: the best score of a cut of word[0, i), how the last piece got there, and its id.
   std::vector<float> best(n + 1, -std::numeric_limits<float>::infinity());
   std::vector<size_t> from(n + 1, 0);
   std::vector<int> id(n + 1, -1);
   best[0] = 0.0f;
   for (size_t start = 0; start < n; ++start) {
      if (isContinuation(static_cast<unsigned char>(word[start])) || best[start] == -std::numeric_limits<float>::infinity())
         continue;
      // The character at `start`, as an unknown: one whole UTF-8 sequence.
      size_t charEnd = start + 1;
      while (charEnd < n && isContinuation(static_cast<unsigned char>(word[charEnd])))
         ++charEnd;
      bool covered = false;
      const size_t limit = std::min(n, start + maxPieceBytes_);
      for (size_t end = start + 1; end <= limit; ++end) {
         if (end < n && isContinuation(static_cast<unsigned char>(word[end])))
            continue;
         const auto it = pieces_.find(word.substr(start, end - start));
         if (it == pieces_.end())
            continue;
         if (end == charEnd)
            covered = true;
         const float s = best[start] + scores_[static_cast<size_t>(it->second)];
         if (s > best[end]) {
            best[end] = s;
            from[end] = start;
            id[end] = it->second;
         }
      }
      if (!covered) {
         const float s = best[start] + unkScore_;
         if (s > best[charEnd]) {
            best[charEnd] = s;
            from[charEnd] = start;
            id[charEnd] = kUnk;
         }
      }
   }
   std::vector<int64_t> ids;
   for (size_t at = n; at > 0; at = from[at])
      ids.push_back(id[at] < 0 ? kUnk : id[at]);
   std::reverse(ids.begin(), ids.end());
   // Runs of unknowns become one.
   for (int64_t v : ids)
      if (!(v == kUnk && !out.empty() && out.back() == kUnk))
         out.push_back(v);
}

std::vector<int64_t> Tokenizer::encode(const std::string &text, size_t maxLength) const {
   std::vector<int64_t> out;
   size_t i = 0;
   while (i < text.size()) {
      while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
         ++i;
      size_t j = i;
      while (j < text.size() && !std::isspace(static_cast<unsigned char>(text[j])))
         ++j;
      if (j > i)
         encodeWord(kMeta + text.substr(i, j - i), out);
      i = j;
   }
   if (maxLength > 0 && out.size() + 1 > maxLength)
      out.resize(maxLength - 1);
   out.push_back(kEos);
   return out;
}

} // namespace jkit::gen
