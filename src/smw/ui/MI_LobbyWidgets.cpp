#include "MI_LobbyWidgets.h"

#include "FileList.h"
#include "GameValues.h"
#include "gfx.h"
#include "ResourceManager.h"
#include "RandomNumberGenerator.h"
#include "rollback/NetGame.h"
#include "ui/MI_Image.h"
#include "ui/MI_MenuPanel.h"
#include "ui/SmwTheme.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <vector>


namespace {

std::string ellipsize(const gfxFont& font, std::string text, int maxWidth)
{
    if (font.getWidth(text) <= maxWidth)
        return text;
    while (!text.empty() && font.getWidth(text + "..") > maxWidth)
        text.pop_back();
    return text + "..";
}

std::string defaultSkinPath()
{
    return skinlist && skinlist->count() > 0 ? skinlist->at(0).path.string() : std::string();
}

// The blue bar of the original headers (the sprite is 512 wide: two halves, like MI_Button)
void blueBar(int x, int y, int width)
{
    const int half = width >> 1;
    rm->menu_plain_field.draw(x, y, {0, 0, half, 32});
    rm->menu_plain_field.draw(x + half, y, {512 - width + half, 0, width - half, 32});
}

// The cream frame of the map previews (336 x 254 plus the right and bottom edges)
void creamFrame(int x, int y)
{
    rm->menu_dialog.draw(x, y, {0, 0, 336, 254});
    rm->menu_dialog.draw(x + 336, y, {496, 0, 16, 254});
    rm->menu_dialog.draw(x, y + 254, {0, 464, 336, 16});
    rm->menu_dialog.draw(x + 336, y + 254, {496, 464, 16, 16});
}

constexpr SDL_Color BLACK {0, 0, 0, 255};

} // namespace


// ---- MI_OnlineTeamSelect

struct MI_OnlineTeamSelect::Info {
    rollback::SlotKind kind = rollback::SlotKind::Empty;
    bool human = false;
    bool cpu = false;
    bool mine = false;
    bool ready = false;
    bool downloadingSkin = false;
    int team = 0;
    int color = 0;
    int pingMs = -1;
    std::string name;
    std::string role;
};

MI_OnlineTeamSelect::MI_OnlineTeamSelect(short x, short y)
    : UI_Control(x, y)
    , m_background(std::make_unique<MI_Image>(&rm->spr_player_select_background, x, y, 0, 0, 416, 256, 1, 1, 0))
{}

void MI_OnlineTeamSelect::Reset()
{
    m_hostLocked = false;
    for (int i = 0; i < 4; i++)
        m_overrideFrames[i] = m_holdFrames[i] = 0;
}

bool MI_OnlineTeamSelect::locked() const
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby || lobby->localPeer() < 0)
        return false;
    return lobby->isHost() ? m_hostLocked : lobby->peer(lobby->localPeer()).ready;
}

void MI_OnlineTeamSelect::Lock(bool lock)
{
    rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby || lobby->localPeer() < 0 || lobby->phase() != rollback::NetLobby::Phase::Lobby)
        return;
    if (lobby->isHost())
        m_hostLocked = lock;
    else
        lobby->setReady(lock);
}

// The slot of the local player with this index (-1: none)
int MI_OnlineTeamSelect::mySlot(int localIndex) const
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby)
        return -1;
    for (int s = 0; s < 4; s++) {
        const rollback::LobbySlot& slot = lobby->slot(s);
        if (slot.kind == rollback::SlotKind::Human && slot.peer == lobby->localPeer() && slot.localIndex == localIndex)
            return s;
    }
    return -1;
}

int MI_OnlineTeamSelect::teamOf(int slot) const
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    return m_overrideFrames[slot] > 0 ? m_overrideTeam[slot] & 3 : lobby->slot(slot).team & 3;
}

std::string MI_OnlineTeamSelect::hint() const
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby || lobby->phase() != rollback::NetLobby::Phase::Lobby)
        return {};
    if (locked())
        return lobby->isHost() ? "Select Continue to choose the match (Cancel: not ready)" : "Waiting for the host (Cancel: not ready)";
    return "Up/Down: skin  Left/Right: team  Select: ready";
}

