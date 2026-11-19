#include "gen/orb/planner.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace jkit::orb {

// ============================================================================
// Grammar
// ============================================================================

Grammar::Grammar(const PlanOptions &options) : options_(options) {}

std::vector<int> Grammar::digitsBelow(int limit) const {
   int scale = 1;
   for (int i = 0; i < left_ - 1; ++i)
      scale *= 10;
   std::vector<int> out;
   for (int d = 0; d < 10; ++d)
      if (static_cast<int64_t>(value_ * 10 + d) * scale <= limit)
         out.push_back(tok::DIGIT + d);
   return out;
}

std::vector<int> Grammar::allowed() const {
   using namespace tok;
   const auto range = [](int lo, int count) {
      std::vector<int> v(static_cast<size_t>(count));
      for (int i = 0; i < count; ++i)
         v[static_cast<size_t>(i)] = lo + i;
      return v;
   };
   switch (state_) {
   case St::Start: return {SPEC};
   case St::Dur: return {DUR};
   case St::DurDigits:
      if (options_.durationCs >= 0) { // the one digit that spells the forced length
         int div = 1;
         for (int i = 0; i < left_ - 1; ++i)
            div *= 10;
         return {DIGIT + (options_.durationCs / div) % 10};
      }
      return digitsBelow(99999);
   case St::Tex: return {TEX};
   case St::TexVal: {
      if (options_.noBed)
         return {NOBED};
      std::vector<int> v = range(TEXVAL, 8);
      for (int i = 8; i < TEXTURES; ++i)
         v.push_back(ROLEVAL + ROLES + i - 8);
      v.push_back(NOBED);
      std::sort(v.begin(), v.end());
      return v;
   }
   case St::Mood: return {MOOD};
   case St::MoodVal: return range(MOODVAL, MOODS);
   case St::Events: return {EVENTS};
   case St::EventOrEnd:
      if (bedless_ && events_ == 0)
         return {EVENT};
      return events_ < MAX_EVENTS ? std::vector<int>{EVENT, END_EVENTS} : std::vector<int>{END_EVENTS};
   case St::Role: {
      if (options_.roles.empty())
         return range(ROLEVAL, ROLES);
      std::vector<int> v;
      for (int r : options_.roles)
         v.push_back(ROLEVAL + r);
      std::sort(v.begin(), v.end());
      return v;
   }
   case St::T: return {T};
   case St::TDigits: return digitsBelow(std::max(0, durationCs_ - 2));
   case St::D: return {D};
   case St::DDigits: return digitsBelow(std::max(0, durationCs_ - tCs_));
   case St::G: return {G};
   case St::GainVal: return range(GAIN, GAIN_LEVELS);
   case St::P: return {P};
   case St::PanVal: return range(PAN, PAN_LEVELS);
   case St::S: return {S};
   case St::HexDigits: return range(HEX, 16);
   case St::EndSpec: return {END_SPEC};
   case St::Done: return {EOS};
   }
   return {};
}

void Grammar::advance(int token) {
   using namespace tok;
   switch (state_) {
   case St::Start: state_ = St::Dur; break;
   case St::Dur:
      state_ = St::DurDigits;
      left_ = 5;
      value_ = 0;
      break;
   case St::DurDigits:
      value_ = value_ * 10 + (token - DIGIT);
      if (--left_ == 0) {
         durationCs_ = value_;
         value_ = 0;
         state_ = St::Tex;
      }
      break;
   case St::Tex: state_ = St::TexVal; break;
   case St::TexVal:
      bedless_ = token == NOBED;
      state_ = St::Mood;
      break;
   case St::Mood: state_ = St::MoodVal; break;
   case St::MoodVal: state_ = St::Events; break;
   case St::Events: state_ = St::EventOrEnd; break;
   case St::EventOrEnd:
      if (token == EVENT) {
         ++events_;
         state_ = St::Role;
      } else {
         state_ = St::EndSpec;
      }
      break;
   case St::Role: state_ = St::T; break;
   case St::T:
      state_ = St::TDigits;
      left_ = 4;
      value_ = 0;
      break;
   case St::TDigits:
      value_ = value_ * 10 + (token - DIGIT);
      if (--left_ == 0) {
         tCs_ = value_;
         value_ = 0;
         state_ = St::D;
      }
      break;
   case St::D:
      state_ = St::DDigits;
      left_ = 4;
      value_ = 0;
      break;
   case St::DDigits:
      value_ = value_ * 10 + (token - DIGIT);
      if (--left_ == 0) {
         value_ = 0;
         state_ = St::G;
      }
      break;
   case St::G: state_ = St::GainVal; break;
   case St::GainVal: state_ = St::P; break;
   case St::P: state_ = St::PanVal; break;
   case St::PanVal: state_ = St::S; break;
   case St::S:
      state_ = St::HexDigits;
      left_ = 2;
      break;
   case St::HexDigits:
      if (--left_ == 0)
         state_ = St::EventOrEnd;
      break;
   case St::EndSpec: state_ = St::Done; break;
   case St::Done: break;
   }
}

