#include "desktopcontext.h"

#include <algorithm>
#include <chrono>
#include <tuple>
#include <utility>
#include <vector>
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/message.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/dbus/variant.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>

namespace {
using namespace fcitx::dbus;
using Clock = std::chrono::steady_clock;
using Object = DBusStruct<std::string, ObjectPath>;
using Selection = DBusStruct<Object, int32_t, Object, int32_t, bool>;
using Attributes = std::vector<DictEntry<std::string, std::string>>;
using Integers = std::vector<int32_t>;
using Rule = DBusStruct<Integers, int32_t, Attributes, int32_t, Integers, int32_t,
                        std::vector<std::string>, int32_t, bool>;
constexpr char accessible[] = "org.a11y.atspi.Accessible";
constexpr char textInterface[] = "org.a11y.atspi.Text";
constexpr char root[] = "/org/a11y/atspi/accessible/root";
constexpr char dbusName[] = "org.freedesktop.DBus";
constexpr char dbusPath[] = "/org/freedesktop/DBus";
// AT-SPI wire constants for focused + editable, canonical order, MATCH_ALL.
constexpr uint32_t focusedEditable = (1u << 12) | (1u << 7);
constexpr uint32_t passwordRole = 40;
constexpr uint32_t webDocumentRole = 95;
constexpr int32_t windowSize = 4;

struct Span {
    std::string left, right;
};

// All calls run off the input thread. Bound the whole lookup, including an
// unresponsive application, and abandon it when the composition goes away.
class Lookup {
public:
    explicit Lookup(std::weak_ptr<fcitx::SurroundingText> target) : target_(std::move(target)) {}

    template<typename... Args>
    Message call(Bus &bus, const std::string &owner, const std::string &path,
                 const char *iface, const char *method, const Args &...args) const {
        using namespace std::chrono;
        auto remaining = duration_cast<microseconds>(until_ - Clock::now()).count();
        if (target_.expired() || remaining <= 0) {
            return {};
        }
        auto msg = bus.createMethodCall(owner.c_str(), path.c_str(), iface, method);
        (msg << ... << args);
        return msg.call(std::min<int64_t>(remaining, 100000));
    }

    uint32_t pid(Bus &bus, const std::string &owner) const {
        uint32_t result = 0;
        call(bus, dbusName, dbusPath, dbusName, "GetConnectionUnixProcessID", owner) >> result;
        return result;
    }

    // Some Chromium versions report a collapsed Text.GetSelection after a
    // non-BMP character. The document exposes both endpoints in code points.
    bool documentSelection(Bus &bus, const std::string &owner, const std::string &field,
                           int32_t &begin, int32_t &end) const {
        std::string path = field;
        for (int depth = 0; depth < 24; ++depth) {
            Object parent;
            auto reply = call(bus, owner, path, "org.freedesktop.DBus.Properties", "Get", accessible, "Parent");
            if (reply.type() != MessageType::Reply ||
                !(reply >> Container(Container::Type::Variant, Signature("(so)")) >> parent) ||
                std::get<0>(parent.data()) != owner) {
                return false;
            }
            path = std::get<1>(parent.data()).path();
            uint32_t role = 0;
            call(bus, owner, path, accessible, "GetRole") >> role;
            if (!role || path == root) {
                return false;
            }
            if (role == webDocumentRole) {
                std::vector<Selection> ranges;
                call(bus, owner, path, "org.a11y.atspi.Document", "GetTextSelections") >> ranges;
                if (ranges.size() != 1) {
                    return false;
                }
                const auto &[first, start, last, finish, reversed] = ranges[0].data();
                if (std::get<0>(first.data()) != owner || std::get<1>(first.data()).path() != field ||
                    std::get<0>(last.data()) != owner || std::get<1>(last.data()).path() != field) {
                    return false;
                }
                begin = start;
                end = finish;
                return true;
            }
        }
        return false;
    }

