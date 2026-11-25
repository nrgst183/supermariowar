#include "AINav.h"

#include "DebugProfile.h"
#include "Game.h"
#include "GlobalConstants.h"
#include "IO_Block.h"
#include "objects/blocks/BreakableBlock.h"
#include "map.h"
#include "movingplatform.h"
#include "MovingPlatformPaths.h"
#include "TileTypes.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>


namespace ainav {
namespace {

constexpr int W = MAPWIDTH;
constexpr int H = MAPHEIGHT;
constexpr int NODES = W * H;

constexpr int MAX_POINTS = 3000;   // platform points at most (nodes NODES and up: the search keeps a node in 12 bits)

struct Edge {
    uint16_t to;
    uint8_t cost;
    Move move;
    BoardKind board = BoardNone;
};

// A point of a moving platform's way: where its middle is then, and how it's moving
struct Point {
    float x, y;
    float vx, vy;
};

struct Platform {
    const MovingPlatform* platform = nullptr;
    int firstPoint = 0;   // its points, points[firstPoint, firstPoint + count), in the order it goes
    int count = 0;
    int ticks = 0;        // from one point to the next
    int period = 0;       // the whole way round
    int halfW = 0;
    int halfH = 0;
    int deckTop = 0;      // what we stand on: the top of its topmost floor (pixels down from its top)...
    int deckLeft = 0;     // ...across these pixels (from its left side)
    int deckRight = 0;
    bool deckThrough = false;   // that floor is one we can jump up through
};

struct Graph {
    uint64_t stamp = 0;          // hash of what the graph was built from
    bool built = false;
    std::array<bool, NODES> spot {};
    std::array<bool, H> edgeOpen {};   // a player fits through the left/right screen edge in this row
    std::vector<Edge> edges;     // all edges, grouped by node
    std::vector<uint32_t> first; // node n's edges: edges[first[n], first[n + 1]); the tile spots, then the platform points
    std::vector<Platform> platforms;
    std::vector<Point> points;   // node NODES + i

    int nodes() const { return NODES + static_cast<int>(points.size()); }
};

Graph g_graph;

int wrapCol(int c) { return ((c % W) + W) % W; }
int node(int c, int r) { return r * W + c; }

bool blockSolid(int c, int r)
{
    IO_Block* b = g_map->block(static_cast<short>(c), static_cast<short>(r));
    return b && !b->isTransparent() && !b->isHidden();
}

// A brick: a block that breaks for good when a player hits it from below
bool breakableAt(int c, int r)
{
    if (r < 0 || r >= H)
        return false;
    IO_Block* b = g_map->block(static_cast<short>(wrapCol(c)), static_cast<short>(r));
    return b && !b->isHidden() && dynamic_cast<B_BreakableBlock*>(b) != nullptr;
}

bool solid(int c, int r)
{
    if (r < 0 || r >= H)
        return false;
    c = wrapCol(c);
    return (g_map->map(c, r) & tile_flag_solid) || blockSolid(c, r);
}

bool passable(int c, int r)
{
    return !solid(c, r);
}

bool floorAt(int c, int r)
{
    if (r < 0 || r >= H)
        return false;
    c = wrapCol(c);
    const int f = g_map->map(c, r);
    if (f & tile_flag_death_on_top)
        return false;
    return (f & (tile_flag_solid | tile_flag_solid_on_top)) || blockSolid(c, r);
}

bool standable(int c, int r)
{
    if (r < 0 || r + 1 >= H)
        return false;
    c = wrapCol(c);
    const int f = g_map->map(c, r);
    if (f & tile_flag_has_regular_death)
        return false;
    return passable(c, r) && floorAt(c, r + 1);
}

// Falling down column c from row r (exclusive): the first spot, or -1 (a pit or death)
int landing(int c, int r)
{
    for (int y = r + 1; y + 1 < H; y++) {
        if (!passable(c, y))
            return -1;
        if (g_map->map(wrapCol(c), y + 1) & tile_flag_death_on_top)
            return -1;
        if (standable(c, y))
            return y;
    }
    return -1;
}

uint64_t mapStamp()
{
    uint64_t h = 1469598103934665603ull;
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            const uint64_t v = static_cast<uint64_t>(g_map->map(c, r)) * 2 + (blockSolid(c, r) ? 1 : 0);
            h = (h ^ v) * 1099511628211ull;
        }
    }
    for (const MovingPlatform* p : g_map->permanentPlatforms())
        h = (h ^ reinterpret_cast<uintptr_t>(p)) * 1099511628211ull;
    //(the warps too: the Map Maker changes them in a match, and the graph goes through them)
    for (int k = 0; k < g_map->warpExitCount(); k++) {
        const WarpExit& e = g_map->warpExit(k);
        const uint64_t v = static_cast<uint64_t>(static_cast<uint16_t>(e.warpx)) | static_cast<uint64_t>(static_cast<uint16_t>(e.warpy)) << 16
            | static_cast<uint64_t>(static_cast<uint16_t>(e.connection)) << 32 | static_cast<uint64_t>(static_cast<uint16_t>(e.direction)) << 48;
        h = (h ^ v ^ static_cast<uint64_t>(e.numblocks) << 8) * 1099511628211ull;
    }
    return h;
}