std::vector<prompts::Prompt> MI_OnlineTeamSelect::prompts() const
{
    using prompts::Pad;
    using prompts::menuKey;
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby || lobby->phase() != rollback::NetLobby::Phase::Lobby)
        return {};
    if (locked())
        return {{{Pad::Down}, {menuKey(prompts::MENU_DOWN)}, "the buttons"}, {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "not ready"}};
    std::vector<prompts::Prompt> row = {
        {{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "skin"},
        {{Pad::Left, Pad::Right}, {menuKey(prompts::MENU_LEFT), menuKey(prompts::MENU_RIGHT)}, "team"},
        {{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "ready"},
        {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "leave"},
    };
    //(the random skin: on a controller, where the row has room)
    if (prompts::lastDevice() == prompts::Device::Controller)
        row.insert(row.begin() + 2, prompts::Prompt {{Pad::X}, {}, "random"});
    return row;
}

MenuCodeEnum MI_OnlineTeamSelect::Modify(bool modify)
{
    fModifying = modify;
    return MENU_CODE_MODIFY_ACCEPTED;
}

void MI_OnlineTeamSelect::changeTeam(int slot, int direction)
{
    rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    int team = teamOf(slot);
    for (int attempt = 0; attempt < 4; attempt++) {
        team = (team + direction + 4) & 3;
        int count = 0;
        for (int s = 0; s < 4; s++)
            count += s != slot && lobby->slot(s).kind != rollback::SlotKind::Empty && teamOf(s) == team;
        if (count < 3)
            break;
    }
    if (team == teamOf(slot))
        return;
    // In the original game the color of a player is the team's
    lobby->setSlotTeam(slot, team);
    lobby->setSlotColor(slot, team);
    m_overrideTeam[slot] = team;
    m_overrideFrames[slot] = 60;
}

void MI_OnlineTeamSelect::changeSkin(int localIndex, int direction, bool random)
{
    if (!skinlist || skinlist->count() == 0 || localIndex < 0 || localIndex >= 4)
        return;
    const int count = static_cast<int>(skinlist->count());
    short& id = game_values.skinids[localIndex];
    for (int attempt = 0; attempt < count; attempt++) {
        id = static_cast<short>(random ? (id + 1 + RANDOM_INT(count - 1)) % count : (id + direction + count) % count);
        if (rm->LoadMenuSkin(localIndex, id, game_values.colorids[localIndex], false))
            break;
    }
}

MenuCodeEnum MI_OnlineTeamSelect::SendInput(CPlayerInput* playerInput)
{
    rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby || lobby->phase() != rollback::NetLobby::Phase::Lobby || lobby->localPeer() < 0) {
        for (int p = 0; p < 4; p++)
            if (playerInput->outputControls[p].menu_cancel.fPressed)
                return MENU_CODE_ONLINE_LEAVE;
        return MENU_CODE_NONE;
    }

    for (int p = 0; p < 4; p++) {
        const COutputControl& keys = playerInput->outputControls[p];
        const int slot = mySlot(p);
        const bool isLocked = locked();

        if (slot >= 0 && !isLocked) {
            if (keys.menu_left.fPressed)
                changeTeam(slot, -1);
            if (keys.menu_right.fPressed)
                changeTeam(slot, 1);

            if (keys.menu_up.fPressed) {
                changeSkin(p, -1, keys.menu_down.fDown);
                m_holdFrames[p] = 0;
            } else if (keys.menu_down.fPressed) {
                changeSkin(p, 1, keys.menu_up.fDown);
                m_holdFrames[p] = 0;
            } else if (keys.menu_up.fDown != keys.menu_down.fDown) {
                // Held: scrolls fast after a while
                if (++m_holdFrames[p] > 40 && (m_holdFrames[p] % 5) == 0)
                    changeSkin(p, keys.menu_up.fDown ? -1 : 1, false);
            } else {
                m_holdFrames[p] = 0;
            }
            if (keys.menu_random.fPressed)
                changeSkin(p, 1, true);
        }

        if (keys.menu_select.fPressed && !isLocked && (slot >= 0 || p == 0)) {
            Lock(true);
            if (m_leaveDown)
                m_leaveDown();
            return MENU_CODE_NONE;
        }

        if (isLocked && keys.menu_down.fPressed && (slot >= 0 || p == 0)) {
            if (m_leaveDown)
                m_leaveDown();
            return MENU_CODE_NONE;
        }

        if (keys.menu_cancel.fPressed && (slot >= 0 || p == 0)) {
            if (isLocked) {
                Lock(false);
                return MENU_CODE_NONE;
            }
            return MENU_CODE_ONLINE_LEAVE;
        }
    }
    return MENU_CODE_NONE;
}

