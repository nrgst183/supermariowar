#include "EditorsMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/InputPrompts.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_MenuPanel.h"
#include "ui/MI_Text.h"
#include "ui/MI_TextField.h"
#include "ui/SmwTheme.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

using Action = UI_EditorsMenu::Action;

const std::vector<prompts::Ink> TICK_INKS = {{'#', {0, 0, 0, 255}}, {'w', {255, 255, 255, 255}}};

// A menu pipe (the select field's sprite): gray, light gray, green or light green
enum class PipeLook { Gray = 128, LightGray = 160, Green = 0, LightGreen = 32 };
void pipe(int x, int y, int w, PipeLook look)
{
    const int row = static_cast<int>(look);
    rm->spr_selectfield.draw(x, y, {0, row, w / 2, 32});
    rm->spr_selectfield.draw(x + w / 2, y, {512 - w + w / 2, row, w - w / 2, 32});
}

// The editors' icons (the game's small ones)
void editorIcon(int x, int y, bool level)
{
    theme::icon(x, y, level ? theme::Icon::QuestionBlock : theme::Icon::Star);
}

// ---- a button drawn as a card: an icon, its title and a line under it; or (no line) a choice, green
// with a tick when it is the one chosen
class MI_Card : public MI_Button {
public:
    MI_Card(short x, short y, short w, short h, std::string title, std::string about, std::function<void(int, int)> icon = nullptr,
        std::function<bool()> chosen = nullptr)
        : MI_Button(&rm->spr_selectfield, x, y, title, w, TextAlign::CENTER)
        , m_w(w)
        , m_h(h)
        , m_title(std::move(title))
        , m_about(std::move(about))
        , m_icon(std::move(icon))
        , m_chosen(std::move(chosen))
    {}

    void Draw() override
    {
        if (!m_visible)
            return;
        //A pipe like the menus' buttons: a choice is green (and ticked) when it is the one chosen, gray
        //when not; the selected button is the lighter one
        const bool chosen = m_chosen && m_chosen();
        const bool green = m_chosen ? chosen : fSelected;
        pipe(m_pos.x, m_pos.y, m_w, green ? (fSelected ? PipeLook::LightGreen : PipeLook::Green) : fSelected ? PipeLook::LightGray : PipeLook::Gray);
        const gfxFont& font = rm->menu_font_large;
        if (m_icon) {
            m_icon(m_pos.x + 18, m_pos.y + 8);
            font.draw(m_pos.x + 42, m_pos.y + 5, m_title.c_str());
        } else {
            //(chosen: the title and a tick after it, centered together)
            const int tw = font.getWidth(m_title.c_str()) + (chosen ? 22 : 0);
            font.draw(m_pos.x + (m_w - tw) / 2, m_pos.y + 5, m_title.c_str());
            if (chosen)
                prompts::art(m_pos.x + (m_w + tw) / 2 - 16, m_pos.y + 9,
                    {"......##", ".....#w#", "#...#w#.", "##.#w#..", "#w#w#...", ".#w#....", "..#....."}, TICK_INKS, 2);
        }
    }

    MenuCodeEnum MouseClick(short x, short y) override
    {
        if (fDisable || !m_visible)
            return MENU_CODE_NONE;
        return x >= m_pos.x && x < m_pos.x + m_w && y >= m_pos.y && y < m_pos.y + m_h ? menuCode : MENU_CODE_NONE;
    }

private:
    short m_w;
    short m_h;
    std::string m_title;
    std::string m_about;
    std::function<void(int, int)> m_icon;
    std::function<bool()> m_chosen;
};

// The second page's panel: the editor's name, and its two questions
class MI_SetupPanel : public UI_Control {
public:
    MI_SetupPanel(short x, short w, const short* y, const short* h, const short* whereY, const short* howY, const bool* level,
        const bool* online)
        : UI_Control(x, 0)
        , m_w(w)
        , m_y(y)
        , m_h(h)
        , m_whereY(whereY)
        , m_howY(howY)
        , m_level(level)
        , m_online(online)
    {}

    void Draw() override
    {
        if (!m_visible)
            return;
        const int x = m_pos.x;
        const int y = *m_y;
        menupanel::box(x, y, m_w, *m_h);
        //The editor: its icon and name, a line under them
        editorIcon(x + 24, y + 17, *m_level);
        rm->menu_font_large.draw(x + 48, y + 15, *m_level ? "Map Editor" : "World Editor");
        rm->menu_font_small.drawRightJustified(x + m_w - 24, y + 19, *m_level ? "The levels you battle in" : "Overworlds of levels to play through");
        prompts::fill(x + 18, y + 44, m_w - 36, 2, {248, 248, 248, 255});
        prompts::fill(x + 18, y + 46, m_w - 36, 1, {0, 40, 104, 255});
        //(the rows' names, level with their buttons)
        rm->menu_font_large.draw(x + 32, *m_whereY + 5, "Where");
        if (*m_online)
            rm->menu_font_large.draw(x + 32, *m_howY + 5, "How");
    }

private:
    short m_w;
    const short* m_y;
    const short* m_h;
    const short* m_whereY;
    const short* m_howY;
    const bool* m_level;
    const bool* m_online;
};

} // namespace

