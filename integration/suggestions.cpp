#include "suggestions.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>
#include <fcitx-utils/utf8.h>
#include <libime/core/languagemodel.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/filter.h>
#include <rime/registry.h>
#include <rime/schema.h>
#include <rime/translation.h>

namespace {
thread_local fcitx::InputContext *currentInput = nullptr;
constexpr unsigned windowChars = 4;
constexpr size_t candidateLimit = 32;

bool isHan(uint32_t code) {
    return (code >= 0x3400 && code <= 0x4dbf) || (code >= 0x4e00 && code <= 0x9fff) ||
           (code >= 0x20000 && code <= 0x323af);
}

struct Neighbors {
    std::vector<std::string_view> left;
    std::vector<std::string_view> right;
};

Neighbors neighbors(fcitx::InputContext *ic) {
    Neighbors result;
    if (!ic || !ic->hasFocus() || !ic->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText) ||
        ic->capabilityFlags().testAny(fcitx::CapabilityFlag::PasswordOrSensitive)) {
        return result;
    }
    const auto &surrounding = ic->surroundingText();
    if (!surrounding.isValid() || surrounding.text().empty()) {
        return result;
    }
    const std::string &text = surrounding.text();
    const char *end = text.data() + text.size();
    const auto begin = std::min(surrounding.cursor(), surrounding.anchor());
    const auto finish = std::max(surrounding.cursor(), surrounding.anchor());
    const char *left = fcitx_utf8_get_nth_char(text.data(), begin);
    const char *right = fcitx_utf8_get_nth_char(left, finish - begin);
    const char *p = left;
    while (p > text.data() && result.left.size() < windowChars) {
        do { --p; } while ((static_cast<unsigned char>(*p) & 0xc0) == 0x80);
        if (!isHan(fcitx::utf8::getChar(p, left))) {
            break;
        }
        result.left.emplace_back(p, left - p);
    }
    p = right;
    while (p < end && result.right.size() < windowChars) {
        uint32_t code;
        p = fcitx::utf8::getNextChar(p, end, &code);
        if (!isHan(code)) {
            break;
        }
        result.right.emplace_back(right, p - right);
    }
    return result;
}

const libime::LanguageModel *chineseModel() {
    static const auto model = []() -> std::unique_ptr<libime::LanguageModel> {
        try {
            auto file = libime::DefaultLanguageModelResolver::instance().languageModelFileForLanguage("zh_CN");
            if (file) {
                return std::make_unique<libime::LanguageModel>(std::move(file));
            }
        } catch (const std::exception &) {
            // The model is optional. Rime still supplies the normal candidates.
        }
        return nullptr;
    }();
    return model.get();
}

struct ScoredWord {
    std::string_view text;
    float baseline;
    bool whole;
};

std::vector<ScoredWord> neighborWords(const libime::LanguageModel &model,
                                      const std::vector<std::string_view> &words) {
    std::vector<ScoredWord> result;
    for (auto word : words) {
        if (model.index(word) != model.unknown()) {
            // Prefer the whole neighboring word: 银行 is not a lone 行.
            result.clear();
            result.push_back({word, model.singleWordScore(word), true});
        } else {
            std::vector<std::string_view> chars;
            auto end = word.data() + word.size();
            for (auto p = word.data(); p < end;) {
                auto next = fcitx::utf8::nextChar(p);
                chars.emplace_back(p, next - p);
                p = next;
            }
            // A partial word such as 巧克 can form a known compound across the cursor.
            result.push_back({word, model.wordsScore(model.nullState(), chars), false});
        }
    }
    return result;
}

// Log likelihood gain for adjacent words over their independent frequencies.
// A compound can also be a single vocabulary entry in the system model.
float association(const libime::LanguageModel &model, ScoredWord left, ScoredWord right) {
    float baseline = left.baseline + right.baseline;
    float gain = left.whole && right.whole
        ? model.wordsScore(model.nullState(), {left.text, right.text}) - baseline : 0;
    std::string compound(left.text);
    compound += right.text;
    if (model.index(compound) != model.unknown()) {
        gain = std::max(gain, model.singleWordScore(compound) - baseline - 1.0f);
    }
    return gain;
}

