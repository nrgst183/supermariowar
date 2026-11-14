#include "MI_LobbyWidgets.h"

#include "FileList.h"
#include "GameValues.h"
#include "gfx.h"
#include "ResourceManager.h"
#include "rollback/NetGame.h"

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

const char* const TEAM_NAMES[4] = {"Red", "Green", "Yellow", "Blue"};

// The skin of the computer players: the robot, if this game has it
std::string cpuSkinPath()
{
    static bool searched = false;
    static std::string path;
    if (!searched) {
        searched = true;
        for (size_t i = 0; skinlist && i < skinlist->count(); i++) {
            std::string name = skinlist->at(i).name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name.find("r.o.b") != std::string::npos || name == "rob") {
                path = skinlist->at(i).path.string();
                break;
            }
        }
    }
    return path;
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

// The orange and white player card of the team select screen, at any size: the corners of the
// 128 x 96 sprite and its edges and middle stretched
void playerCard(int x, int y, int w, int h)
{
    constexpr int C = 16;
    const gfxSprite& s = rm->spr_player_select_ready;
    s.draw(x, y, {0, 0, C, C});
    s.draw(x + w - C, y, {128 - C, 0, C, C});
    s.draw(x, y + h - C, {0, 96 - C, C, C});
    s.draw(x + w - C, y + h - C, {128 - C, 96 - C, C, C});
    s.drawStretch({C, 0, 128 - 2 * C, C}, blitdest, {x + C, y, w - 2 * C, C});
    s.drawStretch({C, 96 - C, 128 - 2 * C, C}, blitdest, {x + C, y + h - C, w - 2 * C, C});
    s.drawStretch({0, C, C, 96 - 2 * C}, blitdest, {x, y + C, C, h - 2 * C});
    s.drawStretch({128 - C, C, C, 96 - 2 * C}, blitdest, {x + w - C, y + C, C, h - 2 * C});
    s.drawStretch({C, C, 128 - 2 * C, 96 - 2 * C}, blitdest, {x + C, y + C, w - 2 * C, h - 2 * C});
}

} // namespace


// ---- layout

LobbyLayout lobbyLayout()
{
    LobbyLayout l;
    // Left: the Single Game menu's fields and map frame; right: the players and the settings
    for (int i = 0; i < 3; i++)
        l.field[i] = {10, static_cast<short>(36 + i * 34), 352, 32};
    l.fieldIndent = 96;
    l.mapFrame = {10, 138, 352, 270};
    l.map = {26, 152, 320, 240};
    for (int i = 0; i < 4; i++)
        l.card[i] = {398, static_cast<short>(36 + i * 68), 232, 64};
    l.players = {398, 308, 232, 32};
    l.delay = {398, 342, 232, 32};
    l.hint = {380, 377, 250, 32};
    l.status = {10, 410, 620, 32};
    l.buttonY = 446;
    l.buttonX[0] = 10;
    l.buttonX[1] = 220;
    l.buttonX[2] = 430;
    l.buttonW = 200;
    l.joinerW = 303;
    return l;
}


// ---- MI_LobbyCard

struct MI_LobbyCard::Info {
    rollback::SlotKind kind = rollback::SlotKind::Empty;
    bool occupied = false;
    bool human = false;
    bool cpu = false;
    bool mine = false;
    bool ready = false;
    bool downloadingSkin = false;
    bool lobbyHost = false;    // we are the host
    int team = 0;
    int color = 0;
    int pingMs = -1;
    std::string name;
    std::string role;
};

MI_LobbyCard::MI_LobbyCard(const LobbyRect& rect, int slot)
    : UI_Control(rect.x, rect.y)
    , m_width(rect.w)
    , m_height(rect.h)
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
    ++m_tick;
}

