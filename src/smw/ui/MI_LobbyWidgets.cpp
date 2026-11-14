#include "MI_LobbyWidgets.h"

#include "FileList.h"
#include "GameModeType.h"
#include "GameValues.h"
#include "gfx.h"
#include "ResourceManager.h"
#include "rollback/NetGame.h"
#include "util/SdlHelpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

struct Rgb { Uint8 r, g, b; };
// The team columns of the team select screen: red, green, yellow, blue
constexpr Rgb TEAM_COLORS[4] = {{237, 80, 84}, {86, 220, 115}, {240, 214, 100}, {88, 120, 237}};
const char* const TEAM_NAMES[4] = {"RED", "GREEN", "YELLOW", "BLUE"};
constexpr Rgb BLACK = {0, 0, 0};
constexpr Rgb WHITE = {248, 248, 248};
constexpr Rgb GOLD = {250, 200, 40};
constexpr Rgb NAVY = {22, 26, 52};
constexpr Rgb TONE_INFO = {64, 168, 240};
constexpr Rgb TONE_GOOD = {107, 203, 0};
constexpr Rgb TONE_WARN = {248, 200, 64};
constexpr Rgb TONE_ERROR = {237, 80, 84};

// lighter (percent > 0, toward white) or darker (< 0, toward black)
Rgb shade(Rgb c, int percent)
{
    const auto f = [percent](int v) {
        return static_cast<Uint8>(percent >= 0 ? v + (255 - v) * percent / 100 : v * (100 + percent) / 100);
    };
    return {f(c.r), f(c.g), f(c.b)};
}

void fillRect(int x, int y, int w, int h, Rgb c)
{
    if (w <= 0 || h <= 0)
        return;
    SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(blitdest, &rect, SDL_MapSurfaceRGB(blitdest, c.r, c.g, c.b));
}

// A translucent rectangle: a one pixel surface with alpha, stretched
void fillRectA(int x, int y, int w, int h, Rgb c, int alpha)
{
    if (w <= 0 || h <= 0)
        return;
    static SdlSurfacePtr pixel;
    if (!pixel) {
        pixel.reset(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32));
        SDL_SetSurfaceBlendMode(pixel.get(), SDL_BLENDMODE_BLEND);
    }
    SDL_FillSurfaceRect(pixel.get(), nullptr, SDL_MapSurfaceRGBA(pixel.get(), c.r, c.g, c.b, static_cast<Uint8>(alpha)));
    SDL_Rect rect {x, y, w, h};
    SDL_BlitSurfaceScaled(pixel.get(), nullptr, blitdest, &rect, SDL_SCALEMODE_NEAREST);
}

void outline(int x, int y, int w, int h, int thickness, Rgb c)
{
    fillRect(x, y, w, thickness, c);
    fillRect(x, y + h - thickness, w, thickness, c);
    fillRect(x, y, thickness, h, c);
    fillRect(x + w - thickness, y, thickness, h, c);
}

// A dark translucent panel with the black and white outline of the original menus' boxes
void panel(int x, int y, int w, int h, int alpha = 175, Rgb border = {232, 232, 240})
{
    fillRect(x, y, w, h, BLACK);
    fillRectA(x + 1, y + 1, w - 2, h - 2, NAVY, alpha);
    outline(x + 1, y + 1, w - 2, h - 2, 1, border);
    //Rounded corners
    for (const auto& c : {std::pair {x, y}, std::pair {x + w - 1, y}, std::pair {x, y + h - 1}, std::pair {x + w - 1, y + h - 1}})
        fillRect(c.first, c.second, 1, 1, {0, 0, 0});
}

// The orange and white plate of the team select screen's player cards
void plate(int x, int y, int w, int h)
{
    fillRect(x, y, w, h, BLACK);
    fillRect(x + 1, y + 1, w - 2, h - 2, {232, 232, 232});
    fillRect(x + 2, y + 2, w - 4, h - 4, {240, 192, 0});
    fillRect(x + 4, y + 4, w - 8, h - 8, {248, 248, 0});
    fillRect(x + 6, y + 6, w - 12, h - 12, WHITE);
    for (const auto& c : {std::pair {x, y}, std::pair {x + w - 1, y}, std::pair {x, y + h - 1}, std::pair {x + w - 1, y + h - 1}})
        fillRect(c.first, c.second, 2, 2, {0, 0, 0});
}

