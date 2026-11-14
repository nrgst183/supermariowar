#include "MI_LobbyWidgets.h"

#include "FileList.h"
#include "GameValues.h"
#include "gfx.h"
#include "ResourceManager.h"
#include "rollback/NetGame.h"

#include <algorithm>
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

} // namespace


// ---- MI_LobbyCard

namespace {

struct Rgb { Uint8 r, g, b; };
// The team columns of the team select screen: red, green, yellow, blue
constexpr Rgb TEAM_COLORS[4] = {{237, 80, 84}, {86, 220, 115}, {240, 214, 100}, {88, 120, 237}};
const char* const COLOR_NAMES[4] = {"red", "green", "yellow", "blue"};

void fillRect(int x, int y, int w, int h, Rgb c)
{
    SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(blitdest, &rect, SDL_MapSurfaceRGB(blitdest, c.r, c.g, c.b));
}

} // namespace

MI_LobbyCard::MI_LobbyCard(short x, short y, short width, short height, int slot)
    : UI_Control(x, y)
    , m_width(width)
    , m_height(height)
    , m_slot(slot)
{}

bool MI_LobbyCard::editable() const
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby || lobby->phase() != rollback::NetLobby::Phase::Lobby)
        return false;
    const rollback::LobbySlot& slot = lobby->slot(m_slot);
    if (slot.kind == rollback::SlotKind::Empty)
        return false;
    return lobby->isHost() || (slot.kind == rollback::SlotKind::Human && slot.peer == lobby->localPeer());
}

std::string MI_LobbyCard::hint() const
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby || lobby->phase() != rollback::NetLobby::Phase::Lobby)
        return {};
    const rollback::LobbySlot& slot = lobby->slot(m_slot);
    const bool host = lobby->isHost();

    if (fModifying) {
        std::string text = "Left/Right: team   Up/Down: color   Select: done";
        if (host && slot.kind == rollback::SlotKind::Cpu)
            text += "   Space: remove CPU";
        else if (host && slot.kind == rollback::SlotKind::Human && slot.peer != lobby->localPeer())
            text += "   Space: kick";
        return text;
    }
    if (slot.kind == rollback::SlotKind::Empty)
        return host ? "Select: add a CPU player" : "";
    if (editable())
        return "Select: change team and color";
    return "Only this player or the host can change it";
}

MenuCodeEnum MI_LobbyCard::Modify(bool modify)
{
    if (!modify) {
        fModifying = false;
        return MENU_CODE_MODIFY_ACCEPTED;
    }

    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (fDisable || !lobby || lobby->phase() != rollback::NetLobby::Phase::Lobby)
        return MENU_CODE_UNSELECT_ITEM;

    if (lobby->slot(m_slot).kind == rollback::SlotKind::Empty)
        return lobby->isHost() ? MENU_CODE_ONLINE_SLOT_ACTION : MENU_CODE_UNSELECT_ITEM;

    if (!editable())
        return MENU_CODE_UNSELECT_ITEM;

    fModifying = true;
    return MENU_CODE_MODIFY_ACCEPTED;
}

MenuCodeEnum MI_LobbyCard::SendInput(CPlayerInput* playerInput)
{
    rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby || !editable()) {
        fModifying = false;
        return MENU_CODE_UNSELECT_ITEM;
    }

    const rollback::LobbySlot& slot = lobby->slot(m_slot);
    int team = m_overrideFrames > 0 ? m_overrideTeam : slot.team;
    int color = m_overrideFrames > 0 ? m_overrideColor : slot.color;

    for (int p = 0; p < 4; p++) {
        const COutputControl& keys = playerInput->outputControls[p];
        bool changed = false;

        if (keys.menu_left.fPressed) {
            team = (team + 3) & 3;
            changed = true;
        } else if (keys.menu_right.fPressed) {
            team = (team + 1) & 3;
            changed = true;
        } else if (keys.menu_up.fPressed) {
            color = (color + 3) & 3;
            changed = true;
        } else if (keys.menu_down.fPressed) {
            color = (color + 1) & 3;
            changed = true;
        }

        if (changed) {
            if (team != slot.team)
                lobby->setSlotTeam(m_slot, team);
            if (color != slot.color)
                lobby->setSlotColor(m_slot, color);
            m_overrideTeam = team;
            m_overrideColor = color;
            m_overrideFrames = 60;
            return MENU_CODE_NONE;
        }

        if (keys.menu_random.fPressed && lobby->isHost() && slot.kind != rollback::SlotKind::Empty
            && !(slot.kind == rollback::SlotKind::Human && slot.peer == lobby->localPeer())) {
            fModifying = false;
            return MENU_CODE_ONLINE_SLOT_ACTION;
        }

        if (keys.menu_select.fPressed || keys.menu_cancel.fPressed) {
            fModifying = false;
            return MENU_CODE_UNSELECT_ITEM;
        }
    }
    return MENU_CODE_NONE;
}