void build()
{
    Graph& gr = g_graph;
    gr.edges.clear();
    gr.first.assign(NODES + 1, 0);
    gr.platforms.clear();
    gr.points.clear();
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++)
            gr.spot[node(c, r)] = standable(c, r);
    for (int r = 0; r < H; r++)
        gr.edgeOpen[r] = passable(0, r) && passable(W - 1, r);

    auto add = [&](Move m, int c, int r, int cost) {
        gr.edges.push_back(Edge {static_cast<uint16_t>(node(wrapCol(c), r)), static_cast<uint8_t>(cost < 255 ? cost : 255), m});
    };

    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            gr.first[node(c, r)] = static_cast<uint32_t>(gr.edges.size());
            if (!gr.spot[node(c, r)])
                continue;

            for (int d = -1; d <= 1; d += 2) {
                // Walk to the next tile, or walk off the edge and fall
                const int c1 = c + d;
                if (passable(c1, r)) {
                    if (standable(c1, r)) {
                        add(Move::Walk, c1, r, 2);
                    } else {
                        const int y1 = landing(c1, r);
                        if (y1 >= 0)
                            add(Move::Walk, c1, y1, 2 + (y1 - r));
                        // Running off the edge carries us a tile further
                        const int c2 = c + 2 * d;
                        if (passable(c2, r)) {
                            const int y2 = standable(c2, r) ? r : landing(c2, r);
                            if (y2 >= 0)
                                add(Move::Walk, c2, y2, 3 + (y2 - r));
                        }
                    }
                }

                // Jump across a gap to the same level or lower
                for (int dx = 2; dx <= 3; dx++) {
                    bool clear = true;
                    for (int k = 1; k <= dx && clear; k++)
                        clear = passable(c + d * k, r) && passable(c + d * k, r - 1);
                    if (!clear)
                        break;
                    for (int down = 0; down <= 3; down++) {
                        if (standable(c + d * dx, r + down)) {
                            bool fallClear = true;
                            for (int y = r + 1; y <= r + down && fallClear; y++)
                                fallClear = passable(c + d * dx, y);
                            if (fallClear)
                                add(Move::Jump, c + d * dx, r + down, 4 + dx + down);
                            break;
                        }
                    }
                }
            }

            // Jump up: straight up first (the tiles above us must be free, platforms we can pass
            // through from below are fine), then over to the side at the top
            for (int up = 1; up <= 3; up++) {
                if (!passable(c, r - up))
                    break;
                const int maxDx = up == 3 ? 1 : 3;
                for (int dx = -maxDx; dx <= maxDx; dx++) {
                    const int tc = c + dx;
                    const int tr = r - up;
                    if (!standable(tc, tr))
                        continue;
                    bool clear = true;
                    const int step = dx < 0 ? -1 : 1;
                    for (int k = step; dx != 0 && clear && k != dx + step; k += step)
                        clear = passable(c + k, tr);
                    // Straight up through a platform: its floor must be one we can jump through
                    if (dx == 0 && solid(c, tr + 1))
                        clear = false;
                    if (clear)
                        add(Move::Jump, tc, tr, 4 + up + (dx < 0 ? -dx : dx));
                }
            }

            // Through a brick overhead: a breakable block in reach of a jump straight up (the tiles
            // between free, or floors we jump through) breaks when we hit it from below, for good; then
            // the graph is built again without it (the map's blocks changed). An edge to standing on
            // it (as if it were gone already: what's above it is reached from there), dearer than
            // a jump, so it's only taken when it's the better way (the stairs on Sidewinder's right
            // side, capped by one)
            for (int up = 1; up <= 3; up++) {
                if (r - up < 0)
                    break;
                if (breakableAt(c, r - up)) {
                    if (standable(c, r - up - 1))
                        add(Move::Jump, c, r - up - 1, 10 + up);
                    break;
                }
                if (!passable(c, r - up))
                    break;
            }

            // A running jump (VELTURBOJUMP, about 4 tiles): up 4 tiles beside a ledge two columns
            // over, then onto it. It needs speed at take-off: a tile of run-up behind us. The jump
            // carries us on a little while we brake, so this column and the next must be open all
            // the way up (never under the ledge while rising).
            for (int d = -1; d <= 1; d += 2) {
                const int tc = c + 2 * d;
                const int tr = r - 4;
                if (!standable(tc, tr) || !standable(c - d, r))
                    continue;
                bool open = true;
                for (int up = 1; up <= 4 && open; up++)
                    open = passable(c, r - up) && passable(c + d, r - up);
                if (open)
                    add(Move::RunJump, tc, tr, 10);
            }

            // Drop through the platform we stand on
            const int f = g_map->map(c, r + 1);
            if ((f & tile_flag_solid_on_top) && !(f & tile_flag_solid) && !blockSolid(c, r + 1)) {
                for (int y = r + 2; y + 1 < H; y++) {
                    if (!passable(c, y) || (g_map->map(c, y + 1) & tile_flag_death_on_top))
                        break;
                    if (standable(c, y)) {
                        add(Move::Drop, c, y, 3 + (y - r));
                        break;
                    }
                }
            }
        }
    }
    gr.first[NODES] = static_cast<uint32_t>(gr.edges.size());
    gr.built = true;
}

