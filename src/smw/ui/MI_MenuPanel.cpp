#include "MI_MenuPanel.h"

#include "ResourceManager.h"
#include "ui/SmwTheme.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <utility>

namespace menupanel {
namespace {
// Rounded boxes, one in another, each drawn over the one before: the colors that end up showing,
// filled once each (rows alike merged into one rectangle), not a box over a box a row at a time
struct Layer {
    int inset;      // from the outer edge
    int radius;
    SDL_Color color;
};

// How far a rounded box's row is in from its sides
int roundInset(int row, int h, int r)
{
    const int d = row < r ? r - row : (row >= h - r ? row - (h - r - 1) : 0);
    if (d <= 0)
        return 0;
    return r - static_cast<int>(std::sqrt(static_cast<float>(r * r) - (d - 0.5f) * (d - 0.5f)) + 0.5f);
}

void layered(int x, int y, int w, int h, std::initializer_list<Layer> layers)
{
    struct Run {
        int x0, x1;
        SDL_Color color;
        bool operator==(const Run& o) const
        {
            return x0 == o.x0 && x1 == o.x1 && color.r == o.color.r && color.g == o.color.g && color.b == o.color.b;
        }
    };
    constexpr int MAX_RUNS = 8;
    Run previous[MAX_RUNS];
    int previousCount = -1;
    int previousFrom = 0;
    const auto flush = [&](int toRow) {
        for (int i = 0; i < previousCount; i++)
            prompts::fill(previous[i].x0, y + previousFrom, previous[i].x1 - previous[i].x0, toRow - previousFrom, previous[i].color);
    };

    for (int row = 0; row < h; row++) {
        //Each box's span on this row (the later over the earlier)
        int from[4], to[4];
        int count = 0;
        for (const Layer& layer : layers) {
            if (count == 4)
                break;    //(four at most)
            const int lh = h - 2 * layer.inset;
            const int lrow = row - layer.inset;
            from[count] = to[count] = 0;
            if (lrow >= 0 && lrow < lh) {
                const int in = roundInset(lrow, lh, layer.radius);
                from[count] = x + layer.inset + in;
                to[count] = x + w - layer.inset - in;
            }
            ++count;
        }
        //The row's pieces: from edge to edge of the spans, each the color of the last box over it
        int edges[8];
        int edgeCount = 0;
        for (int i = 0; i < count; i++) {
            if (to[i] > from[i]) {
                edges[edgeCount++] = from[i];
                edges[edgeCount++] = to[i];
            }
        }
        //(a few: in order by insertion)
        for (int e = 1; e < edgeCount; e++) {
            for (int f = e; f > 0 && edges[f - 1] > edges[f]; f--)
                std::swap(edges[f - 1], edges[f]);
        }
        Run runs[MAX_RUNS];
        int runCount = 0;
        for (int e = 0; e + 1 < edgeCount; e++) {
            const int x0 = edges[e], x1 = edges[e + 1];
            if (x1 <= x0)
                continue;
            const SDL_Color* color = nullptr;
            int i = 0;
            for (const Layer& layer : layers) {
                if (i == count)
                    break;
                if (from[i] <= x0 && x1 <= to[i])
                    color = &layer.color;
                ++i;
            }
            if (!color)
                continue;
            if (runCount > 0 && runs[runCount - 1].x1 == x0 && runs[runCount - 1].color.r == color->r && runs[runCount - 1].color.g == color->g
                && runs[runCount - 1].color.b == color->b)
                runs[runCount - 1].x1 = x1;
            else if (runCount < MAX_RUNS)
                runs[runCount++] = {x0, x1, *color};
        }
        //(alike the row before: one rectangle on)
        bool same = runCount == previousCount;
        for (int i = 0; same && i < runCount; i++)
            same = runs[i] == previous[i];
        if (!same) {
            flush(row);
            std::copy(runs, runs + runCount, previous);
            previousCount = runCount;
            previousFrom = row;
        }
    }
    flush(h);
}
} // namespace

void box(int x, int y, int w, int h)
{
    //(Super Mario All-Stars' SMB3 status bar, with a solid shadow as its dialogs have)
    theme::statusFrame(x, y, w - 6, h - 6, 6);
}

void row(int x, int y, int w, int h, bool selected, bool dim)
{
    //(SMB3's status bar strips: the one chosen lit, a white line round it as the title bar's; the others
    //sunk into the panel, a dark line round them and a lighter one along their top)
    if (selected && !dim) {
        theme::statusBar(x, y, w, h);
        return;
    }
    const SDL_Color body = dim ? SDL_Color {56, 64, 88, 255} : SDL_Color {0, 64, 136, 255};
    //(2-pixel lines: the game's doubled pixels, no flicker on an interlaced TV picture)
    layered(x, y, w, h, {{0, 5, selected ? SDL_Color {248, 248, 248, 255} : SDL_Color {0, 24, 64, 255}}, {2, 4, body}});
    if (!selected)
        prompts::fill(x + 5, y + 2, w - 10, 2, dim ? SDL_Color {80, 88, 112, 255} : SDL_Color {24, 96, 176, 255});
}

void bestText(int x, int y, int w, const std::string& text)
{
    //(the large font a line higher, its letters on the small one's line)
    if (rm->menu_font_large.getWidth(text) <= w)
        rm->menu_font_large.draw(x, y - 6, text);
    else
        rm->menu_font_small.drawScrolling(x, y, w, text);
}

namespace {
bool s_footerDrawn = false;
SDL_Rect s_footerBack {0, 0, 0, 0};    // the last footer's B prompt (w 0: none)
}

void beginFrame()
{
    s_footerDrawn = false;
}

bool footerDrawn()
{
    return s_footerDrawn;
}

bool footerBackAt(short x, short y)
{
    const SDL_Rect& r = s_footerBack;
    return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

std::vector<prompts::Prompt> standardPrompts(bool change, const char* back)
{
    using prompts::Pad;
    using prompts::menuKey;
    std::vector<prompts::Prompt> row = {{{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"}};
    if (change)
        row.push_back({{Pad::Left, Pad::Right}, {menuKey(prompts::MENU_LEFT), menuKey(prompts::MENU_RIGHT)}, "change"});
    row.push_back({{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "select"});
    if (back)
        row.push_back({{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, back});
    return row;
}

void dialogBox(int x, int y, int w, int h)
{
    box(x, y, w, h);
}

void dialogChoice(int cx, int y, const char* left, const char* right, bool leftChosen)
{
    constexpr int W = 84;
    constexpr int H = 30;
    theme::block(cx - W - 6, y, W, H, leftChosen ? theme::Tone::Gold : theme::Tone::Gray);
    theme::block(cx + 6, y, W, H, leftChosen ? theme::Tone::Gray : theme::Tone::Gold);
    rm->menu_font_large.drawCentered(cx - W / 2 - 6, y + 5, left);
    rm->menu_font_large.drawCentered(cx + 6 + W / 2, y + 5, right);
}

void footer(const std::vector<prompts::Prompt>& row, const std::string& label, int icon)
{
    s_footerDrawn = true;
    s_footerBack = {0, 0, 0, 0};
    //The status bar's strip along the bottom of the screen (as the title bar's at the top)
    theme::statusBar(0, 448, 640, 32);
    constexpr int LEFT = 16;
    constexpr int RIGHT = 624;
    constexpr int GAP = 18;

    //The prompts at the right (their labels large, read from a couch, unless that leaves the label
    //too little room)
    int promptsLeft = RIGHT;
    if (!row.empty()) {
        prompts::setLargeLabels(true);
        int w = prompts::promptsWidth(row, GAP);
        if (!label.empty() && RIGHT - w - LEFT < 240) {
            prompts::setLargeLabels(false);
            w = prompts::promptsWidth(row, GAP);
        }
        promptsLeft = RIGHT - w;
        prompts::drawPrompts(promptsLeft + w / 2, 458, row, GAP);
        //(where its B prompt is: a click there goes back, as B)
        int px = promptsLeft;
        for (const prompts::Prompt& p : row) {
            const int pw = prompts::promptsWidth({p}, GAP);
            const bool back = std::find(p.pads.begin(), p.pads.end(), prompts::Pad::B) != p.pads.end() ||
                              std::find(p.keys.begin(), p.keys.end(), prompts::menuKey(prompts::MENU_CANCEL)) != p.keys.end();
            if (back)
                s_footerBack = {px - 4, 448, pw + 8, 32};
            px += pw + GAP;
        }
        prompts::setLargeLabels(false);
    }

    //The label in the room left (moving to show all of it when it's longer)
    if (label.empty())
        return;
    int x = LEFT;
    if (icon >= 0) {
        theme::statusIcon(x, 456, static_cast<theme::Status>(icon));
        x += 22;
    }
    const int room = promptsLeft - (row.empty() ? 0 : 20) - x;
    if (room <= 0)
        return;
    if (rm->menu_font_large.getWidth(label) <= room)
        rm->menu_font_large.draw(x, 455, label);
    else
        rm->menu_font_small.drawScrolling(x, 459, room, label);
}
} // namespace menupanel

MI_MenuPanel* AddPanelBehind(UI_Menu& menu, const std::vector<const UI_Control*>& controls, short padX, short padY)
{
    int x0 = 640, y0 = 480, x1 = 0, y1 = 0;
    for (const UI_Control* control : controls) {
        const UI_Control::Box b = control->FocusBox();
        if (b.w <= 0)
            continue;
        x0 = std::min<int>(x0, b.x);
        y0 = std::min<int>(y0, b.y);
        x1 = std::max<int>(x1, b.x + b.w);
        y1 = std::max<int>(y1, b.y + b.h);
    }
    if (x1 <= x0)
        return nullptr;
    //(clear of the title bar and the footer; at the sides, room for the controller's pointer at the
    //focused control's left, as far as the screen's border allows)
    constexpr int POINTER_ROOM = 12;
    x0 = std::max(std::min(36, x0 - padX), x0 - padX - POINTER_ROOM);
    x1 = std::min(std::max(604, x1 + padX), x1 + padX + POINTER_ROOM);
    x0 = std::max(4, x0);
    x1 = std::min(636, x1);
    y0 = std::max(36, y0 - padY);
    y1 = std::min(446, y1 + padY + 2);    //(the rim is thicker at the bottom: as much blue below as above)
    auto* panel = new MI_MenuPanel(static_cast<short>(x0), static_cast<short>(y0), static_cast<short>(x1 - x0), static_cast<short>(y1 - y0));
    menu.AddBackdrop(panel);
    return panel;
}

MI_MenuPanel* AddPanelBehind(UI_Menu& menu, std::initializer_list<const UI_Control*> controls, short padX, short padY)
{
    return AddPanelBehind(menu, std::vector<const UI_Control*>(controls), padX, padY);
}


MI_MenuPanel::MI_MenuPanel(short x, short y, short w, short h, std::string title, std::string note)
    : UI_Control(x, y)
    , m_w(w)
    , m_h(h)
    , m_title(std::move(title))
    , m_note(std::move(note))
{}

void MI_MenuPanel::SetTitle(std::string title, std::string note)
{
    m_title = std::move(title);
    m_note = std::move(note);
}

void MI_DialogBox::Draw()
{
    if (m_visible)
        menupanel::dialogBox(m_pos.x, m_pos.y);
}

void MI_MenuPanel::Draw()
{
    if (!m_visible)
        return;
    menupanel::box(m_pos.x, m_pos.y, m_w, m_h);
    if (!m_title.empty())
        rm->menu_font_large.draw(m_pos.x + 20, m_pos.y + 10, m_title.c_str());
    const std::string note = m_noteSource ? m_noteSource() : m_note;
    if (!note.empty())
        rm->menu_font_small.drawRightJustified(m_pos.x + m_w - 20, m_pos.y + 14, note.c_str());
}


MI_PromptStrip::MI_PromptStrip(short x, short y, short w, std::string label, std::function<std::vector<prompts::Prompt>()> row)
    : UI_Control(x, y)
    , m_w(w)
    , m_label(std::move(label))
    , m_row(std::move(row))
{}

void MI_PromptStrip::Draw()
{
    if (!m_visible || !m_row)
        return;
    //(the footer, as on every menu: its place is the bottom of the screen)
    menupanel::footer(m_row(), m_label);
}
