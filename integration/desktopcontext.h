#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace fcitx {
class EventDispatcher;
class InputContext;
class SurroundingText;
}

// One worker per engine, started on the first request from a client without
// native surrounding text. Only the most recent composition needs an answer.
class LuneDesktopReader {
public:
    explicit LuneDesktopReader(fcitx::EventDispatcher &dispatcher) : dispatcher_(dispatcher) {}
    ~LuneDesktopReader();
    void request(fcitx::InputContext &input, std::weak_ptr<fcitx::SurroundingText> target,
                 std::function<void()> refresh);

private:
    struct Request {
        std::string busAddress;
        std::string sender;
        std::weak_ptr<fcitx::SurroundingText> target;
        std::function<void()> refresh;
    };
    void run();
    fcitx::EventDispatcher &dispatcher_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Request> pending_;
    std::thread worker_;
    bool stopping_ = false;
};