void MI_OnlineTeamSelect::Update()
{
    ++m_tick;
    for (int i = 0; i < 4; i++)
        if (m_overrideFrames[i] > 0)
            --m_overrideFrames[i];
}

bool MI_OnlineTeamSelect::gather(int s, Info& info) const
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby)
        return false;

    const rollback::LobbySlot& slot = lobby->slot(s);
    info.kind = slot.kind;
    info.human = slot.kind == rollback::SlotKind::Human;
    info.cpu = slot.kind == rollback::SlotKind::Cpu;
    info.mine = info.human && slot.peer == lobby->localPeer();
    info.team = teamOf(s);
    info.color = m_overrideFrames[s] > 0 ? info.team : slot.color & 3;
    const rollback::LobbyPeer* peer = info.human ? &lobby->peer(slot.peer) : nullptr;
    if (info.human) {
        // The host's ready flag is always set: its own lock is only shown on its own screen
        info.ready = slot.peer == 0 ? (!lobby->isHost() || m_hostLocked) : peer->ready;
    } else {
        info.ready = info.cpu;
    }
    info.downloadingSkin = info.human && lobby->slotSkinDownloading(s);

    switch (slot.kind) {
        case rollback::SlotKind::Empty:
            break;
        case rollback::SlotKind::Cpu:
            info.name = "CPU";
            info.role = "Computer";
            break;
        case rollback::SlotKind::Human:
            info.name = peer->name;
            //(no nickname: "Player" and the slot's number; else the nickname, and which of its players)
            if (info.name.empty() || info.name.rfind("Player ", 0) == 0)
                info.name = std::format("Player {}", s + 1);
            else if (slot.localIndex > 0)
                info.name += std::format(" ({})", slot.localIndex + 1);
            //(the second, third, fourth player of a computer: which one)
            if (slot.localIndex > 0)
                info.role = slot.localIndex == 1 ? "2nd player" : slot.localIndex == 2 ? "3rd player" : "4th player";
            else if (info.mine)
                info.role = slot.peer == 0 ? "You, host" : "You";
            else
                info.role = slot.peer == 0 ? "Host" : "Player";
            info.pingMs = info.mine ? -1 : peer->pingMs;
            break;
    }
    return true;
}

// The sprite of the player in a slot, as it will be in the match (the skin of its owner, the default one
// while it is downloaded and for computer players); frame 0 stands, 1 runs
const gfxSprite* MI_OnlineTeamSelect::skinFrame(int slot, const Info& info, int frame)
{
    if (!info.human && !info.cpu)
        return nullptr;
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    //(the skin the match will load: rollback::loadMatchSkin, the default one for computer players)
    std::string path = lobby->slotSkinPath(slot);
    if (path.empty())
        path = defaultSkinPath();
    Skin& skin = m_skins[slot];
    if (path != skin.path || skin.color != info.color) {
        skin.path = path;
        skin.color = static_cast<short>(info.color);
        skin.strip = SpriteStrip();
        try {
            skin.strip = gfx_loadmenuskin(path, skin.color, false);
        } catch (const std::string&) {
            try {
                skin.strip = gfx_loadmenuskin(defaultSkinPath(), skin.color, false);
            } catch (const std::string&) {
            }
        }
    }
    const gfxSprite& sprite = skin.strip[frame ? 2 : 0];
    return sprite ? &sprite : nullptr;
}

