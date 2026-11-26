#include "caret.h"

#include <string>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/key_event.h>
#include <rime/key_table.h>
#include <rime/processor.h>
#include <rime/registry.h>
#include <rime/schema.h>

namespace {
class CaretBoundary : public rime::Processor {
public:
    explicit CaretBoundary(const rime::Ticket &ticket) : Processor(ticket) {
        std::string action;
        auto *config = engine_->schema()->config();
        editArrows_ = config->GetString("selector/linear/bindings/Right", &action) && action == "noop";
    }

    rime::ProcessResult ProcessKeyEvent(const rime::KeyEvent &key) override {
        if (!editArrows_ || key.modifier() != 0) {
            return rime::kNoop;
        }
        const int sym = key.keycode();
        const bool left = sym == XK_Left || sym == XK_KP_Left;
        const bool right = sym == XK_Right || sym == XK_KP_Right;
        if (!left && !right) {
            return rime::kNoop;
        }
        auto *ctx = engine_->context();
        // The stock navigator wraps at either edge. Leave actual moves to it.
        if (ctx->IsComposing() && ((left && ctx->caret_pos() == 0) ||
                                  (right && ctx->caret_pos() == ctx->input().size()))) {
            return rime::kAccepted;
        }
        return rime::kNoop;
    }

private:
    bool editArrows_;
};
}

void registerLuneCaretBoundary() {
    rime::Registry::instance().Register("lune_caret_boundary", new rime::Component<CaretBoundary>);
}
