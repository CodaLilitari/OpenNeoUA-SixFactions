#ifndef OPENNEOUA_AGENT_PROTOCOL_H
#define OPENNEOUA_AGENT_PROTOCOL_H
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace Agent {
enum class Op { Observe, Key, Move, Click, Text, Release };
struct Command {
    uint32_t id = 0;
    uint64_t expires = 0;
    Op op = Op::Observe;
    int a = 0, b = 0, c = 0, d = 0, ms = 0;
    std::string text;
};
struct Parsed {
    bool valid = false, addressed = false;
    Command command;
    std::string error = "malformed_request";
};
inline bool Integer(const std::string &s, int minimum, int maximum, int &result) {
    if (s.empty() || s.size() > 11) return false;
    size_t i = 0;
    const bool negative = s[0] == '-';
    if (negative && (++i == s.size())) return false;
    int64_t value = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        value = value * 10 + s[i] - '0';
        if (value > 2147483648LL) return false;
    }
    if (negative) value = -value;
    if (value < minimum || value > maximum) return false;
    result = static_cast<int>(value);
    return true;
}
inline Parsed Parse(const std::string &line, const std::string &session) {
    Parsed p;
    if (line.empty() || line.size() > 4096 || line.back() != '\n') return p;
    for (size_t i = 0; i + 1 < line.size(); ++i)
        if (line[i] < 32 || line[i] > 126) return p;
    std::istringstream input(line);
    std::vector<std::string> tokens;
    for (std::string token; input >> token;) tokens.push_back(token);
    if (tokens.size() < 5 || tokens[1] != session) return p;
    p.addressed = true;
    int id = 0;
    if (!Integer(tokens[2], 1, 2147483647, id)) return p;
    p.command.id = static_cast<uint32_t>(id);
    if (tokens[3].empty() || tokens[3].size() > 13) return p;
    for (char c : tokens[3]) {
        if (c < '0' || c > '9') return p;
        p.command.expires = p.command.expires * 10 + static_cast<unsigned>(c - '0');
    }
    if (p.command.expires > 9999999999999ULL) return p;
    tokens.erase(tokens.begin() + 3); // Remaining fields use the operation parser below.
    if (tokens[0] != "1") { p.error = "unsupported_protocol"; return p; }
    Command &c = p.command;
    const auto number = [&](size_t index, int lo, int hi, int &out) {
        return index < tokens.size() && Integer(tokens[index], lo, hi, out);
    };
    if (tokens[3] == "observe" && tokens.size() == 4) c.op = Op::Observe;
    else if (tokens[3] == "release" && tokens.size() == 4) c.op = Op::Release;
    else if (tokens[3] == "key" && tokens.size() == 6 &&
             number(4, 1, 511, c.a) && number(5, 1, 2000, c.ms)) c.op = Op::Key;
    else if (tokens[3] == "move" && tokens.size() == 8 &&
             number(4, 0, 4095, c.a) && number(5, 0, 4095, c.b) &&
             number(6, -4095, 4095, c.c) && number(7, -4095, 4095, c.d)) c.op = Op::Move;
    else if (tokens[3] == "click" && tokens.size() == 8 &&
             number(4, 1, 3, c.a) && number(5, 0, 4095, c.b) &&
             number(6, 0, 4095, c.c) && number(7, 1, 2000, c.ms)) c.op = Op::Click;
    else if (tokens[3] == "text" && tokens.size() == 5 &&
             !tokens[4].empty() && tokens[4].size() <= 256 && tokens[4].size() % 2 == 0) {
        const auto hex = [](char v) {
            if (v >= '0' && v <= '9') return v - '0';
            if (v >= 'a' && v <= 'f') return v - 'a' + 10;
            if (v >= 'A' && v <= 'F') return v - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < tokens[4].size(); i += 2) {
            const int high = hex(tokens[4][i]), low = hex(tokens[4][i + 1]);
            if (high < 0 || low < 0 || high * 16 + low < 32 || high * 16 + low > 126) return p;
            c.text.push_back(static_cast<char>(high * 16 + low));
        }
        c.op = Op::Text;
    } else return p;
    p.valid = true;
    p.error.clear();
    return p;
}

enum class Decision { Start, Ignore, Repeat, Defer };
class Sequence {
    uint32_t last = 0;
    bool pending = false;
public:
    Decision Begin(uint32_t id, bool release) {
        if (!id || id < last) return Decision::Ignore;
        if (id == last) return pending ? Decision::Ignore : Decision::Repeat;
        if (pending && !release) return Decision::Defer;
        last = id;
        pending = true;
        return Decision::Start;
    }
    void Complete() { pending = false; }
};
class Hold {
    bool active = false;
    uint64_t deadline = 0, frame = 0;
public:
    void Start(uint64_t now, uint64_t startFrame, unsigned milliseconds) {
        active = true; deadline = now + milliseconds; frame = startFrame;
    }
    bool Due(uint64_t now, uint64_t currentFrame) const {
        return active && now >= deadline && currentFrame > frame;
    }
    void Stop() { active = false; }
};
class TextPacer {
    std::string text;
    size_t offset = 0;
    uint64_t lastFrame = 0;
    bool started = false;
public:
    void Start(const std::string &value) { text = value; offset = 0; started = false; }
    bool Next(uint64_t frame, char &value) {
        if (Done() || (started && frame <= lastFrame)) return false;
        value = text[offset++]; lastFrame = frame; started = true; return true;
    }
    bool Done() const { return offset == text.size(); }
};
} // namespace Agent
#endif