// Warp pipes: a pipe beside a spot (we walk into its side) or under it (we press down) leads to
// every other exit of its connection (the game picks one at random). Edges go into a second
// list, appended to each node's own after build() (see addExtra).
void addWarps(std::vector<std::vector<Edge>>& extra)
{
    Graph& gr = g_graph;
    // Where a player coming out of an exit ends up standing
    auto exitSpot = [](const WarpExit& e) {
        int c = -1, r = -1;
        int x = e.x;
        if (e.direction == WarpExitDirection::Right)
            x += TILESIZE;
        else if (e.direction == WarpExitDirection::Left)
            x -= TILESIZE;
        //(as PlayerSpot, without building the graph again)
        int cx = x + PW / 2;
        if (cx < 0)
            cx += App::screenWidth;
        else if (cx >= App::screenWidth)
            cx -= App::screenWidth;
        c = wrapCol(cx / TILESIZE);
        r = (e.y + PH - 1) / TILESIZE;
        if (r < 0 || r >= H)
            return -1;
        if (!g_graph.spot[node(c, r)]) {
            r = landing(c, r);
            if (r < 0)
                return -1;
        }
        return node(c, r);
    };
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            if (!gr.spot[node(c, r)])
                continue;
            for (int side : {-1, 1, 0}) {
                const int wc = wrapCol(c + side);
                const int wr = side == 0 ? r + 1 : r;
                if (wr >= H)
                    continue;
                Warp* w = g_map->warp(static_cast<short>(wc), static_cast<short>(wr));
                const WarpEnterDirection want = side < 0 ? WARP_LEFT : side > 0 ? WARP_RIGHT : WARP_DOWN;
                if (!w || w->connection < 0 || w->direction != want)
                    continue;
                for (int k = 0; k < g_map->warpExitCount(); k++) {
                    const WarpExit& e = g_map->warpExit(k);
                    if (e.connection != w->connection || e.id == w->id)
                        continue;
                    const int to = exitSpot(e);
                    if (to < 0 || to == node(c, r))
                        continue;
                    extra[node(c, r)].push_back(Edge {static_cast<uint16_t>(to), 8, Move::Warp});
                }
            }
        }
    }
}

// Moving platforms. Each one's way is cut into points about half a tile apart (in the order it
// goes, round and round: there and back for a straight path), worked out from its path, so the
// same on every machine. A path gets on at a point (from a spot it's then in reach of), rides
// from point to point and gets off at one (onto a spot in reach then).

// What we stand on: each column's topmost floor tile, side by side columns with theirs in the same
// row making one deck (an airship has a few, at different heights); not the deadly ones
std::vector<Platform> decksOf(const MovingPlatform& mp, const Platform& like)
{
    std::vector<Platform> decks;
    int lastRow = -1;
    for (int tx = 0; tx < mp.tileWidth(); tx++) {
        int row = -1;
        int f = 0;
        for (int ty = 0; ty < mp.tileHeight() && row < 0; ty++) {
            f = tileToFlags(mp.tileTypeAt(static_cast<size_t>(tx), static_cast<size_t>(ty)));
            if (f & (tile_flag_solid | tile_flag_solid_on_top))
                row = ty;
        }
        if (row >= 0 && (f & (tile_flag_death_on_top | tile_flag_super_or_player_death_top)))
            row = -1;
        if (row < 0) {
            lastRow = -1;
            continue;
        }
        if (row != lastRow) {
            Platform pl = like;
            pl.deckTop = row * TILESIZE;
            pl.deckLeft = tx * TILESIZE;
            pl.deckThrough = true;
            decks.push_back(pl);
        }
        Platform& deck = decks.back();
        deck.deckRight = (tx + 1) * TILESIZE;
        if (f & tile_flag_solid)
            deck.deckThrough = false;
        lastRow = row;
    }
    return decks;
}

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void addPlatformPoints()
{
    Graph& gr = g_graph;
    for (const MovingPlatform* mp : g_map->permanentPlatforms()) {
        const MovingPlatformPath* path = mp->path();
        Platform pl;
        pl.platform = mp;
        pl.halfW = mp->width() / 2;
        pl.halfH = mp->height() / 2;
        if (!path)
            continue;

        std::vector<Point> pts;
        float period = 0.0f;
        const float v = path->speed();
        switch (path->typeId()) {
            case PlatformPathType::Straight: {
                // There and back (as StraightPath: a tick more each way than the length takes)
                const auto* sp = static_cast<const StraightPath*>(path);
                const Vec2f a = sp->startPos();
                const Vec2f b = sp->endPos();
                const float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
                if (len < 1.0f || v <= 0.0f)
                    break;
                const int m = clampInt(static_cast<int>(len / 16.0f) + 1, 2, 32);
                for (int leg = 0; leg < 2; leg++) {
                    const Vec2f& from = leg == 0 ? a : b;
                    const Vec2f& to = leg == 0 ? b : a;
                    for (int j = 0; j < m; j++) {
                        const float t = static_cast<float>(j) / static_cast<float>(m);
                        pts.push_back(Point {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t,
                                             (to.x - from.x) / len * v, (to.y - from.y) / len * v});
                    }
                }
                period = 2.0f * static_cast<float>(static_cast<int>(len / v) + 1);
                break;
            }
            case PlatformPathType::Ellipse: {
                // speed: the angle a tick
                const auto* ep = static_cast<const EllipsePath*>(path);
                const Vec2f c = ep->centerPos();
                const Vec2f rad = ep->radius();
                if (v == 0.0f)
                    break;
                const float around = TWO_PI * (rad.x > rad.y ? rad.x : rad.y);
                const int m = clampInt(static_cast<int>(around / 16.0f), 8, 48);
                for (int j = 0; j < m; j++) {
                    const float ang = ep->startAngle() + (v > 0.0f ? 1.0f : -1.0f) * TWO_PI * static_cast<float>(j) / static_cast<float>(m);
                    pts.push_back(Point {rad.x * std::cos(ang) + c.x, rad.y * std::sin(ang) + c.y,
                                         -rad.x * std::sin(ang) * v, rad.y * std::cos(ang) * v});
                }
                period = TWO_PI / std::fabs(v);
                break;
            }
            case PlatformPathType::StraightContinuous: {
                // Straight on, coming back in on the other side (as StraightPathContinuous::Move);
                // only across or up/down: a slanted one isn't back where it started each time round
                const auto* cp = static_cast<const StraightPathContinuous*>(path);
                const float vx = v * std::cos(cp->angle());
                const float vy = v * std::sin(cp->angle());
                const bool across = std::fabs(vy) < 0.001f && std::fabs(vx) > 0.001f;
                const bool upDown = std::fabs(vx) < 0.001f && std::fabs(vy) > 0.001f;
                if (!across && !upDown)
                    break;
                const float wrapH = static_cast<float>(App::screenHeight + mp->height());
                const float cycle = across ? static_cast<float>(App::screenWidth) : wrapH;
                const int m = clampInt(static_cast<int>(cycle / 16.0f), 8, 64);
                for (int j = 0; j < m; j++) {
                    const float d = cycle * static_cast<float>(j) / static_cast<float>(m);
                    float x = cp->startPos().x + (across ? (vx > 0.0f ? d : -d) : 0.0f);
                    float y = cp->startPos().y + (upDown ? (vy > 0.0f ? d : -d) : 0.0f);
                    while (x - static_cast<float>(pl.halfW) < 0.0f)
                        x += static_cast<float>(App::screenWidth);
                    while (x - static_cast<float>(pl.halfW) >= static_cast<float>(App::screenWidth))
                        x -= static_cast<float>(App::screenWidth);
                    while (y + static_cast<float>(pl.halfH) < 0.0f)
                        y += wrapH;
                    while (y - static_cast<float>(pl.halfH) >= static_cast<float>(App::screenHeight))
                        y -= wrapH;
                    pts.push_back(Point {x, y, vx, vy});
                }
                period = cycle / (across ? std::fabs(vx) : std::fabs(vy));
                break;
            }
            default:
                break;   // falling ones don't come back
        }
        if (pts.empty())
            continue;
        pl.count = static_cast<int>(pts.size());
        pl.period = static_cast<int>(period);
        pl.ticks = pl.period / pl.count > 1 ? pl.period / pl.count : 1;
        // Each deck rides the same way: its own points
        for (Platform& deck : decksOf(*mp, pl)) {
            if (gr.points.size() + pts.size() > static_cast<size_t>(MAX_POINTS))
                break;
            deck.firstPoint = static_cast<int>(gr.points.size());
            gr.points.insert(gr.points.end(), pts.begin(), pts.end());
            gr.platforms.push_back(deck);
        }
    }
}