MenuCodeEnum MI_LobbyCard::MouseClick(short iMouseX, short iMouseY)
{
    if (iMouseX >= m_pos.x && iMouseX < m_pos.x + m_width && iMouseY >= m_pos.y && iMouseY < m_pos.y + m_height)
        return MENU_CODE_CLICKED;
    return MENU_CODE_NONE;
}

void MI_LobbyCard::Update()
{
    if (m_overrideFrames > 0)
        --m_overrideFrames;
    if (++m_animTimer > 7) {
        m_animTimer = 0;
        m_animFrame = (m_animFrame + 1) & 3;
    }
}

void MI_LobbyCard::Draw()
{
    if (!m_visible)
        return;

    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby)
        return;

    const rollback::LobbySlot& slot = lobby->slot(m_slot);
    const bool occupied = slot.kind != rollback::SlotKind::Empty;
    const bool human = slot.kind == rollback::SlotKind::Human;
    const bool mine = human && slot.peer == lobby->localPeer();
    const rollback::LobbyPeer* peer = human ? &lobby->peer(slot.peer) : nullptr;
    const bool ready = human ? (slot.peer == 0 || peer->ready) : slot.kind == rollback::SlotKind::Cpu;

    int team = slot.team & 3;
    int color = slot.color & 3;
    if (m_overrideFrames > 0) {
        team = m_overrideTeam & 3;
        color = m_overrideColor & 3;
    }

    const gfxFont& largeFont = rm->menu_font_large;
    const gfxFont& smallFont = rm->menu_font_small;

    // The frame: white while selected, gold for the player's own cards
    Rgb frame = mine ? Rgb {250, 200, 40} : Rgb {16, 16, 16};
    if (fModifying && (m_animFrame & 1))
        frame = {255, 255, 255};
    else if (fSelected)
        frame = {255, 255, 255};
    fillRect(m_pos.x, m_pos.y, m_width, m_height, frame);
    fillRect(m_pos.x + 3, m_pos.y + 3, m_width - 6, m_height - 6, ready && occupied ? Rgb {36, 62, 44} : Rgb {44, 46, 54});

    // The team block, as a column of the team select screen
    const int blockW = 62;
    const int bx = m_pos.x + 3;
    const int by = m_pos.y + 3;
    const int bh = m_height - 6;
    if (occupied)
        fillRect(bx, by, blockW, bh, TEAM_COLORS[team]);
    else
        fillRect(bx, by, blockW, bh, {70, 72, 80});

    if (human) {
        if (m_skinId != game_values.skinids[m_slot] || m_skinColor != color) {
            m_skinId = game_values.skinids[m_slot];
            m_skinColor = static_cast<short>(color);
            m_skin = SpriteStrip();
            try {
                if (m_skinId >= 0 && m_skinId < static_cast<short>(skinlist->count()))
                    m_skin = gfx_loadmenuskin(skinlist->at(m_skinId).path, m_skinColor, false);
            } catch (const std::string&) {
            }
        }
        const gfxSprite& frameSprite = m_skin[ready ? 0 : (m_animFrame & 1) * 2];
        if (frameSprite)
            frameSprite.draw(bx + 15, by + 6, {0, 0, 32, 32});
    } else if (slot.kind == rollback::SlotKind::Cpu) {
        rm->spr_player_select_background.draw(bx + 10, by + 6, {416, ready ? 0 : m_animFrame * 32, 42, 32});
    }

    if (occupied) {
        rm->spr_menu_boxed_numbers.draw(bx + (blockW - 16) / 2, by + 38, {m_slot * 16, color * 16, 16, 16});
        if (fModifying) {
            largeFont.draw(bx + 1, by + 12, "<");
            largeFont.draw(bx + blockW - 13, by + 12, ">");
        }
    }

    // Who
    const int tx = bx + blockW + 10;
    const int textWidth = m_width - (tx - m_pos.x) - 52;
    std::string name;
    std::string detail;
    std::string state;
    switch (slot.kind) {
        case rollback::SlotKind::Empty:
            name = "Open";
            state = lobby->isHost() ? "Select: add CPU" : "";
            break;
        case rollback::SlotKind::Cpu:
            name = "CPU";
            state = "Computer player";
            break;
        case rollback::SlotKind::Human:
            name = peer->name;
            if (slot.localIndex > 0)
                name += std::format(" ({})", slot.localIndex + 1);
            if (slot.localIndex == 0) {
                if (mine)
                    detail = slot.peer == 0 ? "You, the host" : "You";
                else if (slot.peer == 0)
                    detail = peer->pingMs >= 0 ? std::format("Host, {} ms", peer->pingMs) : "Host";
                else
                    detail = peer->pingMs >= 0 ? std::format("{} ms", peer->pingMs) : "-- ms";
            } else {
                detail = "Second player";
            }
            state = slot.peer == 0 ? (mine ? "" : "") : (peer->ready ? "Ready" : "Not ready");
            break;
    }

    if (occupied)
        largeFont.drawChopRight(tx, m_pos.y + 6, textWidth, name);
    else
        largeFont.drawChopRight(tx, m_pos.y + 20, textWidth, name);
    smallFont.drawChopRight(tx, m_pos.y + 32, textWidth, detail);
    smallFont.drawChopRight(tx, m_pos.y + 45, textWidth, state);

    // Ready or not, as on the team select screen
    if (occupied)
        rm->spr_player_select_ready.draw(m_pos.x + m_width - 46, m_pos.y + (m_height - 32) / 2, {128, ready ? (human ? 32 : 64) : 0, 34, 32});
}


