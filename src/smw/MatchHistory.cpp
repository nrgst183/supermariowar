#include "MatchHistory.h"

#include "FileList.h"
#include "Game.h"
#include "map.h"
#include "GameMode.h"
#include "GameValues.h"
#include "MatchTypes.h"
#include "path.h"
#include "SimulationState.h"
#include "rollback/NetGame.h"
#include "rollback/Replay.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

extern CGameValues game_values;
extern SkinList* skinlist;

namespace history {
namespace {

const char* matchTypeName(MatchType type)
{
    switch (type) {
        case MatchType::SingleGame: return "Single Game";
        case MatchType::Tournament: return "Tournament";
        case MatchType::Tour: return "Tour";
        case MatchType::MiniGame: return "Minigame";
        case MatchType::World: return "World";
        case MatchType::QuickGame: return "Quick Game";
    }
    return "Match";
}

std::tm localTime(int64_t time)
{
    const std::time_t t = static_cast<std::time_t>(time);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return local;
}

// ---- JSON (just what the entries use)

std::string quote(const std::string& s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out + "\"";
}

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    const Json& operator[](const std::string& key) const
    {
        static const Json none;
        const auto it = o.find(key);
        return it == o.end() ? none : it->second;
    }
    int i() const { return static_cast<int>(n); }
};

struct JsonParser {
    const std::string& t;
    size_t p = 0;
    bool ok = true;

