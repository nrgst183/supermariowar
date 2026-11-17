#pragma once

#include "uicontrol.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_MapPreview.h"
#include "ui/InputPrompts.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class gfxSprite;
class MI_Image;


// The controls of the online lobby screens that show the state of the lobby
// (rollback::NetGame::instance().lobby()); they read it every frame. They are drawn only from the
// art of the original menus: the Team and Character Selection screen (its team box, the orange player
// cards and Ready mushrooms), the pipe fields and buttons, the cream map frame and the menu fonts.

struct LobbyRect {
    short x = 0;
    short y = 0;
    short w = 0;
    short h = 0;
};

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


/// The Team and Character Selection screen of the online lobby, like MI_TeamSelect: the four teams as
/// columns of the players in them, and a card for each player at the bottom (number, name, Ready mushroom,
/// and the role and ping in the small font). Each player controls only its own character on this
/// computer: up/down choose the skin, left/right the team and select is ready (the original's keys); the
/// others (remote players, CPU players) are shown, locked. While a player is ready (locked), "down" moves
/// to the buttons under the box and "cancel" takes the ready back; cancel when not ready leaves the lobby.
class MI_OnlineTeamSelect : public UI_Control {
public:
    MI_OnlineTeamSelect(short x, short y);

    void Update() override;
    void Draw() override;
    MenuCodeEnum SendInput(CPlayerInput* playerInput) override;
    MenuCodeEnum Modify(bool modify) override;

    /// The screen was opened: nobody is locked
    void Reset();
    /// Whether this computer's players are ready (the host's lock is only a feature of the screen)
    bool locked() const;
    void Lock(bool lock);
    /// Called when the focus should go to the buttons under the box
    void SetLeaveDown(std::function<void()> leave) { m_leaveDown = std::move(leave); }
    /// What the local players can do now, for the hint line
    std::string hint() const;
    /// And the buttons that do it, for the prompts row
    std::vector<prompts::Prompt> prompts() const;

private:
    struct Info;
    bool gather(int slot, Info& info) const;
    int mySlot(int localIndex) const;
    int teamOf(int slot) const;
    void changeTeam(int slot, int direction);
    void changeSkin(int localIndex, int direction, bool random);
    const gfxSprite* skinFrame(int slot, const Info& info, int frame);

    std::unique_ptr<MI_Image> m_background;
    std::function<void()> m_leaveDown;
    bool m_hostLocked = false;
    int m_tick = 0;

    int m_overrideTeam[4] {};
    int m_overrideFrames[4] {};
    int m_holdFrames[4] {};     // how long up/down are held, for the fast scroll of the skins

    struct Skin {
        SpriteStrip strip;
        std::string path;
        short color = -1;
    };
    Skin m_skins[4];
};


/// A small marker for something only the host can change ("Host", "Set by host")
class MI_LockTag : public UI_Control {
public:
    MI_LockTag(short x, short y, std::string text, bool rightJustified = true)
        : UI_Control(x, y)
        , m_text(std::move(text))
        , m_right(rightJustified)
    {}

    void SetText(std::string text) { m_text = std::move(text); }
    void Draw() override;

private:
    std::string m_text;
    bool m_right = true;
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


/// The header and the status bar (the blue bar of the headers) of the lobby screens, the hint line,
/// and the "connecting" screen
class MI_LobbyScene : public UI_Control {
public:
    enum class Tone { Info, Good, Warn, Error };

    MI_LobbyScene();

    void Draw() override;

    /// The header, the status line, and the hint under the buttons
    void SetTitle(std::string title) { m_title = std::move(title); }
    void SetStatus(std::string text, Tone tone, std::string hint);
    void SetJoining(bool joining)
    {
        if (joining && !m_joining)
            m_joinTicks = 0;
        m_joining = joining;
    }
    /// The line "Settings: host only" at the bottom right of the header bar
    void SetSettingsNote(std::string note) { m_note = std::move(note); }
    /// The buttons row (y 352): the controller's buttons or the keys of what can be done
    void SetPrompts(std::vector<prompts::Prompt> row) { m_prompts = std::move(row); }

private:
    LobbyRect m_statusRect;
    LobbyRect m_hintRect;
    std::string m_title;
    std::string m_status;
    std::string m_hint;
    std::string m_note;
    Tone m_tone = Tone::Info;
    std::vector<prompts::Prompt> m_prompts;
    bool m_joining = false;
    int m_tick = 0;
    int m_joinTicks = 0;    //(frames since the "connecting" screen came up)
};


/// A row of button prompts (centered on x): the controller's buttons or the keys, whichever was used last
class MI_PromptRow : public UI_Control {
public:
    MI_PromptRow(short x, short y)
        : UI_Control(x, y)
    {}

    // (asked every frame: the keys can change in the Controls)
    void SetPrompts(std::function<std::vector<prompts::Prompt>()> row) { m_row = std::move(row); }
    void Draw() override;

private:
    std::function<std::vector<prompts::Prompt>()> m_row;
};


/// Word-wrapped text, centered on x, at most `maxLines` lines of the large menu font
class MI_WrappedText : public UI_Control {
public:
    enum class Tone { Info, Good, Warn, Error };    //(the status icons of SmwTheme.h)

    MI_WrappedText(short x, short y, short width, int maxLines);

    void SetText(std::string text, Tone tone = Tone::Info);
    const std::string& text() const { return m_text; }
    /// Drawn in a panel (the menus' blue), with the tone's icon at its left
    void SetBoxed(bool boxed) { m_boxed = boxed; }
    void Draw() override;

private:
    std::string m_text;
    short m_width = 0;
    int m_maxLines = 1;
    Tone m_tone = Tone::Info;
    bool m_boxed = false;
};


/// A horizontal progress bar
class MI_ProgressBar : public UI_Control {
public:
    MI_ProgressBar(short x, short y, short width, short height);

    void Draw() override;

private:
    short m_width = 0;
    short m_height = 0;
    float m_fraction = 0.0f;
};