void promote(const libime::LanguageModel &model, const Neighbors &context,
             size_t segmentEnd, rime::CandidateList &words) {
    const auto left = neighborWords(model, context.left);
    const auto right = neighborWords(model, context.right);
    size_t best = 0;
    float bestScore = 0.8f;  // Require a clear gain before changing dictionary order.
    for (size_t i = 0; i < words.size(); ++i) {
        const auto &text = words[i]->text();
        if (words[i]->end() != segmentEnd || text.empty() || !isHan(fcitx::utf8::getChar(text)) ||
            model.index(text) == model.unknown()) {
            continue;
        }
        ScoredWord candidate{text, model.singleWordScore(text), true};
        float before = 0, after = 0;
        for (auto word : left) {
            before = std::max(before, association(model, word, candidate));
        }
        for (auto word : right) {
            after = std::max(after, association(model, candidate, word));
        }
        float score = before + after - 0.035f * i;
        if (score > bestScore) {
            best = i;
            bestScore = score;
        }
    }
    if (best > 1) {
        std::rotate(words.begin() + 1, words.begin() + best, words.begin() + best + 1);
    }
}

class LuneSuggestions : public rime::Filter {
public:
    explicit LuneSuggestions(const rime::Ticket &ticket) : Filter(ticket) {
        auto *config = engine_->schema()->config();
        config->GetBool("lune/symbol_candidates", &symbols_);
        config->GetBool("lune/context_candidates", &context_);
    }

    bool AppliesToSegment(rime::Segment *segment) override {
        auto *ctx = engine_->context();
        return (symbols_ || context_) && engine_->schema()->page_size() >= 2 &&
               segment->end == ctx->input().size() && ctx->caret_pos() == segment->end &&
               segment->HasTag("abc");
    }

    rime::an<rime::Translation> Apply(rime::an<rime::Translation> source, rime::CandidateList *) override {
        if (source->exhausted()) {
            return source;
        }
        auto *ctx = engine_->context();
        const auto &segment = ctx->composition().back();
        rime::an<rime::ConfigList> symbols;
        if (symbols_) {
            auto code = ctx->input().substr(segment.start);
            code.erase(std::remove(code.begin(), code.end(), '\''), code.end());
            if (!code.empty() && code.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos) {
                symbols = engine_->schema()->config()->GetList("lune/symbols/" + code);
            }
        }
        auto context = context_ && segment.start == 0 ? neighbors(currentInput) : Neighbors{};
        auto *model = context.left.empty() && context.right.empty() ? nullptr : chineseModel();
        if (!symbols && !model) {
            return source;
        }
        rime::CandidateList words;
        const size_t limit = model ? candidateLimit : 1;
        while (words.size() < limit && !source->exhausted()) {
            if (auto word = source->Peek()) {
                words.push_back(word);
            }
            source->Next();
        }
        if (words.empty()) {
            return source;
        }
        if (model) {
            promote(*model, context, segment.end, words);
        }
        auto head = rime::New<rime::FifoTranslation>();
        head->Append(words.front());
        if (symbols) {
            for (size_t i = 0; i < symbols->size(); ++i) {
                auto value = symbols->GetValueAt(i);
                if (value && !value->str().empty() && value->str() != words.front()->text()) {
                    head->Append(rime::New<rime::SimpleCandidate>(
                        "lune_symbol", segment.start, segment.end, value->str()));
                }
            }
        }
        for (size_t i = 1; i < words.size(); ++i) {
            head->Append(words[i]);
        }
        auto result = rime::New<rime::UnionTranslation>();
        *result += head;
        *result += source;
        return result;
    }

private:
    bool symbols_ = false;
    bool context_ = false;
};
} // namespace

LuneInputScope::LuneInputScope(fcitx::InputContext *input) : previous_(currentInput) { currentInput = input; }
LuneInputScope::~LuneInputScope() { currentInput = previous_; }

void registerLuneSuggestions() {
    rime::Registry::instance().Register("lune_suggestions", new rime::Component<LuneSuggestions>);
}