UI_EditorsMenu::UI_EditorsMenu()
    : UI_Menu()
{
    AddNonControl(newTitleBarBack());
    AddNonControl(new MI_HeaderText("Editors", 320, 5));
    SetHints(false);    //(its own prompts, at the bottom)

    auto want = [this](Action a) {
        return [this, a] { Want(a); };
    };

    //The first page: maps or worlds
    miPickBox = new MI_MenuPanel(100, 112, 440, 214);
    AddNonControl(miPickBox);
    miPickText = new MI_Text("What do you want to make?", 320, 128, 0, true, TextAlign::CENTER);
    AddNonControl(miPickText);
    miMaps = new MI_Card(120, 166, 400, 32, "Maps", "", [](int x, int y) { editorIcon(x, y, true); });
    miMapsText = new MI_Text("The levels you battle in", 320, 202, 0, false, TextAlign::CENTER);
    AddNonControl(miMapsText);
    miWorlds = new MI_Card(120, 238, 400, 32, "Worlds", "", [](int x, int y) { editorIcon(x, y, false); });
    miWorldsText = new MI_Text("Overworlds of levels to play through", 320, 274, 0, false, TextAlign::CENTER);
    AddNonControl(miWorldsText);

    //The second page: where (here or online), how (host or join), then start
    miSetupPanel = new MI_SetupPanel(36, 568, &m_panelY, &m_panelH, &m_whereY, &m_howY, &m_level, &m_online);
    AddNonControl(miSetupPanel);
    miLocal = new MI_Card(168, 100, 196, 32, "This computer", "", nullptr, [this] { return !m_online; });
    miOnline = new MI_Card(372, 100, 196, 32, "Online", "", nullptr, [this] { return m_online; });
    miWhereText = new MI_Text("", 368, 140, 0, false, TextAlign::CENTER);
    AddNonControl(miWhereText);
    miHost = new MI_Card(168, 188, 196, 32, "Host", "", nullptr, [this] { return !m_joining; });
    miJoin = new MI_Card(372, 188, 196, 32, "Join", "", nullptr, [this] { return m_joining; });
    miHowText = new MI_Text("", 368, 228, 0, false, TextAlign::CENTER);
    AddNonControl(miHowText);
    //(the online menu's address, without its port: the editors have their own)
    m_address = game_values.onlineaddress.substr(0, game_values.onlineaddress.find(':'));
    miAddressField = new MI_TextField(&rm->spr_selectfield, 168, 254, "Address", 400, 120);
    miAddressField->SetData(m_address, 40);
    miStatusText = new MI_Text("", 320, 296, 0, false, TextAlign::CENTER);
    AddNonControl(miStatusText);
    miStart = new MI_Button(&rm->spr_selectfield, 170, 322, "Start", 300, TextAlign::CENTER);

    //Both pages: how the editors work, and back
    AddNonControl(new MI_PromptStrip(16, 434, 512, "In the editors:", [] {
        const bool controller = prompts::lastDevice() == prompts::Device::Controller;
        return controller ? std::vector<prompts::Prompt> {{{prompts::Pad::Start}, {}, "tools"}, {{prompts::Pad::A}, {}, "place"},
                                {{prompts::Pad::Back}, {}, "leave"}}
                          : std::vector<prompts::Prompt> {{{}, {"Tab"}, "tools"}, {{}, {"F1"}, "help"}, {{}, {"Esc"}, "leave"}};
    }));
    const std::pair<MI_Button*, Action> buttons[] = {{miMaps, Action::Maps}, {miWorlds, Action::Worlds}, {miLocal, Action::Local},
        {miOnline, Action::Online}, {miHost, Action::Host}, {miJoin, Action::Join}, {miStart, Action::Start}};
    for (const auto& [button, action] : buttons) {
        button->SetCode(MENU_CODE_EDITORS_ACTION);
        button->SetOnPress(want(action));
        AddControl(button, nullptr, nullptr, nullptr, nullptr);
    }
    AddControl(miAddressField, nullptr, nullptr, nullptr, nullptr);
    //(Escape and B: Back, see Act)
    SetCancelCode(MENU_CODE_EDITORS_ACTION);
    ShowPage(false);
}

void UI_EditorsMenu::Open()
{
    m_pending = Action::None;
    ShowPage(false);
}

void UI_EditorsMenu::ShowPage(bool setup)
{
    m_setup = setup;
    Relink();
    UI_Control* first = setup ? static_cast<UI_Control*>(m_online ? miOnline : miLocal) : (m_level ? miMaps : miWorlds);
    if (setup && !m_online)
        first = miStart;
    setInitialFocus(first);
}

