#include "spelling.h"

#include <algorithm>
#include <cstring>
#include <string_view>
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
constexpr Spelling spellings[] = {
#include "transpositions.inc"
};

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
        if (source->exhausted() || raw.size() < 3 || raw.size() > 6) {
            return source;
        }
        auto match = std::lower_bound(std::begin(spellings), std::end(spellings), raw,
            [](const Spelling &entry, std::string_view key) { return entry.first < key; });
        if (match == std::end(spellings) || match->first != raw) {
            return source;
        }
        auto hint = rime::New<rime::SimpleCandidate>(candidateType, 0, raw.size(),
            std::string(match->second), "〔纠错〕");
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
    char spelling[16] = {};
    if (!api->get_property(session, property, spelling, sizeof(spelling))) {
        return false;
    }
    api->set_property(session, property, "");
    return std::strcmp(spelling, commit) == 0 && api->set_input(session, spelling);
}