void fillCircle(int cx, int cy, int r, Rgb c)
{
    for (int dy = -r; dy <= r; dy++) {
        const int half = static_cast<int>(std::sqrt(static_cast<float>(r * r - dy * dy)) + 0.5f);
        fillRect(cx - half, cy + dy, half * 2 + 1, 1, c);
    }
}

// A small filled triangle: direction 0 left, 1 right, 2 up, 3 down; `size` is its length
void triangle(int x, int y, int size, int direction, Rgb c)
{
    for (int i = 0; i < size; i++) {
        const int span = (size - i) * 2 - 1;
        switch (direction) {
            case 0: fillRect(x + i, y - (size - i) + 1, 1, span, c); break;
            case 1: fillRect(x + size - 1 - i, y - (size - i) + 1, 1, span, c); break;
            case 2: fillRect(x - (size - i) + 1, y + i, span, 1, c); break;
            default: fillRect(x - (size - i) + 1, y + size - 1 - i, span, 1, c); break;
        }
    }
}

// A round badge with a check mark (ready) or three dots (waiting)
void statusBadge(int cx, int cy, int r, bool ready)
{
    fillCircle(cx, cy, r + 1, BLACK);
    if (ready) {
        fillCircle(cx, cy, r, {40, 170, 60});
        fillCircle(cx, cy - r / 3, r - 3, {90, 220, 100});
        fillCircle(cx, cy + 1, r - 2, {52, 196, 72});
        const int t = std::max(2, r / 4);
        for (int i = 0; i <= r / 2 + 1; i++)    //the short stroke down
            fillRect(cx - r / 2 + i - t / 2 + 1, cy + i - r / 6 - 1, t, t, WHITE);
        for (int i = 0; i <= r - 2; i++)    //the long stroke up
            fillRect(cx + 1 + i - t / 2 + 1, cy + r / 2 - r / 6 - 1 - i, t, t, WHITE);
    } else {
        fillCircle(cx, cy, r, {88, 94, 112});
        fillCircle(cx, cy - r / 3, r - 3, {112, 120, 140});
        fillCircle(cx, cy + 1, r - 2, {96, 102, 122});
        for (int i = -1; i <= 1; i++)
            fillRect(cx + i * (r / 2 + 1) - 1, cy - 1, 3, 3, WHITE);
    }
}

// Four bars that fill with the quality of the connection
void pingBars(int x, int y, int pingMs)
{
    const int level = pingMs < 0 ? 0 : pingMs < 50 ? 4 : pingMs < 100 ? 3 : pingMs < 180 ? 2 : 1;
    const Rgb color = level >= 3 ? TONE_GOOD : level == 2 ? TONE_WARN : TONE_ERROR;
    for (int i = 0; i < 4; i++) {
        const int h = 4 + i * 3;
        fillRect(x + i * 5, y + 13 - h, 4, h, i < level ? color : Rgb {70, 74, 92});
    }
}

// The blue bar of the original headers (the sprite is 512 wide: two halves, like MI_Button)
void blueBar(int x, int y, int width, bool dark = false)
{
    const int half = width >> 1;
    const int row = dark ? 32 : 0;
    rm->menu_plain_field.draw(x, y, {0, row, half, 32});
    rm->menu_plain_field.draw(x + half, y, {512 - width + half, row, width - half, 32});
}

int modeIconIndex(int modeId)
{
    if (modeId >= 0 && modeId < GAMEMODE_LAST)
        return modeId;
    switch (modeId) {
        case game_mode_pipe_minigame: return 25;
        case game_mode_boss_minigame: return 23;
        case game_mode_boxes_minigame: return 27;
        default: return -1;
    }
}

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

void drawPlayersIcon(int x, int y, int count)
{
    for (int i = 0; i < count; i++) {
        const int ox = x + (count == 1 ? 6 : i * 10);
        fillCircle(ox + 4, y + 5, 4, WHITE);
        fillCircle(ox + 4, y + 5, 3, {255, 205, 150});
        fillRect(ox, y + 10, 9, 6, WHITE);
        fillRect(ox + 1, y + 11, 7, 4, i == 0 ? Rgb {237, 80, 84} : Rgb {88, 120, 237});
    }
}

