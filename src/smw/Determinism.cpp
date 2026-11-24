#include "Determinism.h"

#include "CmdArgs.h"
#include "path.h"
#include "RandomNumberGenerator.h"
#include "rollback/StateChecksum.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace determinism {
namespace {

struct Case {
    const char* map;    // the map list's short name
    int mode;           // GameModeType
    const char* name;   // for the report
};

// Maps with moving platforms and circling hazards; the modes whose objects move by sin, cos and
// atan2 (flags, race goals, scattered coins and cards, stars), and the plain ones
const Case SUITE[] = {
    {"0smw", 0, "Classic"},
    {"Airshipz", 14, "Owned"},
    {"Elevating Explosives", 7, "Capture the Flag"},
    {"Crevice Canyon", 13, "Race"},
    {"Fire Fortress", 17, "Greed"},
    {"Clocks on Fire", 19, "Card Collection"},
    {"cliff", 1, "Frag"},
    {"Airshipz", 10, "Star"},
    {"Elevating Explosives", 16, "Survival"},
    {"0smw", 8, "Chicken"},
};
constexpr int SUITE_SIZE = static_cast<int>(sizeof(SUITE) / sizeof(SUITE[0]));
constexpr int FRAMES = 1200;
constexpr int EVERY = 60;           // the state checksum
constexpr int FIELDS_EVERY = 10;    // the state's values one by one: what differs first, and where
constexpr int EVERY_TICK_UNTIL = 400;    //(the first match's first ticks every tick: its first difference to the tick)

// (suite match, frame) -> checksum
using Sums = std::map<std::pair<int, int>, uint64_t>;
// The state's values (rollback::stateFields) of a sample, small: the names once for all (they're the
// same every tick) and a value's lower 32 bits (all of a float; enough of a hash). The Xbox has 64 MB:
// two runs' samples and the reference's, kept as whole names, took more than it could spare.
struct Names {
    std::vector<std::string> names;
    std::vector<bool> isFloat;
    std::map<std::string, uint16_t> index;

    uint16_t id(const std::string& name, bool isFloatValue)
    {
        auto it = index.find(name);
        if (it != index.end())
            return it->second;
        const uint16_t id = static_cast<uint16_t>(names.size());
        names.push_back(name);
        isFloat.push_back(isFloatValue);
        index.emplace(name, id);
        return id;
    }
};
Names g_names;

struct Sample {
    std::vector<uint16_t> ids;
    std::vector<uint32_t> values;
};
// (suite match, frame) -> the state's values
using Fields = std::map<std::pair<int, int>, Sample>;

struct State {
    bool active = false;
    bool finished = false;
    bool passed = false;
    int passes = 1;
    std::vector<Sums> runs;      // one per pass
    Fields fields;               // the first pass's
    std::vector<std::string> laterDifference;    // a later pass's first values different from the first's
    std::vector<std::string> report;
    std::map<std::pair<unsigned, unsigned>, long> fpuSeen;    // (x87 control word, MXCSR) -> ticks
};
State g;

const Case& caseOf(int index)
{
    return SUITE[((index % SUITE_SIZE) + SUITE_SIZE) % SUITE_SIZE];
}

std::string describe(int suiteIndex, int frame)
{
    const Case& c = caseOf(suiteIndex);
    char line[160];
    snprintf(line, sizeof(line), "match %d (%s, %s) at tick %d", suiteIndex + 1, c.map, c.name, frame);
    return line;
}

// The first checksum of `run` that `expected` has otherwise (in match and tick order); "" if none.
// `compared`: how many were compared.
std::string firstDifference(const Sums& expected, const Sums& run, int& compared)
{
    compared = 0;
    for (const auto& [key, sum] : run) {
        auto it = expected.find(key);
        if (it == expected.end())
            continue;
        compared++;
        if (it->second != sum)
            return describe(key.first, key.second);
    }
    return {};
}

std::string valueText(uint16_t id, uint32_t value)
{
    char text[40];
    if (g_names.isFloat[id]) {
        float v = 0;
        std::memcpy(&v, &value, sizeof(v));
        snprintf(text, sizeof(text), "%.9g", static_cast<double>(v));
    } else if (value > 0xffff) {
        snprintf(text, sizeof(text), "#%04x", static_cast<unsigned>(value & 0xffff));    //(a hash: its end)
    } else {
        snprintf(text, sizeof(text), "%u", static_cast<unsigned>(value));
    }
    return text;
}

// The first sample of `run` whose values aren't `expected`'s (in match and tick order): where, then up
// to 3 of the values that differ, a line each; none if every sample is the same
std::vector<std::string> firstFieldDifference(const Fields& expected, const Fields& run, const char* other)
{
    for (const auto& [key, sample] : run) {
        auto it = expected.find(key);
        if (it == expected.end())
            continue;
        std::map<uint16_t, uint32_t> theirs;
        for (size_t k = 0; k < it->second.ids.size(); k++)
            theirs[it->second.ids[k]] = it->second.values[k];
        std::vector<std::string> lines;
        std::string others;
        int more = 0;
        for (size_t k = 0; k < sample.ids.size(); k++) {
            const uint16_t id = sample.ids[k];
            const uint32_t value = sample.values[k];
            auto t = theirs.find(id);
            if (t != theirs.end() && t->second == value)
                continue;
            const std::string& name = g_names.names[id];
            if (lines.size() == 3) {
                //(the rest by name: which parts of the state differ already)
                if (others.size() < 60)
                    others += (others.empty() ? "" : ", ") + name;
                more++;
                continue;
            }
            lines.push_back(name + ": " + valueText(id, value) + ", " + other + " " +
                            (t == theirs.end() ? std::string("(none)") : valueText(id, t->second)));
        }
        if (more > 0)
            lines.push_back("and " + std::to_string(more) + " more: " + others);
        //(a value only the other has: a different list, e.g. a player fewer)
        if (lines.empty() && theirs.size() != sample.ids.size())
            lines.push_back("a different set of values (" + std::to_string(sample.ids.size()) + ", " + other + " " +
                            std::to_string(theirs.size()) + ")");
        if (!lines.empty()) {
            lines.insert(lines.begin(), "values first different at tick " + std::to_string(key.second) + " of match " + std::to_string(key.first + 1) + ":");
            return lines;
        }
    }
    return {};
}

bool readReference(const std::string& path, Sums& sums, Fields& fields)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        //(a value: F, match, tick, float (0/1), value (hex), name; tab separated)
        if (line[0] == 'F') {
            std::vector<std::string> parts;
            std::stringstream split(line);
            std::string part;
            while (std::getline(split, part, '\t'))
                parts.push_back(part);
            if (parts.size() == 6) {
                Sample& sample = fields[{std::stoi(parts[1]), std::stoi(parts[2])}];
                sample.ids.push_back(g_names.id(parts[5], parts[3] == "1"));
                sample.values.push_back(static_cast<uint32_t>(std::stoull(parts[4], nullptr, 16)));
            }
            continue;
        }
        std::istringstream words(line);
        int match = 0, frame = 0;
        std::string hex;
        if (words >> match >> frame >> hex)
            sums[{match, frame}] = std::stoull(hex, nullptr, 16);
    }
    return true;
}