    void ws() { while (p < t.size() && std::isspace(static_cast<unsigned char>(t[p]))) p++; }
    bool eat(char c)
    {
        ws();
        if (p < t.size() && t[p] == c) {
            p++;
            return true;
        }
        return false;
    }
    std::string str()
    {
        std::string out;
        if (!eat('"'))
            return ok = false, out;
        while (p < t.size() && t[p] != '"') {
            char c = t[p++];
            if (c == '\\' && p < t.size()) {
                c = t[p++];
                switch (c) {
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'u':
                        if (p + 4 <= t.size()) {
                            c = static_cast<char>(std::stoi(t.substr(p, 4), nullptr, 16) & 0xff);
                            p += 4;
                        }
                        break;
                    default: break;
                }
            }
            out += c;
        }
        if (p >= t.size())
            ok = false;
        p++;
        return out;
    }
    Json value(int depth = 0)
    {
        Json v;
        ws();
        if (!ok || p >= t.size() || depth > 16)
            return ok = false, v;
        const char c = t[p];
        if (c == '{') {
            p++;
            v.type = Json::Type::Object;
            if (eat('}'))
                return v;
            do {
                std::string key = str();
                if (!eat(':'))
                    return ok = false, v;
                v.o[key] = value(depth + 1);
            } while (ok && eat(','));
            if (!eat('}'))
                ok = false;
        } else if (c == '[') {
            p++;
            v.type = Json::Type::Array;
            if (eat(']'))
                return v;
            do {
                v.a.push_back(value(depth + 1));
            } while (ok && eat(','));
            if (!eat(']'))
                ok = false;
        } else if (c == '"') {
            v.type = Json::Type::String;
            v.s = str();
        } else if (t.compare(p, 4, "true") == 0) {
            v.type = Json::Type::Bool;
            v.b = true;
            p += 4;
        } else if (t.compare(p, 5, "false") == 0) {
            v.type = Json::Type::Bool;
            p += 5;
        } else if (t.compare(p, 4, "null") == 0) {
            p += 4;
        } else {
            size_t end = p;
            while (end < t.size() && (std::isdigit(static_cast<unsigned char>(t[end])) || t[end] == '-' || t[end] == '.' || t[end] == 'e' || t[end] == 'E' || t[end] == '+'))
                end++;
            if (end == p)
                return ok = false, v;
            v.type = Json::Type::Number;
            v.n = std::strtod(t.substr(p, end - p).c_str(), nullptr);
            p = end;
        }
        return v;
    }
};

std::string skinName(int slot)
{
    const short id = game_values.skinids[slot];
    if (skinlist && id >= 0 && static_cast<size_t>(id) < skinlist->count())
        return skinlist->at(static_cast<size_t>(id)).name;
    return {};
}

std::string playerName(int slot, bool online)
{
    if (game_values.playercontrol[slot] != 1)
        return "CPU";
    if (online) {
        const std::string name = rollback::NetGame::instance().slotPlayerName(slot);
        if (!name.empty())
            return name;
    }
    return "Player " + std::to_string(slot + 1);
}

Entry captureEntry(const std::string& replayFile, bool online)
{
    Entry e;
    e.time = static_cast<int64_t>(std::time(nullptr));
    e.matchType = matchTypeName(game_values.matchtype);
    e.mode = game_values.gamemode ? game_values.gamemode->GetModeName() : std::string();
    e.modeId = game_values.gamemode ? game_values.gamemode->gamemode : -1;
    e.goal = game_values.gamemode ? game_values.gamemode->goal : 0;
    e.map = g_map ? stripPathAndExtension(g_map->loadedFile()) : std::string();
    e.ticks = g_sim.tick;
    e.online = online;
    e.replay = replayFile;

    const bool over = game_values.gamemode && game_values.gamemode->gameover;
    const short winner = game_values.gamemode ? game_values.gamemode->winningteam : -1;
    const int teams = std::clamp<int>(g_sim.score_cnt, 0, 4);
    if (!over)
        e.result = "left";
    else if (winner >= 0 && winner < teams)
        e.result = "won", e.winner = winner;
    else
        e.result = "draw";

    for (int t = 0; t < teams; t++) {
        Team team;
        team.score = g_sim.score[static_cast<size_t>(t)].score;
        for (int m = 0; m < game_values.teamcounts[t] && m < 3; m++) {
            const int slot = game_values.teamids[t][m];
            if (slot < 0 || slot >= 4)
                continue;
            Player p;
            p.slot = slot;
            p.human = game_values.playercontrol[slot] == 1;
            p.name = playerName(slot, online);
            p.skin = skinName(slot);
            team.players.push_back(p);
        }
        e.teams.push_back(team);
    }
    //Places: by score, the winner first
    for (int t = 0; t < teams; t++) {
        int place = 1;
        for (int u = 0; u < teams; u++) {
            if (u == t)
                continue;
            const bool better = e.winner >= 0 ? (u == e.winner || (t != e.winner && e.teams[u].score > e.teams[t].score)) : e.teams[u].score > e.teams[t].score;
            place += better;
        }
        e.teams[t].place = place;
    }
    return e;
}

std::string ordinal(int n)
{
    switch (n) {
        case 1: return "1st";
        case 2: return "2nd";
        case 3: return "3rd";
        default: return std::to_string(n) + "th";
    }
}

std::string toText(const Entry& e)
{
    std::ostringstream out;
    out << formatTime(e.time) << "  " << e.matchType << (e.online ? " (online)" : "") << ": " << e.mode;
    if (e.goal > 0)
        out << ", goal " << e.goal;
    out << ", map " << (e.map.empty() ? "?" : e.map) << ", " << formatDuration(e.ticks);
    if (e.result == "left")
        out << ", left before the end";
    else if (e.result == "draw")
        out << ", draw";
    out << "\n";

    std::vector<size_t> order(e.teams.size());
    for (size_t k = 0; k < order.size(); k++)
        order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return e.teams[a].place < e.teams[b].place; });
    for (size_t t : order) {
        const Team& team = e.teams[t];
        out << "  " << ordinal(team.place) << "  " << team.score << "  ";
        for (size_t m = 0; m < team.players.size(); m++) {
            const Player& p = team.players[m];
            out << (m ? ", " : "") << p.name;
            if (!p.skin.empty())
                out << " (" << p.skin << ")";
        }
        if (static_cast<int>(t) == e.winner)
            out << "  - winner";
        out << "\n";
    }
    if (!e.replay.empty())
        out << "  replay: replays/" << e.replay << "\n";
    out << "\n";
    return out.str();
}

void append(const std::string& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size())))
        printf("history: can't write %s\n", path.c_str());
}

} // namespace