void drawClockIcon(int x, int y)
{
    fillCircle(x + 9, y + 9, 9, BLACK);
    fillCircle(x + 9, y + 9, 8, WHITE);
    fillCircle(x + 9, y + 9, 6, {64, 168, 240});
    fillRect(x + 9, y + 4, 2, 6, BLACK);    //hands
    fillRect(x + 9, y + 9, 5, 2, BLACK);
}

} // namespace


// ---- MI_LobbyButton

void MI_LobbyButton::Draw()
{
    if (!m_visible)
        return;
    MI_Button::Draw();
    if (m_dimmed)
        fillRectA(m_pos.x + 2, m_pos.y + 3, iWidth - 4, 26, BLACK, 120);
}


// ---- layouts

LobbyLayout lobbyLayout(LobbyStyle style)
{
    LobbyLayout l;
    l.style = style;
    if (style == LobbyStyle::Stage) {
        l.stage = {20, 38, 600, 188};
        for (int i = 0; i < 4; i++)
            l.card[i] = {static_cast<short>(28 + i * 148), 42, 140, 180};
        l.matchPanel = {20, 232, 600, 140};
        l.mapFrame = {32, 236, 168, 128};
        l.map = {36, 240, 160, 120};
        l.banner = {212, 240, 396, 32};
        l.mapRow = {212, 278, 396, 26};
        l.goalRow = {212, 306, 396, 26};
        l.chip[0] = {212, 334, 194, 28};
        l.chip[1] = {414, 334, 194, 28};
        l.status = {20, 378, 600, 42};
        l.buttonY = 426;
        l.buttonX[0] = 20;
        l.buttonX[1] = 224;
        l.buttonX[2] = 428;
        l.buttonW = 192;
        l.joinerW = 294;
    } else {
        l.banner = {10, 38, 352, 32};
        l.mapFrame = {10, 74, 352, 270};
        l.map = {26, 88, 320, 240};
        for (int i = 0; i < 4; i++)
            l.card[i] = {372, static_cast<short>(38 + i * 78), 256, 72};
        l.chip[0] = {10, 352, 198, 28};
        l.chip[1] = {220, 352, 198, 28};
        l.chip[2] = {430, 352, 198, 28};
        l.status = {10, 384, 618, 40};
        l.buttonY = 430;
        l.buttonX[0] = 10;
        l.buttonX[1] = 220;
        l.buttonX[2] = 430;
        l.buttonW = 198;
        l.joinerW = 303;
    }
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

MI_LobbyCard::MI_LobbyCard(const LobbyRect& rect, int slot, LobbyStyle style)
    : UI_Control(rect.x, rect.y)
    , m_width(rect.w)
    , m_height(rect.h)
    , m_slot(slot)
    , m_style(style)
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
    if (m_style == LobbyStyle::Stage)
        drawStage(info);
    else
        drawCard(info);
}