void writeSums(const std::string& path, const Sums& sums, const Fields& fields)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        printf("determinism: can't write %s\n", path.c_str());
        return;
    }
    out << "# The determinism test's checksums (--at-determinism): match, tick, state checksum, map, mode;\n"
           "# then the state's values (F, match, tick, float, value, name)\n";
    for (const auto& [key, sum] : sums) {
        char hex[20];
        snprintf(hex, sizeof(hex), "%016" PRIx64, sum);
        out << key.first << ' ' << key.second << ' ' << hex << ' ' << caseOf(key.first).map << " / " << caseOf(key.first).name << '\n';
    }
    for (const auto& [key, sample] : fields) {
        for (size_t k = 0; k < sample.ids.size(); k++) {
            const uint16_t id = sample.ids[k];
            char hex[20];
            snprintf(hex, sizeof(hex), "%x", static_cast<unsigned>(sample.values[k]));
            out << "F\t" << key.first << '\t' << key.second << '\t' << (g_names.isFloat[id] ? 1 : 0) << '\t' << hex << '\t'
                << g_names.names[id] << '\n';
        }
    }
    printf("determinism: wrote %s\n", path.c_str());
}

void say(const std::string& line)
{
    printf("determinism: %s\n", line.c_str());
    g.report.push_back(line);
}

} // namespace

bool configure()
{
    cmd::AutoTestArgs& at = cmd::g_autotest;
    if (!at.enabled || at.determinism <= 0)
        return false;
    g.active = true;
    g.passes = at.determinism;
    g.runs.assign(static_cast<size_t>(g.passes), {});
    g.fields.clear();
    at.kind = cmd::AutoTestArgs::Kind::Single;
    at.players = 4;
    at.humans = 0;
    at.frames = FRAMES;
    at.matches = SUITE_SIZE * g.passes;
    at.start = 0;
    if (at.seed == 0)
        at.seed = 1;
    printf("determinism: %d matches, %d pass(es), checksums every %d ticks\n", SUITE_SIZE, g.passes, EVERY);
    return true;
}

bool active()
{
    return g.active;
}

std::string mapOf(int index)
{
    return caseOf(index).map;
}

