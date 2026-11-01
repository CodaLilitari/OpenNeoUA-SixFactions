#include "../src/agent/protocol.h"
#include <cstdlib>
#include <iostream>
#include <string>

static void check(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    const std::string session = "0123456789abcdef0123456789abcdef";
    const auto parse = [&](const std::string &suffix) {
        const auto space = suffix.find(' ');
        return Agent::Parse("1 " + session + " " + suffix.substr(0, space) + " 9999999999999" + suffix.substr(space) + "\n", session);
    };
    auto key = parse("1 key 4 2000");
    check(key.valid && key.command.id == 1 && key.command.a == 4 && key.command.ms == 2000, "valid bounded key rejected");
    for (const auto &bad : {"0 observe", "2147483648 observe", "2 key 0 10", "2 key 512 10", "2 key 4 0",
        "2 key 4 2001", "2 key -1 5", "2 click 4 0 0 20", "2 click 1 4096 0 20", "2 move 0 0 -4096 0",
        "2 observe extra", "2 exec calc", "2 text 00", "2 text 7f", "2 text 2", "2 text gg", "2 release extra"})
        check(!parse(bad).valid, bad);
    auto text = parse("3 text 41207e");
    check(text.valid && text.command.text == "A ~", "hex text decoding failed");
    check(!parse("3 text " + std::string(258, '4')).valid, "oversized text accepted");
    check(!parse("3 observe\n4 observe").valid, "multiple lines accepted");
    check(!Agent::Parse(std::string(4097, 'x'), session).valid, "oversized request accepted");
    check(!Agent::Parse("1 wrong 5 9999999999999 observe\n", session).addressed, "wrong session addressed");
    check(!Agent::Parse("2 " + session + " 5 9999999999999 observe\n", session).valid, "wrong version accepted");
    check(!Agent::Parse("1 " + session + " 5 -1 observe\n", session).valid, "negative expiry accepted");
    check(!Agent::Parse("1 " + session + " 5 10000000000000 observe\n", session).valid, "oversized expiry accepted");
    check(parse("4 move 4095 0 -4095 4095").valid, "valid motion bounds rejected");
    check(parse("5 click 3 0 4095 1").valid, "valid click bounds rejected");
    Agent::Sequence sequence;
    check(sequence.Begin(1, false) == Agent::Decision::Start, "first request not started");
    check(sequence.Begin(1, false) == Agent::Decision::Ignore, "pending duplicate executed");
    check(sequence.Begin(2, false) == Agent::Decision::Defer, "overlapping action executed");
    check(sequence.Begin(2, true) == Agent::Decision::Start, "release cannot preempt");
    sequence.Complete();
    check(sequence.Begin(2, false) == Agent::Decision::Repeat, "completed duplicate not acknowledged");
    check(sequence.Begin(1, false) == Agent::Decision::Ignore, "stale request executed");
    check(sequence.Begin(3, false) == Agent::Decision::Start, "next request not accepted");
    Agent::Hold hold;
    hold.Start(100, 20, 40);
    check(!hold.Due(140, 20), "hold released before a processed frame");
    check(!hold.Due(139, 21), "hold released early");
    check(hold.Due(140, 21), "hold did not release at deadline");
    hold.Stop();
    check(!hold.Due(1000, 40), "stopped hold remained active");
    Agent::TextPacer pacer;
    pacer.Start("ABC");
    char character = 0;
    check(pacer.Next(10, character) && character == 'A', "first text character missing");
    check(!pacer.Next(10, character), "text can overflow the normal input queue in one frame");
    check(pacer.Next(11, character) && character == 'B' && !pacer.Done(), "second text character missing");
    check(pacer.Next(12, character) && character == 'C' && pacer.Done(), "final text character missing");
    std::cout << "PASS: request validation, bounds, printable text, session isolation, replay, preemption and hold scheduling\n";
}