// Team stage: a column in the team's color, the character standing big on a pedestal, the name and the
// ping on a plate. A ready player jumps and has a check mark.
void MI_LobbyCard::drawStage(const Info& info)
{
    const gfxFont& largeFont = rm->menu_font_large;
    const gfxFont& smallFont = rm->menu_font_small;
    const int x = m_pos.x;
    const int y = m_pos.y;
    const int w = m_width;
    const int h = m_height;
    const int cx = x + w / 2;
    const Rgb team = info.occupied ? TEAM_COLORS[info.team] : Rgb {104, 108, 122};

    //The frame: white while selected, gold for the player's own card
    Rgb frame = info.mine ? GOLD : BLACK;
    if (fModifying && ((m_tick / 8) & 1))
        frame = WHITE;
    else if (fSelected)
        frame = WHITE;
    fillRect(x, y, w, h, BLACK);
    if (frame.r != 0 || frame.g != 0 || frame.b != 0)
        outline(x - 2, y - 2, w + 4, h + 4, 2, frame);

    //The column of the team, lighter at the top
    const int bodyH = h - 4;
    for (int band = 0; band < 12; band++) {
        const int y0 = y + 2 + band * bodyH / 12;
        const int y1 = y + 2 + (band + 1) * bodyH / 12;
        fillRect(x + 2, y0, w - 4, y1 - y0, shade(team, 26 - band * 3));
    }
    //The team's name on a ribbon
    fillRect(x + 2, y + 2, w - 4, 18, shade(team, -28));
    fillRect(x + 2, y + 20, w - 4, 1, BLACK);
    smallFont.drawCentered(cx, y + 3, info.occupied ? TEAM_NAMES[info.team] : "OPEN");

    //The pedestal
    const int px = x + 14;
    const int pw = w - 28;
    const int top = y + 106;
    Rgb pedestal = info.occupied ? team : Rgb {120, 124, 138};
    fillRect(px - 1, top - 1, pw + 2, 29, BLACK);
    fillRect(px, top, pw, 8, shade(pedestal, 38));
    fillRect(px, top + 8, pw, 18, shade(pedestal, -8));
    fillRect(px, top + 22, pw, 4, shade(pedestal, -38));
    fillRect(px, top + 8, pw, 1, shade(pedestal, -45));

    if (!info.occupied) {
        //An empty slot: a plus sign
        fillRect(cx - 14, top - 34, 28, 6, shade(pedestal, 55));
        fillRect(cx - 3, top - 45, 6, 28, shade(pedestal, 55));
    } else {
        //A shadow, then the character at 2.5 times its size; a ready one jumps
        const int phase = m_tick % 80;
        const int lift = (info.ready && phase < 26) ? static_cast<int>(16.0f * std::sin(3.14159f * static_cast<float>(phase) / 26.0f)) : 0;
        const int shadow = 24 - lift / 2;
        for (int row = 0; row < 5; row++) {
            const int half = shadow - (row == 0 || row == 4 ? 6 : row == 1 || row == 3 ? 2 : 0);
            fillRectA(cx - half, top - 1 + row, half * 2, 1, BLACK, 80);
        }
        const gfxSprite* sprite = skinFrame(info, lift > 6 ? 1 : 0);
        if (sprite)
            sprite->drawStretch({0, 0, 32, 32}, blitdest, {cx - 40, top - 75 - lift, 80, 80});

        rm->spr_menu_boxed_numbers.draw(px + 6, top + 9, {m_slot * 16, info.color * 16, 16, 16});
        if (info.ready)
            statusBadge(px + pw - 16, top + 17, 8, true);
        else
            statusBadge(px + pw - 16, top + 17, 8, false);
        if (fModifying) {
            triangle(x + 5, top - 34, 7, 0, WHITE);
            triangle(x + w - 12, top - 34, 7, 1, WHITE);
        }
    }

    //The name and the ping on a plate, like the cards of the team select screen
    plate(x + 2, y + 132, w - 4, h - 134);
    const std::string name = ellipsize(largeFont, info.name, w - 24);
    largeFont.drawCentered(cx, y + 135, name);
    std::string detail = info.role;
    if (info.occupied && info.human && !info.mine && info.pingMs >= 0)
        detail = std::format("{} ms", info.pingMs);
    else if (info.occupied && info.human && !info.mine)
        detail = "-- ms";
    if (info.downloadingSkin)
        detail += " (skin..)";
    smallFont.drawCentered(cx, y + 162, ellipsize(smallFont, detail, w - 20));
}