int modeOf(int index)
{
    return caseOf(index).mode;
}

void matchStarting(int index)
{
    //(each match from a seed of its own: what the menus took from the random numbers before it
    //doesn't matter, and every pass plays the same matches)
    const uint32_t seed = static_cast<uint32_t>(cmd::g_autotest.seed) * 2654435761u + static_cast<uint32_t>(index % SUITE_SIZE) + 1u;
    RandomNumberGenerator::gameplay().seed(seed);
}

void noteFpu(unsigned short x87, unsigned int mxcsr)
{
    if (g.active)
        g.fpuSeen[{x87, mxcsr & ~0x3Fu}]++;    //(without the sticky exception flags)
}

void sample(int index, int frame, uint64_t sum)
{
    if (!g.active || (frame % FIELDS_EVERY != 0 && !(index % SUITE_SIZE == 0 && frame <= EVERY_TICK_UNTIL)))
        return;
    const int pass = index / SUITE_SIZE;
    if (pass < 0 || pass >= g.passes)
        return;
    const std::pair<int, int> key {index % SUITE_SIZE, frame};
    if (frame % EVERY == 0)
        g.runs[static_cast<size_t>(pass)][key] = sum;
    Sample sample;
    for (const rollback::StateField& f : rollback::stateFields()) {
        sample.ids.push_back(g_names.id(f.name, f.isFloat));
        sample.values.push_back(static_cast<uint32_t>(f.value));
    }
    if (pass == 0) {
        g.fields[key] = std::move(sample);
    } else if (g.laterDifference.empty()) {
        //(a later pass: compared now, only its first difference kept; the Xbox has no memory to spare)
        Fields one;
        one[key] = std::move(sample);
        g.laterDifference = firstFieldDifference(g.fields, one, "run 1");
    }
}

void finish()
{
    if (!g.active || g.finished)
        return;
    g.finished = true;
    //(the matches may have run without drawing, fast: the result is drawn)
    cmd::g_autotest.norender = false;
    const cmd::AutoTestArgs& at = cmd::g_autotest;
    const Sums& first = g.runs.front();
    bool ok = !first.empty();

#ifdef SMW_XBOX
    say("Determinism test (Xbox)");
#else
    say("Determinism test (PC)");
#endif
    say(std::to_string(SUITE_SIZE) + " matches, " + std::to_string(first.size()) + " checksums a pass");

    //The same machine: every pass as the first
    for (int p = 1; p < g.passes; p++) {
        int compared = 0;
        const std::string diff = firstDifference(first, g.runs[static_cast<size_t>(p)], compared);
        if (!diff.empty()) {
            ok = false;
            say("Run " + std::to_string(p + 1) + " vs run 1: DIFFERENT");
            say("from " + diff);
            for (const std::string& line : g.laterDifference)
                say(line);
        } else {
            say("Run " + std::to_string(p + 1) + " vs run 1: the same (" + std::to_string(compared) + " checksums)");
        }
    }

    //Another build's (the PC's, on the Xbox)
    if (!at.determinismRef.empty()) {
        Sums reference;
        Fields referenceFields;
        if (!readReference(at.determinismRef, reference, referenceFields)) {
            ok = false;
            say("No reference at " + at.determinismRef);
        } else {
            int compared = 0;
            const std::string diff = firstDifference(reference, first, compared);
            if (compared == 0) {
                ok = false;
                say("The reference has none of these checksums");
            } else if (!diff.empty()) {
                ok = false;
                say("Vs the reference: DIFFERENT");
                say("from " + diff);
                for (const std::string& line : firstFieldDifference(referenceFields, g.fields, "reference"))
                    say(line);
            } else {
                say("Vs the reference: the same (" + std::to_string(compared) + " checksums)");
            }
        }
    }

    //(the Xbox: the FPU modes the ticks started with, before being set; 027f/1f80 is as they're set)
    if (!g.fpuSeen.empty()) {
        std::string line = "FPU at tick start:";
        for (const auto& [modes, ticks] : g.fpuSeen) {
            char part[48];
            snprintf(part, sizeof(part), " %04x/%04x x%ld", modes.first, modes.second, ticks);
            line += part;
        }
        say(line);
    }

    g.passed = ok;
    say(ok ? "PASSED" : "FAILED");

    writeSums(at.determinismOut.empty() ? writablePath("determinism_checksums.txt") : at.determinismOut, first, g.fields);
    {
        const std::string path = writablePath("determinism_result.txt");
        std::ofstream out(path);
        for (const std::string& line : g.report)
            out << line << '\n';
    }
}

bool finished()
{
    return g.finished;
}

bool passed()
{
    return g.passed;
}

const std::vector<std::string>& report()
{
    return g.report;
}

} // namespace determinism