// ---- MI_MapBox

MI_MapBox::MI_MapBox(short x, short y)
    : UI_Control(x, y)
{}

void MI_MapBox::SetText(std::string text)
{
    m_text = std::move(text);
}

void MI_MapBox::SetProgress(float fraction, std::string label)
{
    m_fraction = fraction;
    m_label = std::move(label);
}

void MI_MapBox::Draw()
{
    if (!m_visible)
        return;

    const gfxFont& font = rm->menu_font_large;

    if (!m_text.empty()) {
        rm->menu_dialog.draw(m_pos.x, m_pos.y, {0, 0, 336, 254});
        rm->menu_dialog.draw(m_pos.x + 336, m_pos.y, {496, 0, 16, 254});
        rm->menu_dialog.draw(m_pos.x, m_pos.y + 254, {0, 464, 336, 16});
        rm->menu_dialog.draw(m_pos.x + 336, m_pos.y + 254, {496, 464, 16, 16});
        fillRect(m_pos.x + 16, m_pos.y + 14, 320, 240, {44, 46, 54});

        std::vector<std::string> lines;
        size_t pos = 0;
        while (pos <= m_text.size()) {
            size_t end = m_text.find('\n', pos);
            if (end == std::string::npos)
                end = m_text.size();
            lines.push_back(m_text.substr(pos, end - pos));
            pos = end + 1;
        }
        int y = m_pos.y + 14 + 100 - static_cast<int>(lines.size()) * (font.getHeight() + 4) / 2;
        for (const std::string& line : lines) {
            font.drawCentered(m_pos.x + 176, y, line);
            y += font.getHeight() + 4;
        }
    }

    if (m_fraction >= 0.0f) {
        const int x = m_pos.x + 16;
        const int y = m_pos.y + 14 + 240 - 46;
        fillRect(x, y, 320, 46, {16, 16, 20});
        font.drawCentered(m_pos.x + 176, y + 3, m_label);
        const int barX = x + 10;
        const int barW = 300;
        fillRect(barX - 2, y + 28, barW + 4, 14, {250, 250, 250});
        fillRect(barX, y + 30, barW, 10, {40, 40, 40});
        fillRect(barX, y + 30, static_cast<int>(barW * std::clamp(m_fraction, 0.0f, 1.0f)), 10, {86, 220, 115});
    }
}

// ---- MI_WrappedText

MI_WrappedText::MI_WrappedText(short x, short y, short width, int maxLines)
    : UI_Control(x, y)
    , m_width(width)
    , m_maxLines(maxLines)
{}

void MI_WrappedText::SetText(std::string text)
{
    m_text = std::move(text);
}

void MI_WrappedText::Draw()
{
    if (!m_visible || m_text.empty())
        return;

    const gfxFont& font = rm->menu_font_large;

    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos < m_text.size()) {
        size_t end = m_text.find(' ', pos);
        if (end == std::string::npos)
            end = m_text.size();
        const std::string word = m_text.substr(pos, end - pos);
        pos = end + 1;

        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && font.getWidth(candidate) > m_width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty())
        lines.push_back(line);

    if (static_cast<int>(lines.size()) > m_maxLines) {
        lines.resize(static_cast<size_t>(m_maxLines));
        lines.back() = ellipsize(font, lines.back(), m_width);
    }

    int y = m_pos.y;
    for (const std::string& text : lines) {
        font.drawChopCentered(m_pos.x, y, m_width, text);
        y += font.getHeight() + 2;
    }
}


// ---- MI_ProgressBar

MI_ProgressBar::MI_ProgressBar(short x, short y, short width, short height)
    : UI_Control(x, y)
    , m_width(width)
    , m_height(height)
{}

void MI_ProgressBar::SetFraction(float fraction)
{
    m_fraction = std::clamp(fraction, 0.0f, 1.0f);
}

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