bool MI_LobbyCard::gather(Info& info) const
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby)
        return false;

    const rollback::LobbySlot& slot = lobby->slot(m_slot);
    info.kind = slot.kind;
    info.occupied = slot.kind != rollback::SlotKind::Empty;
    info.human = slot.kind == rollback::SlotKind::Human;
    info.cpu = slot.kind == rollback::SlotKind::Cpu;
    info.mine = info.human && slot.peer == lobby->localPeer();
    info.lobbyHost = lobby->isHost();
    const rollback::LobbyPeer* peer = info.human ? &lobby->peer(slot.peer) : nullptr;
    info.ready = info.human ? (slot.peer == 0 || peer->ready) : info.cpu;
    info.team = slot.team & 3;
    info.color = slot.color & 3;
    if (m_overrideFrames > 0) {
        info.team = m_overrideTeam & 3;
        info.color = m_overrideColor & 3;
    }
    info.downloadingSkin = info.human && lobby->slotSkinDownloading(m_slot);

    switch (slot.kind) {
        case rollback::SlotKind::Empty:
            info.name = "Open";
            info.role = lobby->isHost() ? "Add CPU" : "Waiting...";
            break;
        case rollback::SlotKind::Cpu:
            info.name = "CPU";
            info.role = "Computer";
            break;
        case rollback::SlotKind::Human:
            info.name = peer->name;
            if (slot.localIndex > 0)
                info.name += std::format(" ({})", slot.localIndex + 1);
            if (slot.localIndex > 0)
                info.role = "2nd player";
            else if (info.mine)
                info.role = slot.peer == 0 ? "You, the host" : "You";
            else
                info.role = slot.peer == 0 ? "Host" : "Player";
            info.pingMs = info.mine ? -1 : peer->pingMs;
            break;
    }
    return true;
}

// The sprite of the player in this card (the skin of its owner, the default one while it is
// downloaded, the robot for computer players); frame 0 stands, 1 runs
const gfxSprite* MI_LobbyCard::skinFrame(const Info& info, int frame)
{
    if (!info.human && !info.cpu)
        return nullptr;
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    std::string path = info.human ? lobby->slotSkinPath(m_slot) : cpuSkinPath();
    if (path.empty())
        path = defaultSkinPath();
    if (path != m_skinPath || m_skinColor != info.color) {
        const std::string wanted = path;
        m_skinPath = path;
        m_skinColor = static_cast<short>(info.color);
        m_skin = SpriteStrip();
        try {
            m_skin = gfx_loadmenuskin(path, m_skinColor, false);
        } catch (const std::string&) {
            try {
                m_skin = gfx_loadmenuskin(defaultSkinPath(), m_skinColor, false);
            } catch (const std::string&) {
            }
        }
    }
    const gfxSprite& sprite = m_skin[frame ? 2 : 0];
    return sprite ? &sprite : nullptr;
}

void MI_LobbyCard::Draw()
{
    if (!m_visible)
        return;
    Info info;
    if (!gather(info))
        return;
    drawCard(info);
}

void MI_LobbyCard::drawCard(const Info& info)
{
    const gfxFont& largeFont = rm->menu_font_large;
    const gfxFont& smallFont = rm->menu_font_small;
    const int x = m_pos.x;
    const int y = m_pos.y;
    const int w = m_width;
    const int h = m_height;

    playerCard(x, y, w, h);

    // The cursor: the arrow of the select fields, beating while the card is edited
    if (fSelected || fModifying) {
        const int beat = fModifying ? ((m_tick / 8) & 1) * 3 : 0;
        rm->spr_selectfield.draw(x - 26 + beat, y + (h - 24) / 2, {32, 88, 26, 24});
    }

    const int textX = x + 58;
    const int textW = w - 58 - 46;
    if (!info.occupied) {
        largeFont.drawChopRight(textX, y + 9, textW + 30, "Open");
        smallFont.drawChopRight(textX, y + 38, textW + 30, info.role);
        return;
    }

    // The number and the character, like the cards of the team select screen
    const int frame = info.ready ? 0 : ((m_tick / 8) & 1);
    if (const gfxSprite* sprite = skinFrame(info, frame))
        sprite->draw(x + 20, y + 10, {0, 0, 32, 32});
    rm->spr_menu_boxed_numbers.draw(x + 12, y + 34, {m_slot * 16, info.color * 16, 16, 16});

    largeFont.drawChopRight(textX, y + 9, textW, info.name);
    // Who (You, Host, CPU), the team, and the ping: short enough for the space before the mushroom
    std::string who = info.cpu ? "CPU" : info.mine ? "You" : info.role == "Host" ? "Host" : info.role == "2nd player" ? "2nd" : "";
    std::string line = who;
    const auto add = [&line](const std::string& part) { line += (line.empty() ? "" : " ") + part; };
    add(TEAM_NAMES[info.team]);
    if (info.human && !info.mine && info.role != "2nd player")
        add(info.pingMs >= 0 ? std::format("{}ms", info.pingMs) : std::string("--ms"));
    if (info.downloadingSkin)
        add("skin..");
    smallFont.drawChopRight(textX, y + 38, textW + 14, line);

    // Ready, as on the team select screen: the red mushroom for a player, the blue one for a bot, and
    // the dark one while the player is not ready
    rm->spr_player_select_ready.draw(x + w - 46, y + (h - 32) / 2, {128, info.ready ? (info.human ? 32 : 64) : 0, 34, 32});
}