// Where the deck is (pixels) with the platform's middle at (x,y)
struct Deck {
    int left, right, top;
};

Deck deckAt(const Platform& pl, float x, float y)
{
    const int l = static_cast<int>(std::floor(x)) - pl.halfW;
    const int t = static_cast<int>(std::floor(y)) - pl.halfH;
    return Deck {l + pl.deckLeft, l + pl.deckRight, t + pl.deckTop};
}

// The tile row (or column) a pixel is in, also above (left of) the screen
int tileOf(int y)
{
    return y < 0 ? -1 - (-1 - y) / TILESIZE : y / TILESIZE;
}

// The pixels [left,right) seen from column c, the nearest way around the screen: the gap between
// them (negative: they overlap), which side they're on (0: overlapping) and their column nearest to c
struct Near {
    int gap = 1 << 20;
    int side = 0;
    int col = 0;
};

Near nearTo(int c, int left, int right)
{
    const int sl = c * TILESIZE;
    const int sr = sl + TILESIZE;
    Near best;
    for (int shift : {-App::screenWidth, 0, App::screenWidth}) {
        const int l = left + shift;
        const int r = right + shift;
        Near n;
        if (l >= sr) {
            n.gap = l - sr;
            n.side = 1;
            n.col = wrapCol(tileOf(l));
        } else if (r <= sl) {
            n.gap = sl - r;
            n.side = -1;
            n.col = wrapCol(tileOf(r - 1));
        } else {
            n.gap = (l > sl ? l : sl) - (r < sr ? r : sr);
            n.col = c;
        }
        if (n.gap < best.gap)
            best = n;
    }
    return best;
}

// Can we get from the spot (c,r) onto the deck, and how
BoardKind boardKind(int c, int r, const Deck& d, const Platform& pl)
{
    if (d.top < TILESIZE || d.top > App::screenHeight)
        return BoardNone;
    const Near n = nearTo(c, d.left, d.right);
    const int up = (r + 1) * TILESIZE - d.top;   // the deck above our floor
    const int feetRow = tileOf(d.top - 1);       // on the deck: the rows we're in
    const int headRow = tileOf(d.top - PH);
    if (up >= -6 && up <= 6)
        return n.gap >= -4 && n.gap <= 8 ? BoardWalk : BoardNone;
    // (as high as a jump goes: only from right beside it, as the tile jumps)
    if (up > 6 && up <= 3 * TILESIZE && n.gap <= (up > 2 * TILESIZE ? 8 : 40)) {
        // Straight up onto it: through its floor
        if (n.side == 0 && !pl.deckThrough)
            return BoardNone;
        // Up our column until we're above it, then across
        for (int y = r - 1; y >= headRow; y--) {
            if (!passable(c, y))
                return BoardNone;
        }
        for (int y = headRow; n.side != 0 && y <= feetRow; y++) {
            if (!passable(c + n.side, y))
                return BoardNone;
        }
        return BoardJump;
    }
    if (up < -6 && up >= -192 && n.gap <= 40) {
        // Off our ledge: the column beside us (toward it) open down to it (no floor on the way: we'd
        // land there); right under our column, it must reach under that one too
        int side = n.side;
        if (side == 0) {
            side = (d.left + d.right) / 2 >= c * TILESIZE + TILESIZE / 2 ? 1 : -1;
            if (nearTo(c + side, d.left, d.right).gap > -8)
                return BoardNone;
        }
        for (int y = r; y <= feetRow; y++) {
            if (!passable(c + side, y) || (y > r && floorAt(c + side, y)))
                return BoardNone;
        }
        return BoardDrop;
    }
    return BoardNone;
}