// Polished card: a portrait on the team's color, name, role and ping, and one status badge
void MI_LobbyCard::drawCard(const Info& info)
{
    const gfxFont& largeFont = rm->menu_font_large;
    const gfxFont& smallFont = rm->menu_font_small;
    const int x = m_pos.x;
    const int y = m_pos.y;
    const int w = m_width;
    const int h = m_height;

    Rgb frame = info.mine ? GOLD : Rgb {160, 168, 200};
    if (fModifying && ((m_tick / 8) & 1))
        frame = WHITE;
    else if (fSelected)
        frame = WHITE;
    const bool thick = fSelected || fModifying || info.mine;
    panel(x, y, w, h, info.ready && info.occupied ? 215 : 190, frame);
    if (thick)
        outline(x + 2, y + 2, w - 4, h - 4, 1, frame);

    //The portrait
    const int px = x + 5;
    const int py = y + 5;
    const int ps = h - 10;
    const Rgb team = info.occupied ? TEAM_COLORS[info.team] : Rgb {70, 74, 90};
    fillRect(px - 1, py - 1, ps + 2, ps + 2, BLACK);
    for (int band = 0; band < 8; band++)
        fillRect(px, py + band * ps / 8, ps, (band + 1) * ps / 8 - band * ps / 8, shade(team, 22 - band * 4));

    if (!info.occupied) {
        fillRect(px + ps / 2 - 12, py + ps / 2 - 3, 24, 6, shade(team, 45));
        fillRect(px + ps / 2 - 3, py + ps / 2 - 12, 6, 24, shade(team, 45));
    } else {
        const int frameNr = info.ready ? 0 : ((m_tick / 8) & 1);
        const gfxSprite* sprite = skinFrame(info, frameNr);
        if (sprite) {
            fillRectA(px + ps / 2 - 18, py + ps - 9, 36, 6, BLACK, 80);
            sprite->drawStretch({0, 0, 32, 32}, blitdest, {px + (ps - 64) / 2, py + ps - 66, 64, 64});
        }
        rm->spr_menu_boxed_numbers.draw(px + 2, py + ps - 18, {m_slot * 16, info.color * 16, 16, 16});
        if (fModifying) {
            triangle(px + 1, py + ps / 2 - 6, 6, 0, WHITE);
            triangle(px + ps - 7, py + ps / 2 - 6, 6, 1, WHITE);
        }
    }

    const int tx = px + ps + 8;
    const int textW = w - (tx - x) - 44;
    if (!info.occupied) {
        largeFont.drawChopRight(tx, y + 12, textW, "Open slot");
        smallFont.drawChopRight(tx, y + 42, textW, info.role);
        return;
    }

    largeFont.drawChopRight(tx, y + 4, textW, ellipsize(largeFont, info.name, textW));
    smallFont.drawChopRight(tx, y + 32, textW, ellipsize(smallFont, info.role + (info.downloadingSkin ? " (skin..)" : ""), textW));
    if (info.human && !info.mine) {
        pingBars(tx, y + 46, info.pingMs);
        smallFont.draw(tx + 26, y + 51, info.pingMs >= 0 ? std::format("{} ms", info.pingMs) : std::string("-- ms"));
    }

    //One status: ready (check) or still setting up
    statusBadge(x + w - 24, y + h / 2, 13, info.ready);
}


// ---- MI_LobbyChip

MI_LobbyChip::MI_LobbyChip(const LobbyRect& rect, std::string label, Icon icon)
    : UI_Control(rect.x, rect.y)
    , m_rect(rect)
    , m_label(std::move(label))
    , m_icon(icon)
{}

void MI_LobbyChip::add(std::string label, short value)
{
    m_items.emplace_back(std::move(label), value);
}

void MI_LobbyChip::clear()
{
    m_items.clear();
    m_index = 0;
}

bool MI_LobbyChip::setCurrentIndex(size_t index)
{
    if (index >= m_items.size())
        return false;
    m_index = index;
    return true;
}

bool MI_LobbyChip::setCurrentValue(short value)
{
    for (size_t i = 0; i < m_items.size(); i++) {
        if (m_items[i].second == value) {
            m_index = i;
            return true;
        }
    }
    return false;
}

MenuCodeEnum MI_LobbyChip::Modify(bool modify)
{
    if (fDisable)
        return MENU_CODE_UNSELECT_ITEM;
    fModifying = modify;
    return MENU_CODE_MODIFY_ACCEPTED;
}

MenuCodeEnum MI_LobbyChip::SendInput(CPlayerInput* playerInput)
{
    if (m_items.empty())
        return MENU_CODE_NONE;
    for (int p = 0; p < 4; p++) {
        const COutputControl& keys = playerInput->outputControls[p];
        if (keys.menu_right.fPressed || keys.menu_down.fPressed) {
            m_index = (m_index + 1) % m_items.size();
            return m_changedCode;
        }
        if (keys.menu_left.fPressed || keys.menu_up.fPressed) {
            m_index = (m_index + m_items.size() - 1) % m_items.size();
            return m_changedCode;
        }
        if (keys.menu_select.fPressed || keys.menu_cancel.fPressed) {
            fModifying = false;
            return MENU_CODE_UNSELECT_ITEM;
        }
    }
    return MENU_CODE_NONE;
}

