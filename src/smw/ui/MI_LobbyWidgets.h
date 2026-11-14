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
// (rollback::NetGame::instance().lobby()); they read it every frame. They are drawn only from the
// art of the original menus (pipe fields and buttons, the orange player cards and Ready mushrooms of the
// team select screen, the cream map frame, the menu fonts), laid out like the Single Game menu: the
// mode, goal and map fields and the map preview on the left, the four players on the right.

struct LobbyRect {
    short x = 0;
    short y = 0;
    short w = 0;
    short h = 0;
};

struct LobbyLayout {
    LobbyRect field[3];      // mode, goal, map (read-only pipe fields)
    short fieldIndent = 0;   // where the value starts in a field
    LobbyRect mapFrame;      // the cream frame of the map picture
    LobbyRect map;           // the map picture inside it
    LobbyRect card[4];       // the four player cards
    LobbyRect players;       // the "Players" select field
    LobbyRect delay;         // the "Delay" select field
    LobbyRect status;        // the status bar
    LobbyRect hint;          // the hint under the fields
    short buttonY = 0;       // the buttons: y, height 32 and the x of the three columns
    short buttonX[3] {};
    short buttonW = 0;       // width of each of the three (host)
    short joinerW = 0;       // width of the two of a joiner
};

LobbyLayout lobbyLayout();


/// A button of the lobby: the pipes of the original menus, green for the main action (ready, start),
/// gray for the others, whatever their width
class MI_LobbyButton : public MI_Button {
public:
    MI_LobbyButton(gfxSprite* sprite, short x, short y, std::string name, short width, bool primary)
        : MI_Button(sprite, x, y, std::move(name), width, TextAlign::CENTER)
    {
        iAdjustmentY = primary ? 0 : 128;
    }
};


/// One of the four players of the next match, drawn like the player cards at the bottom of the team select
/// screen: the boxed number, the skin sprite in the player's color, the name, and the Ready mushroom.
/// Left/right change the team and up/down the color of the player's own card (the host's: of any card)
/// while it is being edited; "select" starts and ends editing. The host's "select" on an empty card adds
/// a CPU player; while editing, "random" kicks the player or removes the CPU player.
class MI_LobbyCard : public UI_Control {
public:
    MI_LobbyCard(const LobbyRect& rect, int slot);

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
    void drawCard(const Info& info);

    short m_width = 0;
    short m_height = 0;
    int m_slot = 0;

    int m_overrideTeam = 0;       // what the player has just chosen, until the lobby's state says so too
    int m_overrideColor = 0;
    int m_overrideFrames = 0;

    int m_tick = 0;
    SpriteStrip m_skin;
    std::string m_skinPath;
    short m_skinColor = -1;
};


/// A read-only field in the style of the Single Game menu's pipe fields: a label, then the value
/// (optionally after a small mode icon)
class MI_LobbyField : public UI_Control {
public:
    MI_LobbyField(const LobbyRect& rect, short indent, std::string label);

    void SetLabel(std::string label) { m_label = std::move(label); }
    void SetValue(std::string value, int modeIcon = -1);
    void Draw() override;

private:
    LobbyRect m_rect;
    short m_indent = 0;
    std::string m_label;
    std::string m_value;
    int m_icon = -1;
};


/// The map picture of the lobby: the map's preview in the cream frame of the match menus. While the
/// map is not here, a text covers it, and a transfer shows its progress with the slider bar.
class MI_LobbyMap : public MI_MapPreview {
public:
    MI_LobbyMap(const LobbyRect& frame, const LobbyRect& map);

    void Draw() override;
    void SetText(std::string text);
    void SetProgress(float fraction, std::string label);
    void SetAvailable(bool available) { m_available = available; }

private:
    LobbyRect m_frame;
    LobbyRect m_map;
    bool m_available = false;
    std::string m_text;
    std::string m_label;
    float m_fraction = -1.0f;
};


/// The status bar (the blue bar of the headers), the hint and the "connecting" screen
class MI_LobbyScene : public UI_Control {
public:
    enum class Tone { Info, Good, Warn, Error };

    MI_LobbyScene();

    void Draw() override;

    /// The status line, and the hint under the settings (the tone is not shown any more)
    void SetStatus(std::string text, Tone tone, std::string hint);
    void SetJoining(bool joining) { m_joining = joining; }

private:
    LobbyLayout m_layout;
    std::string m_status;
    std::string m_hint;
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
