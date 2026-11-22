#pragma once

// How long the game's loading takes, phase by phase: each mark() is the time since the one before
// (and since the first). Printed, and kept in loadtimes.txt in the settings folder (on the Xbox,
// T:\loadtimes.txt), the last run's, to find what makes the loading slow on a slow machine.
namespace loadlog {

// The phase that just ended
void mark(const char* what);

// A phase timed from here to the end of the scope: "what  12 ms" (nullptr: not timed)
class Scope {
public:
    explicit Scope(const char* what);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    const char* m_what;
    unsigned long long m_start;
};

} // namespace loadlog