MenuCodeEnum MI_LobbyChip::MouseClick(short iMouseX, short iMouseY)
{
    if (fDisable)
        return MENU_CODE_NONE;
    if (iMouseX >= m_rect.x && iMouseX < m_rect.x + m_rect.w && iMouseY >= m_rect.y && iMouseY < m_rect.y + m_rect.h && !m_items.empty()) {
        m_index = (m_index + 1) % m_items.size();
        return m_changedCode;
    }
    return MENU_CODE_NONE;
}

void MI_LobbyChip::Draw()
{
    if (!m_visible)
        return;
    const int x = m_rect.x;
    const int y = m_rect.y;
    const int w = m_rect.w;
    const int h = m_rect.h;
    static int blink = 0;
    ++blink;

    Rgb border = {160, 168, 200};
    if (fModifying)
        border = (blink / 8) & 1 ? WHITE : GOLD;
    else if (fSelected)
        border = WHITE;
    panel(x, y, w, h, fDisable ? 120 : 190, border);
    if (fSelected || fModifying)
        outline(x + 2, y + 2, w - 4, h - 4, 1, border);

    const std::string value = m_items.empty() ? std::string() : m_items[m_index].first;
    if (m_icon == Icon::Players)
        drawPlayersIcon(x + 8, y + (h - 16) / 2, m_items.empty() ? 1 : std::clamp<int>(m_items[m_index].second, 1, 2));
    else if (m_icon == Icon::Delay)
        drawClockIcon(x + 8, y + (h - 18) / 2);
    const int labelX = x + (m_icon == Icon::None ? 10 : 34);
    rm->menu_font_small.draw(labelX, y + (h - 15) / 2 + 1, m_label);
    const int valueRight = x + w - 12 - (fModifying ? 12 : 0);
    rm->menu_font_large.drawRightJustified(valueRight, y + (h - 26) / 2 + 1, ellipsize(rm->menu_font_large, value, w - (labelX - x) - 60));
    if (fModifying) {
        const int vw = rm->menu_font_large.getWidth(value);
        triangle(valueRight - vw - 12, y + h / 2, 5, 0, WHITE);
        triangle(valueRight + 6, y + h / 2, 5, 1, WHITE);
    }
    if (fDisable)
        fillRectA(x + 1, y + 1, w - 2, h - 2, BLACK, 70);
}


// ---- MI_LobbyMap