    std::optional<Span> read(Bus &bus, const std::string &owner, const ObjectPath &app) const {
        // Asking the active application's root about its attributes lets
        // Chromium publish its accessibility tree on demand.
        call(bus, owner, app.path(), accessible, "GetAttributes");
        const Rule rule{Integers{static_cast<int32_t>(focusedEditable), 0}, 1, Attributes{}, 1,
                        Integers(4, 0), 1, std::vector<std::string>{}, 1, false};
        std::vector<Object> hits;
        for (int i = 0; i < 5; ++i) {
            auto reply = call(bus, owner, app.path(), "org.a11y.atspi.Collection", "GetMatches",
                              rule, uint32_t(1), int32_t(2), true);
            if (reply.type() != MessageType::Reply) {
                return {};
            }
            reply >> hits;
            if (!hits.empty()) {
                break;
            }
            if (i != 4) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        }
        if (hits.size() != 1 || std::get<0>(hits[0].data()) != owner) {
            return {};
        }
        const auto &path = std::get<1>(hits[0].data()).path();
        uint32_t role = 0;
        call(bus, owner, path, accessible, "GetRole") >> role;
        if (!role || role == passwordRole) {
            return {};
        }
        auto focused = [&]() {
            std::vector<uint32_t> state;
            call(bus, owner, path, accessible, "GetState") >> state;
            return !state.empty() && (state[0] & focusedEditable) == focusedEditable;
        };
        auto property = [&](const char *name) {
            Variant value;
            call(bus, owner, path, "org.freedesktop.DBus.Properties", "Get", textInterface, name) >> value;
            return value.signature() == "i" ? value.dataAs<int32_t>() : -1;
        };
        if (!focused()) {
            return {};
        }
        int32_t length = property("CharacterCount");
        int32_t caret = property("CaretOffset");
        int32_t begin = caret, end = caret;
        int32_t selections = -1;
        call(bus, owner, path, textInterface, "GetNSelections") >> selections;
        if (selections == 1) {
            auto selection = call(bus, owner, path, textInterface, "GetSelection", int32_t(0));
            if ((selection.type() != MessageType::Reply || !(selection >> begin >> end) ||
                 begin == end || (caret != begin && caret != end)) &&
                !documentSelection(bus, owner, path, begin, end)) {
                return {};
            }
            if (begin == end || (caret != begin && caret != end)) {
                return {};
            }
            if (begin > end) {
                std::swap(begin, end);
            }
        }
        if (selections < 0 || selections > 1 || length <= 0 || begin < 0 || end > length || end < begin) {
            return {};
        }
        Span span;
        if (begin > 0) {
            call(bus, owner, path, textInterface, "GetText", std::max(0, begin - windowSize), begin) >> span.left;
        }
        if (end < length) {
            call(bus, owner, path, textInterface, "GetText", end, end + std::min(windowSize, length - end)) >> span.right;
        }
        if ((span.left.empty() && span.right.empty()) || !focused() ||
            fcitx::utf8::lengthValidated(span.left) > windowSize ||
            fcitx::utf8::lengthValidated(span.right) > windowSize) {
            return {};
        }
        return span;
    }

private:
    std::weak_ptr<fcitx::SurroundingText> target_;
    Clock::time_point until_ = Clock::now() + std::chrono::milliseconds(320);
};

std::optional<Span> readClient(const Lookup &lookup, const std::string &busAddress,
                               const std::string &sender) {
    Bus session(busAddress);
    if (!session.isOpen()) {
        return {};
    }
    auto client = lookup.pid(session, sender);
    if (!client) {
        return {};
    }
    std::string address;
    lookup.call(session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress") >> address;
    if (address.empty()) {
        return {};
    }
    Bus bus(address);
    if (!bus.isOpen()) {
        return {};
    }
    std::vector<Object> apps;
    lookup.call(bus, "org.a11y.atspi.Registry", root, accessible, "GetChildren") >> apps;
    for (const auto &app : apps) {
        const auto &[owner, path] = app.data();
        // Match the actual sender of this key, even with several profiles or
        // applications that report the same program name.
        if (!owner.empty() && owner.front() == ':' && lookup.pid(bus, owner) == client) {
            return lookup.read(bus, owner, path);
        }
    }
    return {};
}
} // namespace

void LuneDesktopReader::request(fcitx::InputContext &input, std::weak_ptr<fcitx::SurroundingText> target,
                                std::function<void()> refresh) {
    auto *object = dynamic_cast<fcitx::dbus::ObjectVTableBase *>(&input);
    if (!object || !object->bus() || !object->currentMessage()) {
        return;
    }
    auto sender = object->currentMessage()->sender();
    if (sender.empty() || sender.front() != ':') {
        return;
    }
    std::lock_guard lock(mutex_);
    pending_ = Request{object->bus()->address(), std::move(sender), std::move(target), std::move(refresh)};
    if (!worker_.joinable()) {
        worker_ = std::thread([this] { run(); });
    }
    wake_.notify_one();
}

LuneDesktopReader::~LuneDesktopReader() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        pending_.reset();
    }
    wake_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void LuneDesktopReader::run() {
    for (;;) {
        Request job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
            if (stopping_) {
                return;
            }
            job = std::move(*pending_);
            pending_.reset();
        }
        try {
            if (job.target.expired()) {
                continue;
            }
            if (auto span = readClient(Lookup{job.target}, job.busAddress, job.sender)) {
                dispatcher_.schedule([job = std::move(job), span = std::move(*span)] {
                    if (auto text = job.target.lock()) {
                        auto at = fcitx::utf8::length(span.left);
                        text->setText(span.left + span.right, at, at);
                        job.refresh();
                    }
                });
            }
        } catch (const std::exception &) {
            // This client or its accessibility service may have exited.
        }
    }
}