void MI_OnlineTeamSelect::Draw()
{
    if (!m_visible)
        return;
    rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby)
        return;

    m_background->Draw();

    Info info[4];
    bool have[4] {};
    for (int s = 0; s < 4; s++)
        have[s] = gather(s, info[s]) && info[s].kind != rollback::SlotKind::Empty;

    // The teams: the players in them, in columns
    const int frame = (m_tick / 8) & 1;
    for (int team = 0; team < 4; team++) {
        int row = 0;
        for (int s = 0; s < 4; s++) {
            if (!have[s] || info[s].team != team)
                continue;
            if (const gfxSprite* sprite = skinFrame(s, info[s], info[s].ready ? 0 : frame))
                sprite->draw(team * 96 + 48 + m_pos.x, row * 36 + 52 + m_pos.y, {0, 0, 32, 32});
            rm->spr_menu_boxed_numbers.draw(team * 96 + 44 + m_pos.x, row * 36 + 72 + m_pos.y, {s * 16, info[s].color * 16, 16, 16});
            ++row;
        }
    }

    // The players' cards: the number, the name (a crown: the host), the Ready mushroom (not ready yet:
    // dots going), who it is and the connection; this computer's players in a gold frame (over the
    // footer: 24 higher than they were)
    constexpr int CARD_Y = 344;
    for (int s = 0; s < 4; s++) {
        if (!have[s])
            continue;
        const Info& i = info[s];
        const int cardX = s * 160 + 16;
        if (i.mine) {
            prompts::fill(cardX - 4, CARD_Y - 4, 136, 104, BLACK);
            prompts::fill(cardX - 2, CARD_Y - 2, 132, 100, {248, 200, 40, 255});
        }
        rm->spr_player_select_ready.draw(cardX, CARD_Y, {0, 0, 128, 96});
        rm->spr_menu_boxed_numbers.draw(cardX + 16, CARD_Y + 20, {s * 16, i.color * 16, 16, 16});
        const bool host = i.human && lobby->slot(s).peer == 0 && lobby->slot(s).localIndex == 0;
        const int nameW = host ? 62 : 80;
        rm->menu_font_small.drawChopRight(cardX + 36, CARD_Y + 36 - rm->menu_font_small.getHeight(), nameW, i.name);
        if (host)
            theme::crown(cardX + 36 + std::min(nameW, rm->menu_font_small.getWidth(i.name)) + 2, CARD_Y + 21);
        // (the empty circle of the sprite is not drawn: a player who is not ready shows dots)
        if (i.ready) {
            rm->spr_player_select_ready.draw(cardX + 48, CARD_Y + 40, {128, i.human ? 32 : 64, 34, 32});
        } else if (i.human) {
            const int lit = (m_tick / 12) % 4;
            for (int d = 0; d < 3; d++) {
                prompts::fill(cardX + 50 + d * 10, CARD_Y + 52, 8, 8, BLACK);
                prompts::fill(cardX + 52 + d * 10, CARD_Y + 54, 4, 4, d < lit ? SDL_Color {248, 200, 40, 255} : SDL_Color {150, 150, 160, 255});
            }
        }

        std::string line = i.role;
        const bool showPing = i.human && !i.mine && lobby->slot(s).localIndex == 0;
        if (i.downloadingSkin)
            line = "skin...";
        if (showPing) {
            rm->menu_font_small.drawChopCentered(cardX + 54, CARD_Y + 73, 88, line);
            theme::signal(cardX + 98, CARD_Y + 70, i.pingMs);
        } else {
            rm->menu_font_small.drawChopCentered(cardX + 64, CARD_Y + 73, 112, line);
        }
    }
}


// ---- MI_LockTag

void MI_LockTag::Draw()
{
    if (!m_visible || m_text.empty())
        return;
    if (m_right)
        rm->menu_font_small.drawRightJustified(m_pos.x, m_pos.y, m_text);
    else
        rm->menu_font_small.draw(m_pos.x, m_pos.y, m_text);
}


// ---- MI_LobbyMap

MI_LobbyMap::MI_LobbyMap(const LobbyRect& frame, const LobbyRect& map)
    : MI_MapPreview(&rm->spr_selectfield, frame.x, frame.y, 352, 0)
    , m_frame(frame)
    , m_map(map)
{}