MI_LobbyMap::MI_LobbyMap(const LobbyRect& frame, const LobbyRect& map, LobbyStyle style)
    : MI_MapPreview(&rm->spr_selectfield, frame.x, frame.y, 352, 0)
    , m_frame(frame)
    , m_map(map)
    , m_style(style)
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

    //The frame
    if (m_style == LobbyStyle::Cards) {
        rm->menu_dialog.draw(m_frame.x, m_frame.y, {0, 0, 336, 254});
        rm->menu_dialog.draw(m_frame.x + 336, m_frame.y, {496, 0, 16, 254});
        rm->menu_dialog.draw(m_frame.x, m_frame.y + 254, {0, 464, 336, 16});
        rm->menu_dialog.draw(m_frame.x + 336, m_frame.y + 254, {496, 464, 16, 16});
    } else {
        fillRect(m_frame.x, m_frame.y, m_frame.w, m_frame.h, BLACK);
        fillRect(m_frame.x + 1, m_frame.y + 1, m_frame.w - 2, m_frame.h - 2, {240, 192, 0});
        fillRect(m_frame.x + 2, m_frame.y + 2, m_frame.w - 4, m_frame.h - 4, {248, 248, 0});
        fillRect(m_frame.x + 3, m_frame.y + 3, m_frame.w - 6, m_frame.h - 6, WHITE);
    }

    if (m_available) {
        if (m_map.w == 320 && m_map.h == 240) {
            rectDst = {m_map.x, m_map.y, 320, 240};
            DrawContent();
        } else {
            //Drawn at full size into a scratch surface, then scaled down
            static SdlSurfacePtr scratch;
            if (!scratch)
                scratch.reset(SDL_CreateSurface(320, 240, blitdest->format));
            SDL_Surface* saved = blitdest;
            blitdest = scratch.get();
            SDL_FillSurfaceRect(scratch.get(), nullptr, SDL_MapSurfaceRGB(scratch.get(), 0, 0, 0));
            rectDst = {0, 0, 320, 240};
            DrawContent();
            blitdest = saved;
            const SDL_Rect dst {m_map.x, m_map.y, m_map.w, m_map.h};
            SDL_BlitSurfaceScaled(scratch.get(), nullptr, blitdest, &dst, SDL_SCALEMODE_LINEAR);
        }
    } else {
        fillRect(m_map.x, m_map.y, m_map.w, m_map.h, {44, 46, 54});
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
        int y = m_map.y + m_map.h / 2 - static_cast<int>(lines.size()) * 28 / 2 - 8;
        for (const std::string& line : lines) {
            font.drawCentered(m_map.x + m_map.w / 2, y, line);
            y += 28;
        }
    }

    if (m_fraction >= 0.0f) {
        const int bh = 42;
        const int y = m_map.y + m_map.h - bh;
        fillRectA(m_map.x, y, m_map.w, bh, BLACK, 190);
        rm->menu_font_small.drawCentered(m_map.x + m_map.w / 2, y + 3, ellipsize(rm->menu_font_small, m_label, m_map.w - 12));
        const int barX = m_map.x + 10;
        const int barW = m_map.w - 20;
        fillRect(barX - 2, y + 21, barW + 4, 14, WHITE);
        fillRect(barX, y + 23, barW, 10, {40, 40, 40});
        fillRect(barX, y + 23, static_cast<int>(static_cast<float>(barW) * std::clamp(m_fraction, 0.0f, 1.0f)), 10, TONE_GOOD);
    }
}


// ---- MI_LobbyScene

MI_LobbyScene::MI_LobbyScene(LobbyStyle style)
    : UI_Control(0, 0)
    , m_layout(lobbyLayout(style))
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
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();

    //The header bar, as on every menu of the game
    rm->menu_plain_field.draw(0, 0, {0, 0, 320, 32});
    rm->menu_plain_field.draw(320, 0, {192, 0, 320, 32});
    rm->menu_font_large.drawCentered(320, 5, "Online Lobby");

    if (m_joining) {
        plate(120, 150, 400, 130);
        rm->menu_font_large.drawCentered(320, 168, "Connecting...");
        rm->menu_font_small.drawCentered(320, 206, ellipsize(rm->menu_font_small, m_status, 370));
        rm->menu_font_small.drawCentered(320, 240, "Select Leave to cancel");
        return;
    }

    if (m_layout.style == LobbyStyle::Stage) {
        //The stage behind the columns: a stone colored frame
        fillRect(m_layout.stage.x, m_layout.stage.y, m_layout.stage.w, m_layout.stage.h, BLACK);
        fillRectA(m_layout.stage.x + 1, m_layout.stage.y + 1, m_layout.stage.w - 2, m_layout.stage.h - 2, NAVY, 150);
        outline(m_layout.stage.x + 1, m_layout.stage.y + 1, m_layout.stage.w - 2, m_layout.stage.h - 2, 3, {144, 160, 160});
        panel(m_layout.matchPanel.x, m_layout.matchPanel.y, m_layout.matchPanel.w, m_layout.matchPanel.h);
    }
    drawBanner();
    if (m_layout.style == LobbyStyle::Cards && lobby) {
        //The summary chip: who is here and ready
        const LobbyRect& r = m_layout.chip[2];
        panel(r.x, r.y, r.w, r.h, 140);
        int members = 0;
        int ready = 0;
        for (int s = 0; s < rollback::LOBBY_SLOTS; s++) {
            const rollback::LobbySlot& slot = lobby->slot(s);
            if (slot.kind == rollback::SlotKind::Empty)
                continue;
            members++;
            ready += (slot.kind == rollback::SlotKind::Cpu || slot.peer == 0 || lobby->peer(slot.peer).ready) ? 1 : 0;
        }
        rm->menu_font_small.draw(r.x + 12, r.y + (r.h - 15) / 2 + 1, "Ready");
        rm->menu_font_large.drawRightJustified(r.x + r.w - 12, r.y + (r.h - 26) / 2 + 1, std::format("{} of {}", ready, members));
    }
    drawStatusBar();
}

