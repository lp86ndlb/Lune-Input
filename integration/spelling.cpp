#include "spelling.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/filter.h>
#include <rime/registry.h>
#include <rime/schema.h>
#include <rime/translation.h>

namespace {
constexpr char property[] = "_lune_selected_spelling";
constexpr char candidateType[] = "lune_spelling";
using Spelling = std::pair<std::string_view, std::string_view>;
#include "spellings.inc"

bool isSyllable(std::string_view text) {
    return std::binary_search(std::begin(syllables), std::end(syllables), text);
}

// Mark offsets reachable using complete syllables from either end of the input.
// An explicit apostrophe is a boundary and survives the correction.
std::vector<bool> pinyinBoundaries(std::string_view raw, bool fromEnd) {
    const size_t n = raw.size();
    std::vector<bool> boundaries(n + 1, false);
    boundaries[fromEnd ? n : 0] = true;
    for (size_t i = 0; i < n; ++i) {
        const size_t at = fromEnd ? n - i : i;
        if (!boundaries[at]) {
            continue;
        }
        for (size_t len = 1; len <= std::min(maxSyllableLength, n - i); ++len) {
            const size_t start = fromEnd ? at - len : at;
            auto token = raw.substr(start, len);
            if (token == "'" || isSyllable(token)) {
                boundaries[fromEnd ? start : at + len] = true;
            }
        }
    }
    return boundaries;
}

// Removing [begin, end) either joins two complete syllables, or reconnects
// the two halves of one syllable. The rest of the phrase has already been checked.
bool joinsPinyin(std::string_view raw, size_t begin, size_t end,
                 const std::vector<bool> &prefix, const std::vector<bool> &suffix) {
    if (prefix[begin] && suffix[end]) {
        return true;
    }
    for (size_t left = 1; left < maxSyllableLength && left <= begin; ++left) {
        if (!prefix[begin - left]) {
            continue;
        }
        std::string syllable(raw.substr(begin - left, left));
        const size_t rightLimit = std::min(raw.size() - end, maxSyllableLength - left);
        for (size_t right = 1; right <= rightLimit; ++right) {
            syllable.push_back(raw[end + right - 1]);
            if (suffix[end + right] && isSyllable(syllable)) {
                return true;
            }
        }
    }
    return false;
}

std::string spellingHint(std::string_view raw) {
    if (raw.size() < 3) {
        return {};
    }
    if (raw.size() <= maxSyllableLength) {
        auto it = std::lower_bound(std::begin(transpositions), std::end(transpositions), raw,
            [](const Spelling &entry, std::string_view key) { return entry.first < key; });
        if (it != std::end(transpositions) && it->first == raw) {
            return std::string(it->second);
        }
    }

    auto first = std::adjacent_find(raw.begin(), raw.end());
    if (first == raw.end() || std::binary_search(std::begin(protectedCodes), std::end(protectedCodes), raw)) {
        return {};
    }
    const auto prefix = pinyinBoundaries(raw, false);
    if (prefix.back()) {
        return {};
    }
    const auto suffix = pinyinBoundaries(raw, true);
    size_t bestEnd = std::string_view::npos;
    size_t fewestRemoved = raw.size();
    for (size_t start = first - raw.begin(); start < raw.size();) {
        size_t end = start + 1;
        while (end < raw.size() && raw[end] == raw[start]) {
            ++end;
        }
        if (raw[start] >= 'a' && raw[start] <= 'z') {
            for (size_t drop = 1; drop < end - start && drop <= fewestRemoved; ++drop) {
                if (joinsPinyin(raw, end - drop, end, prefix, suffix)) {
                    // Equally short repairs in different runs are ambiguous.
                    bestEnd = drop < fewestRemoved ? end : std::string_view::npos;
                    fewestRemoved = drop;
                    break;
                }
            }
        }
        start = end;
    }
    if (bestEnd == std::string_view::npos) {
        return {};
    }
    std::string corrected(raw);
    corrected.erase(bestEnd - fewestRemoved, fewestRemoved);
    return corrected;
}

// The Rime menu owns paging, highlighting, number keys and mouse selection.
// Interleave a hint after its first result without dropping any original word.
class WithSpellingHint : public rime::Translation {
public:
    WithSpellingHint(rime::an<rime::Translation> source, rime::an<rime::Candidate> hint)
        : source_(std::move(source)), hint_(std::move(hint)) {}

    rime::an<rime::Candidate> Peek() override {
        return atHint_ ? hint_ : source_->Peek();
    }

    bool Next() override {
        if (atHint_) {
            atHint_ = false;
            hint_.reset();
        } else {
            source_->Next();
            atHint_ = bool(hint_);
        }
        set_exhausted(!atHint_ && source_->exhausted());
        return !exhausted();
    }

private:
    rime::an<rime::Translation> source_;
    rime::an<rime::Candidate> hint_;
    bool atHint_ = false;
};

class LuneSpelling : public rime::Filter {
public:
    explicit LuneSpelling(const rime::Ticket &ticket) : Filter(ticket) {
        auto *config = engine_->schema()->config();
        config->GetBool("lune/spelling_hints", &enabled_);
        enabled_ = enabled_ && engine_->schema()->page_size() >= 2;
        if (enabled_) {
            selected_ = engine_->context()->commit_notifier().connect([](rime::Context *ctx) {
                auto candidate = ctx->GetSelectedCandidate();
                if (candidate && candidate->type() == candidateType &&
                    ctx->GetCommitText() == candidate->text()) {
                    ctx->set_property(property, candidate->text());
                }
            });
        }
    }

    bool AppliesToSegment(rime::Segment *segment) override {
        auto *ctx = engine_->context();
        return enabled_ && segment->start == 0 && segment->end == ctx->input().size() &&
               ctx->caret_pos() == segment->end && segment->HasTag("abc");
    }

    rime::an<rime::Translation> Apply(rime::an<rime::Translation> source,
                                      rime::CandidateList *) override {
        const auto &raw = engine_->context()->input();
        if (source->exhausted()) {
            return source;
        }
        auto corrected = spellingHint(raw);
        if (corrected.empty()) {
            return source;
        }
        auto hint = rime::New<rime::SimpleCandidate>(candidateType, 0, raw.size(),
            corrected, "〔纠错〕");
        return rime::New<WithSpellingHint>(std::move(source), std::move(hint));
    }

private:
    bool enabled_ = true;
    boost::signals2::scoped_connection selected_;
};
} // namespace

void registerLuneSpelling() {
    rime::Registry::instance().Register(candidateType, new rime::Component<LuneSpelling>);
}

bool applyLuneSpelling(RimeApi *api, RimeSessionId session, const char *commit) {
    std::string spelling(std::strlen(commit) + 1, '\0');
    if (!api->get_property(session, property, spelling.data(), spelling.size())) {
        return false;
    }
    api->set_property(session, property, "");
    return std::strncmp(spelling.data(), commit, spelling.size()) == 0 && api->set_input(session, commit);
}