// ============================================================================
// Loading
// ============================================================================

namespace {

// Finds `"name":{...,"data_offsets":[a,b]}` in a safetensors header and checks it is F32 of `count`.
bool tensor(const std::string &header, const std::vector<char> &data, const std::string &name, size_t count,
            std::vector<float> &out, std::string &error) {
   const size_t at = header.find("\"" + name + "\"");
   if (at == std::string::npos) {
      error = "the planner weights have no " + name;
      return false;
   }
   const size_t end = header.find('}', at);
   const std::string entry = header.substr(at, end - at);
   if (entry.find("\"F32\"") == std::string::npos) {
      error = name + " isn't float32";
      return false;
   }
   const size_t off = entry.find("\"data_offsets\"");
   const size_t open = entry.find('[', off);
   if (off == std::string::npos || open == std::string::npos) {
      error = name + " has no data offsets";
      return false;
   }
   const unsigned long long a = std::strtoull(entry.c_str() + open + 1, nullptr, 10);
   const size_t comma = entry.find(',', open);
   const unsigned long long b = std::strtoull(entry.c_str() + comma + 1, nullptr, 10);
   if (b < a || b - a != count * sizeof(float) || b > data.size()) {
      error = name + " has the wrong size";
      return false;
   }
   out.resize(count);
   std::memcpy(out.data(), data.data() + a, count * sizeof(float));
   return true;
}

void layerNorm(const float *x, const std::vector<float> &w, const std::vector<float> &b, int d, float *out) {
   double mu = 0.0;
   for (int i = 0; i < d; ++i)
      mu += x[i];
   mu /= d;
   double var = 0.0;
   for (int i = 0; i < d; ++i)
      var += (x[i] - mu) * (x[i] - mu);
   var /= d;
   const double inv = 1.0 / std::sqrt(var + 1e-5);
   for (int i = 0; i < d; ++i)
      out[i] = static_cast<float>((x[i] - mu) * inv) * w[static_cast<size_t>(i)] + b[static_cast<size_t>(i)];
}

// out[rows] = W (rows x cols, row-major) . x[cols] (+ bias)
void matvec(const std::vector<float> &w, const float *x, int rows, int cols, const float *bias, float *out) {
   for (int r = 0; r < rows; ++r) {
      const float *row = w.data() + static_cast<size_t>(r) * static_cast<size_t>(cols);
      float acc0 = 0.f, acc1 = 0.f, acc2 = 0.f, acc3 = 0.f;
      int c = 0;
      for (; c + 4 <= cols; c += 4) {
         acc0 += row[c] * x[c];
         acc1 += row[c + 1] * x[c + 1];
         acc2 += row[c + 2] * x[c + 2];
         acc3 += row[c + 3] * x[c + 3];
      }
      for (; c < cols; ++c)
         acc0 += row[c] * x[c];
      out[r] = (acc0 + acc1) + (acc2 + acc3) + (bias ? bias[r] : 0.f);
   }
}

} // namespace

bool Planner::load(const std::filesystem::path &file, std::string &error) {
   unload();
   std::ifstream in(file, std::ios::binary);
   std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
   if (bytes.size() < 8) {
      error = "couldn't read the planner weights";
      return false;
   }
   uint64_t n = 0;
   for (int i = 7; i >= 0; --i)
      n = (n << 8) | static_cast<unsigned char>(bytes[static_cast<size_t>(i)]);
   if (n > bytes.size() - 8) {
      error = "the planner weights are damaged";
      return false;
   }
   const std::string header(bytes.data() + 8, n);
   const std::vector<char> data(bytes.begin() + static_cast<std::ptrdiff_t>(8 + n), bytes.end());
   const size_t d = static_cast<size_t>(d_), v = static_cast<size_t>(vocab_);
   bool ok = tensor(header, data, "tok_emb.weight", v * d, tokEmb_, error) &&
             tensor(header, data, "pos_emb.weight", static_cast<size_t>(ctx_) * d, posEmb_, error) &&
             tensor(header, data, "ln_f.weight", d, lnfW_, error) && tensor(header, data, "ln_f.bias", d, lnfB_, error) &&
             tensor(header, data, "head.weight", v * d, head_, error);
   blocks_.resize(static_cast<size_t>(layers_));
   for (int i = 0; ok && i < layers_; ++i) {
      Block &b = blocks_[static_cast<size_t>(i)];
      const std::string p = "blocks." + std::to_string(i) + ".";
      ok = tensor(header, data, p + "ln1.weight", d, b.ln1w, error) && tensor(header, data, p + "ln1.bias", d, b.ln1b, error) &&
           tensor(header, data, p + "attn.qkv.weight", 3 * d * d, b.qkv, error) &&
           tensor(header, data, p + "attn.proj.weight", d * d, b.proj, error) &&
           tensor(header, data, p + "ln2.weight", d, b.ln2w, error) && tensor(header, data, p + "ln2.bias", d, b.ln2b, error) &&
           tensor(header, data, p + "mlp.0.weight", 4 * d * d, b.fc1, error) &&
           tensor(header, data, p + "mlp.0.bias", 4 * d, b.fc1b, error) &&
           tensor(header, data, p + "mlp.2.weight", 4 * d * d, b.fc2, error) &&
           tensor(header, data, p + "mlp.2.bias", d, b.fc2b, error);
   }
   if (!ok)
      unload();
   return ok;
}

