#pragma once

#include "uicontrol.h"
#include "ResourceManager.h"

#include <string>

class gfxSprite;


// The controls of the online lobby screen that show the state of the lobby
// (rollback::NetGame::instance().lobby()); they read it every frame.


/// One of the four players of the next match, drawn like the team select screen: the skin sprite
/// on a block in the color of its team, the nickname, the ping and whether the player is ready
/// (the "Ready" mushroom of the team select, or the dark one while not).
///
/// A player edits its own card (the host any card): "select" starts editing, left/right change the
/// team, up/down the color, "select" or "cancel" ends it. The host's "select" on an empty card adds a
/// CPU player; while editing, the "random" button kicks the player or removes the CPU player.
class MI_LobbyCard : public UI_Control {
public:
    MI_LobbyCard(short x, short y, short width, short height, int slot);

    void Update() override;
    void Draw() override;
    MenuCodeEnum Modify(bool modify) override;
    MenuCodeEnum SendInput(CPlayerInput* playerInput) override;
    MenuCodeEnum MouseClick(short iMouseX, short iMouseY) override;

    int slot() const { return m_slot; }
    /// What the player can do with this card now, for the hint line
    std::string hint() const;

private:
    bool editable() const;

    short m_width = 0;
    short m_height = 0;
    int m_slot = 0;

    int m_overrideTeam = 0;       // what the player has just chosen, until the lobby's state says so too
    int m_overrideColor = 0;
    int m_overrideFrames = 0;

    int m_animTimer = 0;
    int m_animFrame = 0;
    SpriteStrip m_skin;
    short m_skinId = -1;
    short m_skinColor = -1;
};


/// Drawn over the map preview (its frame is at x, y): with a text it covers the preview (the map is
/// not available), and a fraction >= 0 draws a progress bar over the bottom of the preview
class MI_MapBox : public UI_Control {
public:
    MI_MapBox(short x, short y);

    void SetText(std::string text);
    void SetProgress(float fraction, std::string label);
    void Draw() override;

private:
    std::string m_text;
    std::string m_label;
    float m_fraction = -1.0f;
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