std::string textFile()
{
    return GetHomeDirectory() + "match_history.txt";
}

std::string jsonFile()
{
    return GetHomeDirectory() + "match_history.jsonl";
}

std::string formatDuration(int ticks)
{
    //62.5 ticks per second (16 ms)
    const int seconds = std::max(0, ticks) * 16 / 1000;
    char buf[32];
    if (seconds >= 3600)
        snprintf(buf, sizeof(buf), "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else
        snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
    return buf;
}

std::string formatTime(int64_t time)
{
    const std::tm local = localTime(time);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M", &local);
    return buf;
}

std::string formatShortTime(int64_t time)
{
    const std::tm local = localTime(time);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%m-%d %H:%M", &local);
    return buf;
}

std::string toJson(const Entry& e)
{
    std::ostringstream out;
    out << "{\"time\":" << e.time << ",\"date\":" << quote(formatTime(e.time)) << ",\"type\":" << quote(e.matchType) << ",\"mode\":" << quote(e.mode) << ",\"modeId\":" << e.modeId
        << ",\"goal\":" << e.goal << ",\"map\":" << quote(e.map) << ",\"ticks\":" << e.ticks << ",\"duration\":" << quote(formatDuration(e.ticks))
        << ",\"online\":" << (e.online ? "true" : "false") << ",\"result\":" << quote(e.result) << ",\"winner\":" << e.winner << ",\"teams\":[";
    for (size_t t = 0; t < e.teams.size(); t++) {
        const Team& team = e.teams[t];
        out << (t ? "," : "") << "{\"score\":" << team.score << ",\"place\":" << team.place << ",\"players\":[";
        for (size_t m = 0; m < team.players.size(); m++) {
            const Player& p = team.players[m];
            out << (m ? "," : "") << "{\"slot\":" << p.slot << ",\"name\":" << quote(p.name) << ",\"skin\":" << quote(p.skin)
                << ",\"human\":" << (p.human ? "true" : "false") << "}";
        }
        out << "]}";
    }
    out << "],\"replay\":" << quote(e.replay) << "}";
    return out.str();
}

bool fromJson(const std::string& line, Entry& e)
{
    JsonParser parser {line};
    const Json v = parser.value();
    if (!parser.ok || v.type != Json::Type::Object)
        return false;
    e = Entry();
    e.time = static_cast<int64_t>(v["time"].n);
    e.matchType = v["type"].s;
    e.mode = v["mode"].s;
    e.modeId = v["modeId"].type == Json::Type::Number ? v["modeId"].i() : -1;
    e.goal = v["goal"].i();
    e.map = v["map"].s;
    e.ticks = v["ticks"].i();
    e.online = v["online"].b;
    e.result = v["result"].s;
    e.winner = v["winner"].type == Json::Type::Number ? v["winner"].i() : -1;
    e.replay = v["replay"].s;
    for (const Json& jt : v["teams"].a) {
        Team team;
        team.score = jt["score"].i();
        team.place = std::max(1, jt["place"].i());
        for (const Json& jp : jt["players"].a) {
            Player p;
            p.slot = jp["slot"].i();
            p.name = jp["name"].s;
            p.skin = jp["skin"].s;
            p.human = jp["human"].b;
            team.players.push_back(p);
        }
        e.teams.push_back(team);
    }
    return true;
}

void recordMatchEnd(const std::string& replayFile, bool online)
{
    if (replay::playing())
        return;
    if (!game_values.gamemode || g_sim.tick <= 0)
        return;
    const Entry e = captureEntry(replayFile, online);
    ensureSettingsDir();
    append(textFile(), toText(e));
    append(jsonFile(), toJson(e) + "\n");
}

std::vector<Entry> load(size_t max)
{
    std::vector<Entry> entries;
    std::ifstream file(jsonFile(), std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        Entry e;
        if (!line.empty() && fromJson(line, e))
            entries.push_back(std::move(e));
    }
    std::reverse(entries.begin(), entries.end());
    if (entries.size() > max)
        entries.resize(max);
    return entries;
}

} // namespace history