// Can we get off the deck onto the spot (c,r): walking (or walking off its side and falling), or
// jumping; Move::None if not
Move alightMove(const Deck& d, int c, int r)
{
    if (d.top < TILESIZE || d.top > App::screenHeight)
        return Move::None;
    const Near n = nearTo(c, d.left, d.right);
    const int up = d.top - (r + 1) * TILESIZE;   // the spot's floor above the deck
    const int feetRow = tileOf(d.top - 1);
    if (up >= -6 && up <= 6)
        return n.gap >= -4 && n.gap <= 8 ? Move::Walk : Move::None;
    if (up > 6 && up <= 3 * TILESIZE && n.gap <= (up > 2 * TILESIZE ? 8 : 40)) {
        // Up the deck's column nearest to the spot (through the spot's floor when it's over us: one
        // we can jump through), then across
        if (n.side == 0 && solid(c, r + 1))
            return Move::None;
        for (int y = feetRow; y >= r - 1; y--) {
            if (n.side == 0 && y == r + 1)
                continue;
            if (!passable(n.col, y))
                return Move::None;
        }
        return Move::Jump;
    }
    if (up < -6 && up >= -192 && n.side != 0 && n.gap >= -4 && n.gap <= 40) {
        // Off its side, falling onto the spot
        if (!passable(c, feetRow) || landing(c, feetRow - 1) != r)
            return Move::None;
        return Move::Walk;
    }
    return Move::None;
}

void addPlatformEdges(std::vector<std::vector<Edge>>& extra)
{
    Graph& gr = g_graph;
    for (const Platform& pl : gr.platforms) {
        // Riding on to the next point
        const int ride = clampInt(pl.ticks / 4, 1, 255);
        for (int k = 0; pl.count > 1 && k < pl.count; k++) {
            const int to = NODES + pl.firstPoint + (k + 1) % pl.count;
            extra[static_cast<size_t>(NODES + pl.firstPoint + k)].push_back(Edge {static_cast<uint16_t>(to), static_cast<uint8_t>(ride), Move::Ride});
        }

        // Waiting for it: half the way round on average (a step of the path is about 4 ticks)
        const int wait = clampInt(pl.period / 8, 0, 120);

        // Spots it goes through: never wait for it there (it could crush us)
        std::array<bool, NODES> swept {};
        for (int k = 0; k < pl.count; k++) {
            const Point& p = gr.points[static_cast<size_t>(pl.firstPoint + k)];
            const int top = static_cast<int>(std::floor(p.y)) - pl.halfH;
            const int left = static_cast<int>(std::floor(p.x)) - pl.halfW;
            for (int r = 0; r < H; r++) {
                const int floorY = (r + 1) * TILESIZE;
                if (top >= floorY || top + 2 * pl.halfH <= floorY - PH)
                    continue;
                // The columns where it covers more than the gap beside a player in the middle of one
                const int inset = (TILESIZE - PW) / 2;
                for (int c = tileOf(left + inset); c <= tileOf(left + 2 * pl.halfW - 1 - inset); c++)
                    swept[node(wrapCol(c), r)] = true;
            }
        }

        for (int r = 0; r < H; r++) {
            for (int c = 0; c < W; c++) {
                const int n = node(c, r);
                if (!gr.spot[n])
                    continue;
                for (int k = 0; k < pl.count; k++) {
                    const int pn = NODES + pl.firstPoint + k;
                    const Point& p = gr.points[static_cast<size_t>(pl.firstPoint + k)];
                    const Deck d = deckAt(pl, p.x, p.y);
                    const int up = (r + 1) * TILESIZE - d.top;
                    if (up > 192 || up < -192)
                        continue;   // out of reach, on or off
                    const BoardKind b = swept[n] ? BoardNone : boardKind(c, r, d, pl);
                    if (b != BoardNone) {
                        const int cost = (b == BoardWalk ? 2 : b == BoardDrop ? 4 : 6) + wait;
                        extra[static_cast<size_t>(n)].push_back(Edge {static_cast<uint16_t>(pn), static_cast<uint8_t>(cost), Move::Board, b});
                    }
                    const Move m = alightMove(d, c, r);
                    if (m != Move::None)
                        extra[static_cast<size_t>(pn)].push_back(Edge {static_cast<uint16_t>(n), static_cast<uint8_t>(m == Move::Walk ? 3 : 6), m});
                }
            }
        }
    }
}

// Which of our platforms (its decks) a player at (ix,iy) (top left) stands on
int platformIndex(const MovingPlatform* platform, int ix, int iy)
{
    int best = -1;
    int bestD = 0;
    for (size_t i = 0; i < g_graph.platforms.size(); i++) {
        const Platform& pl = g_graph.platforms[i];
        if (pl.platform != platform)
            continue;
        const Deck d = deckAt(pl, platform->centerX(), platform->centerY());
        const int gap = nearTo(tileOf(ix + PW / 2), d.left, d.right).gap;
        const int dy = iy + PH - d.top;
        const int dist = (dy < 0 ? -dy : dy) + (gap > 0 ? gap : 0);
        if (best < 0 || dist < bestD) {
            best = static_cast<int>(i);
            bestD = dist;
        }
    }
    return best;
}

int currentPoint(const Platform& pl);

// Where the deck will be in ticks: on along its way (it may turn back meanwhile), or straight on
// for a moment
Deck deckAhead(const Platform& pl, int ticks)
{
    const MovingPlatform& mp = *pl.platform;
    if (ticks <= pl.ticks || pl.count < 2) {
        const float t = static_cast<float>(ticks);
        return deckAt(pl, mp.centerX() + mp.velX() * t, mp.centerY() + mp.velY() * t);
    }
    const int on = (ticks + pl.ticks / 2) / pl.ticks;
    const Point& p = g_graph.points[static_cast<size_t>(pl.firstPoint + (currentPoint(pl) + on) % pl.count)];
    return deckAt(pl, p.x, p.y);
}