void MI_LobbyScene::drawBanner() const
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby)
        return;
    const gfxFont& largeFont = rm->menu_font_large;
    const gfxFont& smallFont = rm->menu_font_small;
    const LobbyRect& b = m_layout.banner;

    const std::string& mapName = lobby->mapName();
    const bool minigame = mapName == "(minigame)";
    std::string mode = lobby->modeName();
    if (mode.empty())
        mode = "No mode selected yet";
    std::string goal;
    if (!lobby->goalName().empty() && lobby->goal() > 0)
        goal = std::format("{} {}", lobby->goal(), lobby->goalName());
    std::string map = mapName.empty() ? "No map selected yet" : minigame ? "Minigame" : mapName;

    blueBar(b.x, b.y, b.w);
    const int icon = modeIconIndex(lobby->modeId());
    const int textX = b.x + 14 + (icon >= 0 ? 38 : 0);
    if (icon >= 0)
        rm->menu_mode_large.draw(b.x + 10, b.y, {icon << 5, 0, 32, 32});

    if (m_layout.style == LobbyStyle::Stage) {
        largeFont.drawChopRight(textX, b.y + 3, b.w - (textX - b.x) - 12, ellipsize(largeFont, mode, b.w - (textX - b.x) - 12));
        //The map and the goal on their own rows
        const LobbyRect* rows[2] = {&m_layout.mapRow, &m_layout.goalRow};
        const char* labels[2] = {"Map", "Goal"};
        const std::string values[2] = {map, goal.empty() ? std::string("-") : goal};
        for (int i = 0; i < 2; i++) {
            const LobbyRect& r = *rows[i];
            fillRectA(r.x, r.y, r.w, r.h, BLACK, 90);
            outline(r.x, r.y, r.w, r.h, 1, {70, 76, 110});
            smallFont.draw(r.x + 10, r.y + (r.h - 15) / 2 + 1, labels[i]);
            largeFont.draw(r.x + 62, r.y + (r.h - 26) / 2 + 1, ellipsize(largeFont, values[i], r.w - 74));
        }
    } else {
        //Map name on the left, the mode and the goal in two small lines on the right
        const int rightW = 122;
        largeFont.drawChopRight(textX, b.y + 3, b.w - (textX - b.x) - rightW - 16, ellipsize(largeFont, map, b.w - (textX - b.x) - rightW - 16));
        smallFont.drawRightJustified(b.x + b.w - 12, b.y + 1, ellipsize(smallFont, mode, rightW));
        smallFont.drawRightJustified(b.x + b.w - 12, b.y + 16, ellipsize(smallFont, goal.empty() ? std::string("-") : goal, rightW));
    }
}

void MI_LobbyScene::drawStatusBar() const
{
    const LobbyRect& r = m_layout.status;
    panel(r.x, r.y, r.w, r.h, 175);
    const Rgb tone = m_tone == Tone::Good ? TONE_GOOD : m_tone == Tone::Warn ? TONE_WARN : m_tone == Tone::Error ? TONE_ERROR : TONE_INFO;
    fillCircle(r.x + 16, r.y + r.h / 2, 7, BLACK);
    fillCircle(r.x + 16, r.y + r.h / 2, 5, tone);
    fillCircle(r.x + 15, r.y + r.h / 2 - 2, 2, shade(tone, 55));

    const int textX = r.x + 32;
    const int textW = r.w - 44;
    if (!m_status.empty()) {
        rm->menu_font_large.draw(textX, r.y + 1, ellipsize(rm->menu_font_large, m_status, textW));
        if (!m_hint.empty())
            rm->menu_font_small.draw(textX, r.y + r.h - 17, ellipsize(rm->menu_font_small, m_hint, textW));
    } else if (!m_hint.empty()) {
        rm->menu_font_small.draw(textX, r.y + (r.h - 15) / 2 + 1, ellipsize(rm->menu_font_small, m_hint, textW));
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