void MI_LobbyMap::SetText(std::string text)
{
    m_text = std::move(text);
}

void MI_LobbyMap::SetProgress(float fraction, std::string label)
{
    m_fraction = fraction;
    m_label = std::move(label);
}

void MI_LobbyMap::Draw()
{
    if (!m_visible)
        return;

    creamFrame(m_frame.x, m_frame.y);

    if (m_available) {
        rectDst = {m_map.x, m_map.y, 320, 240};
        DrawContent();
        return;
    }

    // The map is not here: its name (and the progress of the transfer) on the cream paper
    const gfxFont& font = rm->menu_font_large;
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= m_text.size()) {
        size_t end = m_text.find('\n', pos);
        if (end == std::string::npos)
            end = m_text.size();
        lines.push_back(ellipsize(font, m_text.substr(pos, end - pos), m_map.w - 16));
        pos = end + 1;
    }
    int y = m_map.y + m_map.h / 2 - static_cast<int>(lines.size()) * 30 / 2 - (m_fraction >= 0.0f ? 20 : 0);
    for (const std::string& line : lines) {
        font.drawCentered(m_map.x + m_map.w / 2, y, line);
        y += 30;
    }

    if (m_fraction >= 0.0f) {
        // The slider bar of the option menus: the track, with its red flag where the transfer is
        const int barX = m_map.x + 20;
        const int barW = m_map.w - 40;
        const int barY = m_map.y + m_map.h / 2 + 20;
        rm->menu_font_small.drawCentered(m_map.x + m_map.w / 2, barY - 18, ellipsize(rm->menu_font_small, m_label, m_map.w - 16));
        for (int px = barX; px < barX + barW; px += 8)
            rm->menu_slider_bar.draw(px, barY, {8, 0, std::min(8, barX + barW - px), 16});
        const int flagX = barX + static_cast<int>(static_cast<float>(barW - 12) * std::clamp(m_fraction, 0.0f, 1.0f));
        rm->menu_slider_bar.draw(flagX, barY, {164, 0, 12, 16});
    }
}


// ---- MI_LobbyScene

MI_LobbyScene::MI_LobbyScene()
    : UI_Control(0, 0)
    , m_statusRect {20, 40, 600, 32}
    , m_hintRect {10, 354, 620, 16}
    , m_title("Team and Character Selection")
{}

void MI_LobbyScene::SetStatus(std::string text, Tone tone, std::string hint)
{
    m_status = std::move(text);
    m_tone = tone;
    m_hint = std::move(hint);
}

void MI_LobbyScene::Draw()
{
    if (!m_visible)
        return;

    // The header bar, as on every menu of the game
    rm->menu_plain_field.draw(0, 0, {0, 0, 320, 32});
    rm->menu_plain_field.draw(320, 0, {192, 0, 320, 32});
    rm->menu_font_large.drawCentered(320, 5, m_title);

    using prompts::Pad;
    if (m_joining) {
        menupanel::box(144, 110, 352, 220);
        theme::spinningCoin(304, 150, m_tick++);
        rm->menu_font_large.drawCentered(320, 196, "Connecting...");
        rm->menu_font_small.drawCentered(320, 232, ellipsize(rm->menu_font_small, m_status, 320));
        //After a moment, how long it has been (joining gives up after 15 seconds)
        const int seconds = m_joinTicks++ / 60;
        if (seconds >= 2)
            rm->menu_font_small.drawCentered(320, 252, ("No answer yet (" + std::to_string(seconds) + " s)").c_str());
        prompts::drawPrompts(320, 280, {{{Pad::B}, {prompts::menuKey(prompts::MENU_CANCEL)}, "stop"}});
        return;
    }
    ++m_tick;

    // The status bar: what the selected thing does, else what is going on (its icon: fine, a
    // warning, a problem)
    const LobbyRect& r = m_statusRect;
    const std::string& line = m_hint.empty() ? m_status : m_hint;
    blueBar(r.x, r.y, r.w);
    //(the large font when the line fits, else the small one)
    const bool large = rm->menu_font_large.getWidth(line) <= r.w - 80;
    const gfxFont& font = large ? rm->menu_font_large : rm->menu_font_small;
    const int lineW = std::min(font.getWidth(line), r.w - 80);
    theme::statusIcon(r.x + r.w / 2 - lineW / 2 - 14, r.y + 8, m_hint.empty() ? static_cast<theme::Status>(m_tone) : theme::Status::Info);
    font.drawCentered(r.x + r.w / 2 + 12, r.y + (large ? 5 : 9), ellipsize(font, line, r.w - 80));

    // The options page: its fields in a panel
    if (m_optionsPage)
        menupanel::box(46, 98, 548, 236);

    // Along the bottom: the buttons of what can be done
    menupanel::footer(m_prompts);
}