// The point of its way the platform is at now (nearest to where it is, going the same way)
int currentPoint(const Platform& pl)
{
    const MovingPlatform& mp = *pl.platform;
    int best = 0;
    float bestD = 0.0f;
    for (int k = 0; k < pl.count; k++) {
        const Point& p = g_graph.points[static_cast<size_t>(pl.firstPoint + k)];
        float dx = p.x - mp.centerX();
        if (dx > static_cast<float>(App::screenWidth / 2))
            dx -= static_cast<float>(App::screenWidth);
        else if (dx < -static_cast<float>(App::screenWidth / 2))
            dx += static_cast<float>(App::screenWidth);
        const float dy = p.y - mp.centerY();
        float d = dx * dx + dy * dy;
        if (p.vx * mp.velX() + p.vy * mp.velY() < 0.0f)
            d += 1.0e6f;
        if (k == 0 || d < bestD) {
            best = k;
            bestD = d;
        }
    }
    return best;
}

// The edges of the second list (warps, platforms) after each node's own; nodes past the tile
// spots (the platform points) have only those
void addExtra(const std::vector<std::vector<Edge>>& extra)
{
    Graph& gr = g_graph;
    const int nodes = static_cast<int>(extra.size());
    std::vector<Edge> merged;
    merged.reserve(gr.edges.size() + 256);
    std::vector<uint32_t> first(static_cast<size_t>(nodes) + 1);
    for (int n = 0; n < nodes; n++) {
        first[n] = static_cast<uint32_t>(merged.size());
        if (n < NODES)
            merged.insert(merged.end(), gr.edges.begin() + gr.first[n], gr.edges.begin() + gr.first[n + 1]);
        merged.insert(merged.end(), extra[n].begin(), extra[n].end());
    }
    first[nodes] = static_cast<uint32_t>(merged.size());
    gr.first = std::move(first);
    gr.edges = std::move(merged);
}

void ensureGraph()
{
    const uint64_t stamp = mapStamp();
    if (!g_graph.built || g_graph.stamp != stamp) {
        prof::Scope profile(prof::Section::NavBuild);
        build();
        addPlatformPoints();
        std::vector<std::vector<Edge>> extra(static_cast<size_t>(g_graph.nodes()));
        addWarps(extra);
        addPlatformEdges(extra);
        addExtra(extra);
        g_graph.stamp = stamp;
    }
}

int shortestDir(int from, int to)
{
    int d = to - from;
    if (d > W / 2)
        d -= W;
    else if (d < -W / 2)
        d += W;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

bool columnOpen(int c, int fromRow, int toRow)
{
    for (int r = toRow; r <= fromRow; r++)
        if (!passable(c, r))
            return false;
    return true;
}

}  // namespace

int SpotBelow(int col, int row)
{
    if (!g_map)
        return -1;
    ensureGraph();
    for (int y = row + 1; y <= row + 3 && y + 1 < H; y++) {
        if (!passable(col, y))
            return -1;
        if (g_graph.spot[node(wrapCol(col), y)])
            return y;
    }
    return -1;
}

bool RunJumpFrom(int col, int row, int dir)
{
    if (!g_map || col < 0 || row < 0 || row >= H)
        return false;
    ensureGraph();
    const int n = node(wrapCol(col), row);
    for (uint32_t e = g_graph.first[n]; e < g_graph.first[n + 1]; e++) {
        const Edge& edge = g_graph.edges[e];
        if (edge.move == Move::RunJump && shortestDir(wrapCol(col), edge.to % W) == dir)
            return true;
    }
    return false;
}

bool CanCrossEdge(int y1, int y2)
{
    if (!g_map)
        return true;
    ensureGraph();
    int top = (y1 < y2 ? y1 : y2) / TILESIZE - 3;   // a jump reaches about 3 tiles up
    int bottom = (y1 > y2 ? y1 : y2) / TILESIZE + 3;
    if (top < 0)
        top = 0;
    if (bottom >= H)
        bottom = H - 1;
    for (int r = top; r <= bottom; r++) {
        if (g_graph.edgeOpen[r])
            return true;
    }
    // Above the map the edges are always open
    return (y1 < y2 ? y1 : y2) < TILESIZE;
}

int HorizontalDistance(int dx, int y1, int y2, bool* wrapped)
{
    bool wrap = false;
    if (dx > App::screenWidth / 2 || dx < -App::screenWidth / 2) {
        if (CanCrossEdge(y1, y2)) {
            dx += dx > 0 ? -App::screenWidth : App::screenWidth;
            wrap = true;
        }
    }
    if (wrapped)
        *wrapped = wrap;
    return dx;
}

int FreeRun(int ix, int iy, int dir, int max)
{
    if (!g_map)
        return max;
    ensureGraph();
    // The rows the player's body covers
    const int top = iy < 0 ? 0 : iy / TILESIZE;
    const int bottom = (iy + PH - 1) / TILESIZE;
    // The column the player's leading edge is in
    int edge = dir < 0 ? ix : ix + PW - 1;
    if (edge < 0)
        edge += App::screenWidth;
    else if (edge >= App::screenWidth)
        edge -= App::screenWidth;
    int c = edge / TILESIZE;
    for (int n = 0; n < max; n++) {
        c += dir;
        // Leaving the screen: only where the far side is open too
        for (int r = top; r <= bottom && r < H; r++) {
            if (!passable(c, r))
                return n;
        }
    }
    return max;
}

