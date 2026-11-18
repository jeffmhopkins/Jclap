#pragma once

// The T5 tokenizer Stable Audio's text encoder reads: a SentencePiece Unigram model, loaded from the
// Hugging Face tokenizer.json that comes with the model files.
//
// A prompt is split on whitespace, each word gets "▁" in front (the Metaspace pre-tokenizer), and
// each word is cut into the vocabulary's pieces with the highest total log-probability (Viterbi).
// Characters no piece covers become <unk>, runs of them fused into one. </s> ends the ids.
//
// The full T5 normalizer (NFKC through a precompiled character map) is left out: prompts are typed
// English, where it changes nothing, and anything outside ASCII goes through as it is.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace jkit::gen {

class Tokenizer {
public:
   static constexpr int kPad = 0, kEos = 1, kUnk = 2;

   // Reads the vocabulary from tokenizer.json's text. False if it has none.
   bool load(const std::string &json);
   bool loaded() const { return !pieces_.empty(); }
   size_t size() const { return scores_.size(); }

   // The prompt's ids, ending with </s>, cut to `maxLength` (the last kept id is still </s>).
   std::vector<int64_t> encode(const std::string &text, size_t maxLength = 64) const;

private:
   void encodeWord(const std::string &word, std::vector<int64_t> &out) const;

   std::unordered_map<std::string, int> pieces_;
   std::vector<float> scores_;
   size_t maxPieceBytes_ = 0;
   float unkScore_ = -10.0f;
};

} // namespace jkit::gen
