#pragma once

namespace fcitx { class InputContext; }

// Rime filters run synchronously. Bind only the client that owns this operation.
class LuneInputScope {
public:
    explicit LuneInputScope(fcitx::InputContext *input);
    ~LuneInputScope();
    LuneInputScope(const LuneInputScope &) = delete;
    LuneInputScope &operator=(const LuneInputScope &) = delete;

private:
    fcitx::InputContext *previous_;
};

void registerLuneSuggestions();