bool PlayerSpot(int ix, int iy, int& col, int& row)
{
    if (!g_map)
        return false;
    ensureGraph();
    int cx = ix + PW / 2;
    if (cx < 0)
        cx += App::screenWidth;
    else if (cx >= App::screenWidth)
        cx -= App::screenWidth;
    col = wrapCol(cx / TILESIZE);
    const int feet = iy + PH - 1;
    if (feet < 0)
        return false;
    row = feet / TILESIZE;
    if (row >= H)
        return false;
    if (g_graph.spot[node(col, row)])
        return true;
    // Standing on the end of a ledge, our middle past it: the spot under our other foot
    for (int foot : {ix, ix + PW - 1}) {
        const int c = wrapCol(tileOf(foot));
        if (c != col && g_graph.spot[node(c, row)]) {
            col = c;
            return true;
        }
    }
    const int y = landing(col, row);
    if (y < 0)
        return false;
    row = y;
    return true;
}

namespace {

// The spots nearest to where the target stands, or lands
int targetGoals(int targetX, int targetY, std::array<int, 4>& goalNodes)
{
    int tx = targetX;
    if (tx < 0)
        tx += App::screenWidth;
    else if (tx >= App::screenWidth)
        tx -= App::screenWidth;
    const int tcol = wrapCol(tx / TILESIZE);
    int trow = targetY < 0 ? 0 : targetY / TILESIZE;
    if (trow >= H)
        trow = H - 1;

    int goals = 0;
    for (int dc = 0; dc <= 1 && goals == 0; dc++) {
        for (int c : {tcol - dc, tcol + dc}) {
            if (g_graph.spot[node(wrapCol(c), trow)])
                goalNodes[goals++] = node(wrapCol(c), trow);
            else {
                const int y = landing(c, trow);
                if (y >= 0)
                    goalNodes[goals++] = node(wrapCol(c), y);
            }
            if (dc == 0)
                break;
        }
    }
    return goals;
}

// Dijkstra from the node start to the nearest goal, with a binary heap (ties broken by node
// number, so every machine picks the same path): the path's first edge (an index into the edges)
// and its length; -1 when there is no way (or we're there)
int search(int start, const std::array<int, 4>& goalNodes, int goals, int& distance)
{
    for (int k = 0; k < goals; k++) {
        if (goalNodes[k] == start)
            return -1;
    }
    const int nodes = g_graph.nodes();
    static std::vector<int> dist;
    static std::vector<int> firstEdge;   // the edge out of the start that leads here
    static std::vector<unsigned char> done;
    static std::vector<uint32_t> heap;   // (distance << 12) | node; a push per edge at most
    dist.assign(static_cast<size_t>(nodes), 1 << 30);
    firstEdge.assign(static_cast<size_t>(nodes), -1);
    done.assign(static_cast<size_t>(nodes), 0);
    heap.resize(g_graph.edges.size() + 16);
    dist[start] = 0;
    int heapSize = 0;
    auto push = [&](int d, int n) {
        if (heapSize >= static_cast<int>(heap.size()))
            return;
        uint32_t v = (static_cast<uint32_t>(d) << 12) | static_cast<uint32_t>(n);
        int i = heapSize++;
        while (i > 0 && heap[(i - 1) / 2] > v) {
            heap[i] = heap[(i - 1) / 2];
            i = (i - 1) / 2;
        }
        heap[i] = v;
    };
    auto pop = [&]() {
        const uint32_t top = heap[0];
        const uint32_t v = heap[--heapSize];
        int i = 0;
        for (;;) {
            int c = 2 * i + 1;
            if (c >= heapSize)
                break;
            if (c + 1 < heapSize && heap[c + 1] < heap[c])
                c++;
            if (heap[c] >= v)
                break;
            heap[i] = heap[c];
            i = c;
        }
        if (heapSize > 0)
            heap[i] = v;
        return top;
    };
    push(0, start);
    while (heapSize > 0) {
        const int u = static_cast<int>(pop() & 0xfff);
        if (done[u])
            continue;
        done[u] = 1;
        for (int k = 0; k < goals; k++) {
            if (goalNodes[k] == u) {
                distance = dist[u];
                return firstEdge[u];
            }
        }
        for (uint32_t i = g_graph.first[u]; i < g_graph.first[u + 1]; i++) {
            const Edge& e = g_graph.edges[i];
            const int nd = dist[u] + e.cost;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                firstEdge[e.to] = u == start ? static_cast<int>(i) : firstEdge[u];
                push(nd, e.to);
            }
        }
    }
    return -1;
}

// The step for the path's first edge, taken from (col,row)
Step stepFor(int edge, int col, int row, int distance)
{
    Step result;
    const Edge& e = g_graph.edges[static_cast<size_t>(edge)];
    result.move = e.move;
    result.distance = distance;
    if (e.to >= NODES) {
        // Onto a platform, or on along its way
        const int point = e.to - NODES;
        for (size_t i = 0; i < g_graph.platforms.size(); i++) {
            const Platform& pl = g_graph.platforms[i];
            if (point >= pl.firstPoint && point < pl.firstPoint + pl.count)
                result.platform = static_cast<int>(i);
        }
        result.point = point;
        result.board = e.board;
        return result;
    }
    result.toCol = e.to % W;
    result.toRow = e.to / W;
    result.dir = shortestDir(col, result.toCol);
    if (e.move == Move::Warp) {
        //(the pipe's side, or 0: under us)
        const Warp* w = g_map->warp(static_cast<short>(wrapCol(col - 1)), static_cast<short>(row));
        result.dir = w && w->direction == WARP_LEFT ? -1 : 0;
        const Warp* wr = g_map->warp(static_cast<short>(wrapCol(col + 1)), static_cast<short>(row));
        if (result.dir == 0 && wr && wr->direction == WARP_RIGHT)
            result.dir = 1;
    }
    return result;
}

}  // namespace