// ---- MI_PromptRow

void MI_PromptRow::Draw()
{
    if (m_visible && m_row)
        prompts::drawPrompts(m_pos.x, m_pos.y, m_row());
}


// ---- MI_WrappedText

MI_WrappedText::MI_WrappedText(short x, short y, short width, int maxLines)
    : UI_Control(x, y)
    , m_width(width)
    , m_maxLines(maxLines)
{}

void MI_WrappedText::SetText(std::string text, Tone tone)
{
    m_text = std::move(text);
    m_tone = tone;
}

namespace {
// The words of a text in lines at most `width` wide
std::vector<std::string> wrapWords(const gfxFont& font, const std::string& text, int width)
{
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(' ', pos);
        if (end == std::string::npos)
            end = text.size();
        const std::string word = text.substr(pos, end - pos);
        pos = end + 1;

        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && font.getWidth(candidate) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}
} // namespace

void MI_WrappedText::Draw()
{
    if (!m_visible || m_text.empty())
        return;

    //(in a panel: the large font when the text fits on one line, else the small one)
    const gfxFont* chosen = &rm->menu_font_large;
    std::vector<std::string> lines = wrapWords(*chosen, m_text, m_width);
    if (m_boxed && lines.size() > 1) {
        chosen = &rm->menu_font_small;
        lines = wrapWords(*chosen, m_text, m_width);
    }
    const gfxFont& font = *chosen;

    if (static_cast<int>(lines.size()) > m_maxLines) {
        lines.resize(static_cast<size_t>(m_maxLines));
        lines.back() = ellipsize(font, lines.back(), m_width);
    }

    const int lineHeight = font.getHeight() + 2;
    int centerX = m_pos.x;
    if (m_boxed) {
        //(as wide as the longest line, the icon at the left)
        int widest = 0;
        for (const std::string& text : lines)
            widest = std::max(widest, font.getWidth(text));
        const int w = widest + 64;
        const int x = m_pos.x - w / 2;
        const int h = static_cast<int>(lines.size()) * lineHeight + 8;
        menupanel::box(x, m_pos.y - 5, w, h);
        theme::statusIcon(x + 12, m_pos.y - 5 + (h - 16) / 2, static_cast<theme::Status>(m_tone));
        centerX += 12;
    }
    int y = m_pos.y;
    for (const std::string& text : lines) {
        font.drawChopCentered(centerX, y, m_width, text);
        y += lineHeight;
    }
}


// ---- MI_ProgressBar

MI_ProgressBar::MI_ProgressBar(short x, short y, short width, short height)
    : UI_Control(x, y)
    , m_width(width)
    , m_height(height)
{}

void MI_ProgressBar::Draw()
{
    if (!m_visible)
        return;

    SDL_Surface* dst = blitdest;
    SDL_Rect frame {m_pos.x, m_pos.y, m_width, m_height};
    SDL_FillSurfaceRect(dst, &frame, SDL_MapSurfaceRGB(dst, 248, 248, 248));
    SDL_Rect inner {m_pos.x + 2, m_pos.y + 2, m_width - 4, m_height - 4};
    SDL_FillSurfaceRect(dst, &inner, SDL_MapSurfaceRGB(dst, 40, 40, 40));
    SDL_Rect fill {m_pos.x + 2, m_pos.y + 2, static_cast<int>(static_cast<float>(m_width - 4) * m_fraction), m_height - 4};
    SDL_FillSurfaceRect(dst, &fill, SDL_MapSurfaceRGB(dst, 120, 176, 0));
}