void Planner::unload() {
   tokEmb_.clear();
   posEmb_.clear();
   lnfW_.clear();
   lnfB_.clear();
   head_.clear();
   blocks_.clear();
}

// ============================================================================
// The transformer, one position at a time with a KV cache
// ============================================================================

void Planner::step(int token, int pos, Cache &cache, std::vector<float> *logits) const {
   const int d = d_, hd = d_ / heads_;
   std::vector<float> x(static_cast<size_t>(d)), h(static_cast<size_t>(d)), qkv(static_cast<size_t>(3 * d)),
      y(static_cast<size_t>(d)), t(static_cast<size_t>(d)), m(static_cast<size_t>(4 * d));
   for (int i = 0; i < d; ++i)
      x[static_cast<size_t>(i)] = tokEmb_[static_cast<size_t>(token * d + i)] + posEmb_[static_cast<size_t>(pos * d + i)];
   const float scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(hd)));
   std::vector<float> att;
   for (int l = 0; l < layers_; ++l) {
      const Block &b = blocks_[static_cast<size_t>(l)];
      layerNorm(x.data(), b.ln1w, b.ln1b, d, h.data());
      matvec(b.qkv, h.data(), 3 * d, d, nullptr, qkv.data());
      std::vector<float> &K = cache.k[static_cast<size_t>(l)], &V = cache.v[static_cast<size_t>(l)];
      K.insert(K.end(), qkv.begin() + d, qkv.begin() + 2 * d);
      V.insert(V.end(), qkv.begin() + 2 * d, qkv.end());
      const int n = pos + 1;
      att.resize(static_cast<size_t>(n));
      for (int head = 0; head < heads_; ++head) {
         const float *q = qkv.data() + head * hd;
         float mx = -INFINITY;
         for (int j = 0; j < n; ++j) {
            const float *k = K.data() + static_cast<size_t>(j * d + head * hd);
            float s = 0.f;
            for (int c = 0; c < hd; ++c)
               s += q[c] * k[c];
            att[static_cast<size_t>(j)] = s * scale;
            mx = std::max(mx, att[static_cast<size_t>(j)]);
         }
         float total = 0.f;
         for (int j = 0; j < n; ++j) {
            att[static_cast<size_t>(j)] = std::exp(att[static_cast<size_t>(j)] - mx);
            total += att[static_cast<size_t>(j)];
         }
         for (int c = 0; c < hd; ++c) {
            float s = 0.f;
            for (int j = 0; j < n; ++j)
               s += att[static_cast<size_t>(j)] * V[static_cast<size_t>(j * d + head * hd + c)];
            y[static_cast<size_t>(head * hd + c)] = s / total;
         }
      }
      matvec(b.proj, y.data(), d, d, nullptr, t.data());
      for (int i = 0; i < d; ++i)
         x[static_cast<size_t>(i)] += t[static_cast<size_t>(i)];
      layerNorm(x.data(), b.ln2w, b.ln2b, d, h.data());
      matvec(b.fc1, h.data(), 4 * d, d, b.fc1b.data(), m.data());
      for (float &v : m) // exact-erf GELU
         v = 0.5f * v * (1.0f + std::erf(v * 0.70710678118654752f));
      matvec(b.fc2, m.data(), d, 4 * d, b.fc2b.data(), t.data());
      for (int i = 0; i < d; ++i)
         x[static_cast<size_t>(i)] += t[static_cast<size_t>(i)];
   }
   if (logits) {
      layerNorm(x.data(), lnfW_, lnfB_, d, h.data());
      logits->resize(static_cast<size_t>(vocab_));
      matvec(head_, h.data(), vocab_, d, nullptr, logits->data());
   }
}