Step NextStep(int col, int row, int targetX, int targetY)
{
    if (!g_map || col < 0 || row < 0 || row >= H)
        return Step {};
    ensureGraph();
    std::array<int, 4> goalNodes {};
    const int goals = targetGoals(targetX, targetY, goalNodes);
    const int start = node(col, row);
    if (goals == 0 || !g_graph.spot[start])
        return Step {};
    int distance = 0;
    const int edge = search(start, goalNodes, goals, distance);
    return edge < 0 ? Step {} : stepFor(edge, col, row, distance);
}

Step NextStepFromPlatform(const MovingPlatform* platform, int ix, int iy, int targetX, int targetY)
{
    if (!g_map || !platform)
        return Step {};
    ensureGraph();
    const int index = platformIndex(platform, ix, iy);
    if (index < 0)
        return Step {};
    const Platform& pl = g_graph.platforms[static_cast<size_t>(index)];
    std::array<int, 4> goalNodes {};
    const int goals = targetGoals(targetX, targetY, goalNodes);
    if (goals == 0)
        return Step {};
    int cx = ix + PW / 2;
    if (cx < 0)
        cx += App::screenWidth;
    else if (cx >= App::screenWidth)
        cx -= App::screenWidth;
    const int row = tileOf(iy + PH - 1);
    int distance = 0;
    const int edge = search(NODES + pl.firstPoint + currentPoint(pl), goalNodes, goals, distance);
    return edge < 0 ? Step {} : stepFor(edge, wrapCol(cx / TILESIZE), row, distance);
}

BoardKind BoardNow(const Step& step, int col, int row, int targetX, int targetY, int& aimX)
{
    if (!g_map || step.platform < 0 || step.platform >= static_cast<int>(g_graph.platforms.size()))
        return BoardNone;
    const Platform& pl = g_graph.platforms[static_cast<size_t>(step.platform)];
    const MovingPlatform& mp = *pl.platform;
    std::array<int, 4> goalNodes {};
    const int goals = targetGoals(targetX, targetY, goalNodes);
    int fromPlanned = 0;
    if (goals == 0 || search(step.point + NODES, goalNodes, goals, fromPlanned) < 0)
        return BoardNone;

    // Each way on takes a while (stepping off and falling: about sqrt(2 * height / gravity) ticks;
    // a jump up and onto it): where the platform will be when we come down onto it must be in reach
    // that way, and the way on from there as short as from where the path gets on (not past where
    // it gets off already, or going the other way)
    const int below = static_cast<int>(std::floor(mp.centerY())) - pl.halfH + pl.deckTop - (row + 1) * TILESIZE;
    for (BoardKind kind : {BoardWalk, BoardJump, BoardDrop}) {
        int lead = 4;
        if (kind == BoardJump)
            lead = 16;
        else if (kind == BoardDrop)
            lead = 8 + static_cast<int>(std::sqrt(2.0f * static_cast<float>(below > 0 ? below : 0) / GRAVITATION));
        if (boardKind(col, row, deckAhead(pl, lead), pl) != kind)
            continue;
        const int on = (lead + pl.ticks / 2) / pl.ticks;
        int fromThen = 0;
        const bool okThen = search(NODES + pl.firstPoint + (currentPoint(pl) + on) % pl.count, goalNodes, goals, fromThen) >= 0;
        if (!okThen || fromThen > fromPlanned + 3)
            continue;
        if (PlatformAim(step.platform, col * TILESIZE + (TILESIZE - PW) / 2, lead, aimX))
            return kind;
    }
    return BoardNone;
}

bool PlatformAim(int platform, int ix, int ahead, int& aimX)
{
    if (!g_map || platform < 0 || platform >= static_cast<int>(g_graph.platforms.size()))
        return false;
    const Deck d = deckAhead(g_graph.platforms[static_cast<size_t>(platform)], ahead);
    // The deck the nearest way around the screen
    const int center = ix + PW / 2;
    int left = d.left;
    int right = d.right;
    while ((left + right) / 2 - center > App::screenWidth / 2) {
        left -= App::screenWidth;
        right -= App::screenWidth;
    }
    while (center - (left + right) / 2 > App::screenWidth / 2) {
        left += App::screenWidth;
        right += App::screenWidth;
    }
    const int margin = (right - left) / 2 < 10 ? (right - left) / 2 : 10;
    aimX = clampInt(center, left + margin, right - margin) - PW / 2;
    return true;
}


bool OverPlatform(int platform, int ix, int iy, int ahead)
{
    if (!g_map || platform < 0 || platform >= static_cast<int>(g_graph.platforms.size()))
        return false;
    const Platform& pl = g_graph.platforms[static_cast<size_t>(platform)];
    // Now or a little later (it may be sliding under us while we come down)
    for (int k = 0; k <= 4; k++) {
        const Deck d = deckAhead(pl, ahead * k);
        if (iy + PH > d.top + 4)
            continue;
        int center = ix + PW / 2;
        while (center < d.left - App::screenWidth / 2)
            center += App::screenWidth;
        while (center >= d.left + App::screenWidth / 2)
            center -= App::screenWidth;
        if (center >= d.left - 24 && center < d.right + 24)
            return true;
    }
    return false;
}

int RiseColumn(int col, int row, int toCol, int toRow)
{
    col = wrapCol(col);
    toCol = wrapCol(toCol);
    const int dir = shortestDir(col, toCol);
    if (dir == 0 || toRow >= row)
        return col;
    int best = col;
    for (int c = wrapCol(col + dir); ; c = wrapCol(c + dir)) {
        if (c == toCol) {
            //(its floor one we jump through: we can be under it already, unless it's as high as a
            //jump goes: then rising through it we may not get over it)
            if (!solid(c, toRow + 1) && row - toRow < 3 && columnOpen(c, row, toRow))
                best = c;
            break;
        }
        if (!columnOpen(c, row, toRow))
            break;
        best = c;
    }
    return best;
}

}  // namespace ainav
