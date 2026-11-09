// windp::Connect failure reasons (src/netlib/connfail.h). Explicit checks,
// so the test also fails in NDEBUG builds.
#include "../src/netlib/connfail.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

static void Require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main() {
    using namespace ZNDNet;
    Require(ConnectFailureFromRefusal(ERR_CONN_FULL) == CONNFAIL_FULL, "FULL refusal mapped wrongly");
    Require(ConnectFailureFromRefusal(ERR_CONN_NAME) == CONNFAIL_NAME, "NAME refusal mapped wrongly");
    Require(ConnectFailureFromRefusal(ERR_CONN_VERSION) == CONNFAIL_VERSION, "VERSION refusal mapped wrongly");
    Require(std::string(ConnectFailureName(ConnectFailureFromRefusal(ERR_CONN_RANDOM))) == "random", "random failure mislabeled");
    Require(ConnectFailureIsFinal(ConnectFailureFromRefusal(ERR_CONN_RANDOM)), "entropy failure retried");
    Require(ConnectFailureFromRefusal(ERR_CONN_TIMEOUT) == CONNFAIL_NO_ANSWER, "transport timeout is not no-answer");
    Require(ConnectFailureFromRefusal(0xff) == CONNFAIL_NO_ANSWER, "unknown code is not treated as no answer");

    std::set<std::string> names;
    for (int f = CONNFAIL_NONE; f <= CONNFAIL_DROPPED; ++f) {
        const char *name = ConnectFailureName(f);
        Require(name && *name && std::strcmp(name, "unknown") != 0, "reason without a name");
        Require(std::strchr(name, ' ') == nullptr, "reason name is not a single token");
        names.insert(name);
    }
    Require(names.size() == static_cast<size_t>(CONNFAIL_DROPPED) + 1, "two reasons share a name");
    Require(std::string(ConnectFailureName(CONNFAIL_VERSION)) == "version", "version reason renamed");
    Require(std::string(ConnectFailureName(CONNFAIL_NAME)) == "name", "name reason renamed");
    Require(std::string(ConnectFailureName(99)) == "unknown", "out-of-range reason has a real name");

    // Only refusals that a retry cannot overcome are final. A taken name may
    // be an orphan reservation that expires; a full session may free a slot.
    Require(ConnectFailureIsFinal(CONNFAIL_ADDRESS), "unresolvable address retried");
    Require(ConnectFailureIsFinal(CONNFAIL_VERSION), "version mismatch retried");
    Require(!ConnectFailureIsFinal(CONNFAIL_NAME), "taken name never retried");
    Require(!ConnectFailureIsFinal(CONNFAIL_FULL), "full session never retried");
    Require(!ConnectFailureIsFinal(CONNFAIL_NO_ANSWER), "silent host never retried");
    Require(!ConnectFailureIsFinal(CONNFAIL_DROPPED), "dropped handshake never retried");
    Require(!ConnectFailureIsFinal(CONNFAIL_NONE), "success treated as a final failure");
    std::cout << "PASS: connect refusal reasons, names and retry policy\n";
    return 0;
}