// What shows on the page, and where the d-pad goes from each button
void UI_EditorsMenu::Relink()
{
    const bool setup = m_setup;
    miPickText->setVisible(!setup);
    miPickBox->setVisible(!setup);
    miMapsText->setVisible(!setup);
    miWorldsText->setVisible(!setup);
    miMaps->setVisible(!setup);
    miWorlds->setVisible(!setup);
    miSetupPanel->setVisible(setup);
    miLocal->setVisible(setup);
    miOnline->setVisible(setup);
    miHost->setVisible(setup && m_online);
    miJoin->setVisible(setup && m_online);
    miAddressField->setVisible(false);    //(joining is from the games list, which has Join by address)
    miStatusText->setVisible(setup);
    miWhereText->setVisible(setup);
    miWhereText->SetText(m_online ? "Edit together with friends over the network" : "Everyone with a controller gets a cursor");
    miHowText->setVisible(setup && m_online);
    miHowText->SetText(m_joining ? "Find the game in the games list" : "A lobby friends join from Join a Game");
    miStart->setVisible(setup);
    miStart->SetName(m_online && m_joining ? "Join" : m_online ? "Host" : "Start");
    //(Start under the last row; the status under it, and the panel around them)
    //The rows from the panel's top (then the panel centered between the header and the tips)
    int y = 60;
    const int where = y;
    y += 32 + 8;
    const int whereText = y;
    y += 26;
    int how = 0, howText = 0, address = 0;
    if (m_online) {
        how = y + 6;
        y = how + 32 + 8;
        howText = y;
        y += 26;
    }
    const int start = y + 6;
    const int status = start + 40;
    const int height = status + 26;
    const int top = 40 + (390 - height) / 2;
    m_panelY = static_cast<short>(top);
    m_panelH = static_cast<short>(height);
    m_whereY = static_cast<short>(top + where);
    m_howY = static_cast<short>(top + how);
    miLocal->SetPosition(168, m_whereY);
    miOnline->SetPosition(372, m_whereY);
    miWhereText->SetPosition(368, static_cast<short>(top + whereText));
    miHost->SetPosition(168, m_howY);
    miJoin->SetPosition(372, m_howY);
    miHowText->SetPosition(368, static_cast<short>(top + howText));
    miAddressField->SetPosition(168, static_cast<short>(top + address));
    miStart->SetPosition(170, static_cast<short>(top + start));
    miStatusText->SetPosition(320, static_cast<short>(top + status));

    //The rows, top to bottom (the d-pad goes around)
    std::vector<std::vector<UI_Control*>> rows;
    if (!setup) {
        rows = {{miMaps}, {miWorlds}};    //(one above the other: up and down)
    } else {
        rows.push_back({miLocal, miOnline});
        if (m_online)
            rows.push_back({miHost, miJoin});
        rows.push_back({miStart});
    }
    const size_t n = rows.size();
    for (size_t r = 0; r < n; r++) {
        const std::vector<UI_Control*>& row = rows[r];
        for (size_t c = 0; c < row.size(); c++) {
            const std::vector<UI_Control*>& above = rows[(r + n - 1) % n];
            const std::vector<UI_Control*>& below = rows[(r + 1) % n];
            row[c]->setNeighbor(MenuNavDirection::Up, above[std::min(c, above.size() - 1)]);
            row[c]->setNeighbor(MenuNavDirection::Down, below[std::min(c, below.size() - 1)]);
            row[c]->setNeighbor(MenuNavDirection::Left, row.size() > 1 ? row[(c + row.size() - 1) % row.size()] : nullptr);
            row[c]->setNeighbor(MenuNavDirection::Right, row.size() > 1 ? row[(c + 1) % row.size()] : nullptr);
        }
    }
}

MenuCodeEnum UI_EditorsMenu::Act()
{
    const Action action = m_pending;
    m_pending = Action::None;
    switch (action) {
        case Action::Maps:
        case Action::Worlds:
            m_level = action == Action::Maps;
            ShowPage(true);
            return MENU_CODE_NONE;
        case Action::Local:
        case Action::Online:
            m_online = action == Action::Online;
            Relink();
            return MENU_CODE_NONE;
        case Action::Host:
        case Action::Join:
            m_joining = action == Action::Join;
            Relink();
            return MENU_CODE_NONE;
        case Action::Start:
            if (!m_online)
                return m_level ? MENU_CODE_RUN_LEVEL_EDITOR : MENU_CODE_RUN_WORLD_EDITOR;
            if (!m_joining)
                return m_level ? MENU_CODE_HOST_LEVEL_EDITOR : MENU_CODE_HOST_WORLD_EDITOR;
            return MENU_CODE_JOIN_EDITOR;
        case Action::None:
        default:
            return Back();
    }
}

MenuCodeEnum UI_EditorsMenu::Back()
{
    if (m_setup) {
        ShowPage(false);
        return MENU_CODE_NONE;
    }
    return MENU_CODE_EDITORS_EXIT;
}

void UI_EditorsMenu::SetStatus(const std::string& text)
{
    miStatusText->SetText(text);
}
