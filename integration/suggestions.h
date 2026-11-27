#pragma once

#include <cstdint>
#include <functional>

namespace fcitx { class InputContext; class SurroundingText; }

// Rime filters run synchronously. Bind only the client that owns this operation.
class LuneInputScope {
public:
    using Fallback = std::function<const fcitx::SurroundingText *()>;
    LuneInputScope(fcitx::InputContext *input, Fallback fallback);
    ~LuneInputScope();
    LuneInputScope(const LuneInputScope &) = delete;
    LuneInputScope &operator=(const LuneInputScope &) = delete;
    static const fcitx::SurroundingText *surrounding();

private:
    const LuneInputScope *previous_;
    fcitx::InputContext *input_;
    Fallback fallback_;
};

void registerLuneSuggestions();
bool refreshLuneMenu(uintptr_t session);