// ---- MI_LobbyField

MI_LobbyField::MI_LobbyField(const LobbyRect& rect, short indent, std::string label)
    : UI_Control(rect.x, rect.y)
    , m_rect(rect)
    , m_indent(indent)
    , m_label(std::move(label))
{}

void MI_LobbyField::SetValue(std::string value, int modeIcon)
{
    m_value = std::move(value);
    m_icon = modeIcon;
}

void MI_LobbyField::Draw()
{
    if (!m_visible)
        return;
    // The label part, the joint and the value part of a select field of the menus
    rm->spr_selectfield.draw(m_rect.x, m_rect.y, {0, 0, m_indent - 16, 32});
    rm->spr_selectfield.draw(m_rect.x + m_indent - 16, m_rect.y, {0, 64, 32, 32});
    rm->spr_selectfield.draw(m_rect.x + m_indent + 16, m_rect.y, {528 - m_rect.w + m_indent, 0, m_rect.w - m_indent - 16, 32});
    rm->menu_font_large.drawChopRight(m_rect.x + 16, m_rect.y + 5, m_indent - 8, m_label);
    int valueX = m_rect.x + m_indent + 8;
    if (m_icon >= 0) {
        rm->menu_mode_small.draw(valueX, m_rect.y + 8, {m_icon * 16, 0, 16, 16});
        valueX += 20;
    }
    rm->menu_font_large.drawChopRight(valueX, m_rect.y + 5, m_rect.w - (valueX - m_rect.x) - 16, m_value);
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
    , m_layout(lobbyLayout())
{}

void MI_LobbyScene::SetStatus(std::string text, Tone, std::string hint)
{
    m_status = std::move(text);
    m_hint = std::move(hint);
}

void MI_LobbyScene::Draw()
{
    if (!m_visible)
        return;

    // The header bar, as on every menu of the game
    rm->menu_plain_field.draw(0, 0, {0, 0, 320, 32});
    rm->menu_plain_field.draw(320, 0, {192, 0, 320, 32});
    rm->menu_font_large.drawCentered(320, 5, "Online Lobby");

    if (m_joining) {
        creamFrame(144, 110);
        rm->menu_font_large.drawCentered(320, 190, "Connecting...");
        rm->menu_font_small.drawCentered(320, 230, ellipsize(rm->menu_font_small, m_status, 320));
        rm->menu_font_small.drawCentered(320, 262, "Select Leave to cancel");
        return;
    }

    const LobbyRect& r = m_layout.status;
    blueBar(r.x, r.y, r.w);
    rm->menu_font_large.drawCentered(r.x + r.w / 2, r.y + 5, ellipsize(rm->menu_font_large, m_status, r.w - 24));

    // The hint, in the small font, over two lines
    const gfxFont& font = rm->menu_font_small;
    const LobbyRect& hr = m_layout.hint;
    std::string line;
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < m_hint.size()) {
        size_t end = m_hint.find(' ', pos);
        if (end == std::string::npos)
            end = m_hint.size();
        const std::string word = m_hint.substr(pos, end - pos);
        pos = end + 1;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && font.getWidth(candidate) > hr.w) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty())
        lines.push_back(line);
    if (lines.size() > 2) {
        lines.resize(2);
        lines.back() = ellipsize(font, lines.back(), hr.w);
    }
    int y = hr.y;
    for (const std::string& text : lines) {
        font.drawChopCentered(hr.x + hr.w / 2, y, hr.w, text);
        y += 16;
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