std::vector<int> Planner::planIds(const std::string &caption, const PlanOptions &options,
                                  const std::atomic<bool> *cancel) const {
   std::vector<int> out;
   if (!loaded())
      return out;
   std::vector<int> prompt = {tok::BOS};
   for (unsigned char c : caption)
      prompt.push_back(c);
   // Room for the longest Spec (about 200 tokens): a very long caption loses its end.
   const size_t maxPrompt = static_cast<size_t>(ctx_) - 260;
   if (prompt.size() > maxPrompt)
      prompt.resize(maxPrompt);
   prompt.push_back(tok::SEP);

   Grammar grammar(options);
   Cache cache;
   cache.k.resize(static_cast<size_t>(layers_));
   cache.v.resize(static_cast<size_t>(layers_));
   std::vector<float> logits;
   std::vector<int> allowed = grammar.allowed();
   uint64_t state = options.sampleSeed;
   int pos = 0;
   for (size_t i = 0; i < prompt.size(); ++i, ++pos)
      step(prompt[i], pos, cache, i + 1 == prompt.size() && allowed.size() > 1 ? &logits : nullptr);
   while (pos < ctx_) {
      if (cancel && cancel->load())
         break;
      int token = allowed.front();
      if (allowed.size() > 1 && options.temperature > 0.0) {
         float top = -INFINITY;
         for (int a : allowed)
            top = std::max(top, logits[static_cast<size_t>(a)]);
         std::vector<double> w(allowed.size());
         double sum = 0.0;
         for (size_t i = 0; i < allowed.size(); ++i)
            sum += w[i] = std::exp((logits[static_cast<size_t>(allowed[i])] - top) / options.temperature);
         // splitmix64, one draw per token
         state += 0x9E3779B97F4A7C15ull;
         uint64_t z = state;
         z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
         z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
         z ^= z >> 31;
         double pick = static_cast<double>(z >> 11) * 0x1.0p-53 * sum;
         token = allowed.back();
         for (size_t i = 0; i < allowed.size(); ++i)
            if ((pick -= w[i]) < 0.0) {
               token = allowed[i];
               break;
            }
      } else if (allowed.size() > 1) {
         float best = -INFINITY;
         for (int a : allowed) // the first of equal maxima, as np.argmax over the sorted ids
            if (logits[static_cast<size_t>(a)] > best) {
               best = logits[static_cast<size_t>(a)];
               token = a;
            }
      }
      out.push_back(token);
      if (token == tok::EOS)
         break;
      grammar.advance(token);
      allowed = grammar.allowed();
      step(token, pos, cache, allowed.size() > 1 ? &logits : nullptr);
      ++pos;
   }
   return out;
}

bool Planner::decode(const std::vector<int> &ids, Spec &spec) {
   using namespace tok;
   std::vector<int> toks;
   for (int t : ids)
      if (t != PAD && t != BOS && t != EOS)
         toks.push_back(t);
   size_t p = 0;
   bool ok = true;
   const auto take = [&](int expected = -1) {
      if (p >= toks.size()) {
         ok = false;
         return -1;
      }
      const int t = toks[p++];
      if (expected >= 0 && t != expected)
         ok = false;
      return t;
   };
   const auto digits = [&](int width) {
      int v = 0;
      for (int i = 0; i < width; ++i) {
         const int t = take();
         if (t < DIGIT || t >= DIGIT + 10)
            ok = false;
         v = v * 10 + (t - DIGIT);
      }
      return v;
   };
   const auto oneOf = [&](int lo, int count) {
      const int t = take();
      if (t < lo || t >= lo + count)
         ok = false;
      return t - lo;
   };
   take(SPEC);
   take(DUR);
   spec = Spec{};
   spec.duration = digits(5) / 100.0;
   take(TEX);
   take();
   take(MOOD);
   oneOf(MOODVAL, MOODS);
   take(EVENTS);
   while (ok && p < toks.size() && toks[p] != END_EVENTS) {
      take(EVENT);
      SpecEvent ev;
      ev.role = oneOf(ROLEVAL, ROLES);
      take(T);
      ev.t = digits(4) / 100.0;
      take(D);
      ev.dur = digits(4) / 100.0;
      take(G);
      ev.gainDb = 2.0 * oneOf(GAIN, GAIN_LEVELS) - 30.0;
      take(P);
      ev.pan = oneOf(PAN, PAN_LEVELS) / 8.0 * 2.0 - 1.0;
      take(S);
      const int hi = oneOf(HEX, 16), lo = oneOf(HEX, 16);
      ev.seed = hi * 16 + lo;
      if (spec.events.size() >= static_cast<size_t>(MAX_EVENTS))
         ok = false;
      spec.events.push_back(ev);
   }
   take(END_EVENTS);
   take(END_SPEC);
   return ok;
}

} // namespace jkit::orb
