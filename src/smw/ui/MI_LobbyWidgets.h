#pragma once

#include "uicontrol.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_MapPreview.h"

#include <string>
#include <utility>
#include <vector>

class gfxSprite;


// The controls of the online lobby screen that show the state of the lobby
// (rollback::NetGame::instance().lobby()); they read it every frame.
//
// The lobby has two layouts (LobbyStyle), both for 640x480 and built from the original menus' pieces
// (the blue header bar, the pipe buttons, the team colors, the orange framed name plates):
//   Stage  "Team stage": the four players stand big on team colored pedestals across the width, like on
//          the team select screen; below them the map thumbnail with a banner of the mode, map and goal.
//   Cards  "Polished cards": the map on the left under a banner, a column of four player cards on the right.
// The layout of each (where every control goes) is lobbyLayout().

enum class LobbyStyle : int { Stage = 0, Cards = 1 };
constexpr int LOBBY_STYLE_COUNT = 2;

struct LobbyRect {
    short x = 0;
    short y = 0;
    short w = 0;
    short h = 0;
};

struct LobbyLayout {
    LobbyStyle style = LobbyStyle::Stage;
    LobbyRect stage;         // the panel behind the player columns (Stage)
    LobbyRect card[4];       // the four player cards
    LobbyRect matchPanel;    // the panel of the match (Stage)
    LobbyRect mapFrame;      // the frame of the map picture
    LobbyRect map;           // the map picture (the preview, scaled)
    LobbyRect banner;        // mode, map and goal
    LobbyRect mapRow;        // Stage: the map name row below the banner
    LobbyRect goalRow;       // Stage: the goal row
    LobbyRect chip[3];       // local players, input delay, (Cards: the summary)
    LobbyRect status;        // the status and hint bar
    short buttonY = 0;       // the buttons: y, height 32 and the x of the three columns
    short buttonX[3] {};
    short buttonW = 0;       // width of each of the three (host)
    short joinerW = 0;       // width of the two of a joiner
};

LobbyLayout lobbyLayout(LobbyStyle style);


/// A button of the lobby: the pipes of the original menus, green for the main action (ready, start),
/// gray for the others, whatever their width
class MI_LobbyButton : public MI_Button {
public:
    MI_LobbyButton(gfxSprite* sprite, short x, short y, std::string name, short width, bool primary)
        : MI_Button(sprite, x, y, std::move(name), width, TextAlign::CENTER)
    {
        iAdjustmentY = primary ? 0 : 128;
    }

    /// Drawn darker (a button that can't be used now)
    void SetDimmed(bool dimmed) { m_dimmed = dimmed; }
    void Draw() override;

private:
    bool m_dimmed = false;
};


/// One of the four players of the next match. Left/right change the team and up/down the color of
/// the player's own card (the host's: of any card) while it is being edited; "select" starts and
/// ends editing. The host's "select" on an empty card adds a CPU player; while editing, "random"
/// kicks the player or removes the CPU player.
///
/// Stage: a big character standing on a pedestal in the color of the team, name and ping below; a
/// ready player jumps and gets a check mark. Cards: a portrait on the team color, the name, the
/// ping bars and one status badge (ready check, or waiting).
class MI_LobbyCard : public UI_Control {
public:
    MI_LobbyCard(const LobbyRect& rect, int slot, LobbyStyle style);

    void Update() override;
    void Draw() override;
    MenuCodeEnum Modify(bool modify) override;
    MenuCodeEnum SendInput(CPlayerInput* playerInput) override;
    MenuCodeEnum MouseClick(short iMouseX, short iMouseY) override;

    int slot() const { return m_slot; }
    /// What the player can do with this card now, for the hint line
    std::string hint() const;

private:
    struct Info;
    bool editable() const;
    bool gather(Info& info) const;
    const gfxSprite* skinFrame(const Info& info, int frame);
    void drawStage(const Info& info);
    void drawCard(const Info& info);

    short m_width = 0;
    short m_height = 0;
    int m_slot = 0;
    LobbyStyle m_style = LobbyStyle::Stage;

    int m_overrideTeam = 0;       // what the player has just chosen, until the lobby's state says so too
    int m_overrideColor = 0;
    int m_overrideFrames = 0;

    int m_tick = 0;
    SpriteStrip m_skin;
    std::string m_skinPath;
    short m_skinColor = -1;
};


/// A compact setting: an icon, a label and the value (a second line of text when modifying shows the
/// arrows). "select" starts editing, left/right change the value, "select" ends it, like a select field.
class MI_LobbyChip : public UI_Control {
public:
    enum class Icon { None, Players, Delay };

    MI_LobbyChip(const LobbyRect& rect, std::string label, Icon icon);

    void Draw() override;
    MenuCodeEnum Modify(bool modify) override;
    MenuCodeEnum SendInput(CPlayerInput* playerInput) override;
    MenuCodeEnum MouseClick(short iMouseX, short iMouseY) override;

    void add(std::string label, short value);
    void clear();
    bool setCurrentIndex(size_t index);
    bool setCurrentValue(short value);
    short currentValue() const { return m_items.empty() ? 0 : m_items[m_index].second; }
    void setItemChangedCode(MenuCodeEnum code) { m_changedCode = code; }

private:
    LobbyRect m_rect;
    std::string m_label;
    Icon m_icon = Icon::None;
    MenuCodeEnum m_changedCode = MENU_CODE_NONE;
    std::vector<std::pair<std::string, short>> m_items;
    size_t m_index = 0;
};


/// The map picture of the lobby: the map's preview scaled into a rectangle (with a frame of the
/// original menus). While the map is not here, a text covers it and a transfer shows its progress.
class MI_LobbyMap : public MI_MapPreview {
public:
    MI_LobbyMap(const LobbyRect& frame, const LobbyRect& map, LobbyStyle style);

    void Draw() override;
    void SetText(std::string text);
    void SetProgress(float fraction, std::string label);
    void SetAvailable(bool available) { m_available = available; }

private:
    LobbyRect m_frame;
    LobbyRect m_map;
    LobbyStyle m_style = LobbyStyle::Stage;
    bool m_available = false;
    std::string m_text;
    std::string m_label;
    float m_fraction = -1.0f;
};


/// Everything that is only drawn: the header bar, the panels behind the cards, the banner of the mode,
/// map and goal, the status bar, and the "connecting" screen.
class MI_LobbyScene : public UI_Control {
public:
    enum class Tone { Info, Good, Warn, Error };

    explicit MI_LobbyScene(LobbyStyle style);

    void Draw() override;

    /// The status line, its tone, and the hint under it
    void SetStatus(std::string text, Tone tone, std::string hint);
    void SetJoining(bool joining) { m_joining = joining; }

private:
    void drawBanner() const;
    void drawStatusBar() const;

    LobbyLayout m_layout;
    std::string m_status;
    std::string m_hint;
    Tone m_tone = Tone::Info;
    bool m_joining = false;
};


/// Word-wrapped text, centered on x, at most `maxLines` lines of the large menu font
class MI_WrappedText : public UI_Control {
public:
    MI_WrappedText(short x, short y, short width, int maxLines);

    void SetText(std::string text);
    const std::string& text() const { return m_text; }
    void Draw() override;

private:
    std::string m_text;
    short m_width = 0;
    int m_maxLines = 1;
};


/// A horizontal progress bar
class MI_ProgressBar : public UI_Control {
public:
    MI_ProgressBar(short x, short y, short width, short height);

    void SetFraction(float fraction);
    void Draw() override;

private:
    short m_width = 0;
    short m_height = 0;
    float m_fraction = 0.0f;
};
