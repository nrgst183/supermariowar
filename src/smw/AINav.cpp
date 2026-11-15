#include "AINav.h"

#include "Game.h"
#include "GlobalConstants.h"
#include "IO_Block.h"
#include "map.h"
#include "TileTypes.h"

#include <array>
#include <cstdint>
#include <vector>


namespace ainav {
namespace {

constexpr int W = MAPWIDTH;
constexpr int H = MAPHEIGHT;
constexpr int NODES = W * H;

struct Edge {
    uint16_t to;
    uint8_t cost;
    Move move;
};

struct Graph {
    uint64_t stamp = 0;          // hash of what the graph was built from
    bool built = false;
    std::array<bool, NODES> spot {};
    std::vector<Edge> edges;     // all edges, grouped by node
    std::array<uint16_t, NODES + 1> first {};
};

Graph g_graph;

int wrapCol(int c) { return ((c % W) + W) % W; }
int node(int c, int r) { return r * W + c; }

bool blockSolid(int c, int r)
{
    IO_Block* b = g_map->block(static_cast<short>(c), static_cast<short>(r));
    return b && !b->isTransparent() && !b->isHidden();
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
    return h;
}

void build()
{
    Graph& gr = g_graph;
    gr.edges.clear();
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++)
            gr.spot[node(c, r)] = standable(c, r);

    auto add = [&](Move m, int c, int r, int cost) {
        gr.edges.push_back(Edge {static_cast<uint16_t>(node(wrapCol(c), r)), static_cast<uint8_t>(cost < 255 ? cost : 255), m});
    };

    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            gr.first[node(c, r)] = static_cast<uint16_t>(gr.edges.size());
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
    gr.first[NODES] = static_cast<uint16_t>(gr.edges.size());
    gr.built = true;
}

void ensureGraph()
{
    const uint64_t stamp = mapStamp();
    if (!g_graph.built || g_graph.stamp != stamp) {
        build();
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

}  // namespace

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
    const int y = landing(col, row);
    if (y < 0)
        return false;
    row = y;
    return true;
}

Step NextStep(int col, int row, int targetX, int targetY)
{
    Step result;
    if (!g_map || col < 0 || row < 0 || row >= H)
        return result;
    ensureGraph();

    // The target spot: where the target stands, or lands
    int tx = targetX;
    if (tx < 0)
        tx += App::screenWidth;
    else if (tx >= App::screenWidth)
        tx -= App::screenWidth;
    const int tcol = wrapCol(tx / TILESIZE);
    int trow = targetY < 0 ? 0 : targetY / TILESIZE;
    if (trow >= H)
        trow = H - 1;

    std::array<int, NODES> goalNodes {};
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
    if (goals == 0)
        return result;

    const int start = node(col, row);
    if (!g_graph.spot[start])
        return result;
    for (int k = 0; k < goals; k++) {
        if (goalNodes[k] == start)
            return result;   // already there
    }

    // Dijkstra over at most 300 spots, with a binary heap (ties broken by node number, so
    // every machine picks the same path)
    std::array<int, NODES> dist;
    std::array<int, NODES> firstEdge;   // the edge out of the start that leads here
    std::array<bool, NODES> done {};
    dist.fill(1 << 30);
    firstEdge.fill(-1);
    dist[start] = 0;
    std::array<uint32_t, 4096> heap;    // (distance << 12) | node; more pushes than this can't happen
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
    for (;;) {
        if (heapSize == 0)
            return result;
        const int u = static_cast<int>(pop() & 0xfff);
        if (done[u])
            continue;
        done[u] = true;
        for (int k = 0; k < goals; k++) {
            if (goalNodes[k] == u) {
                const Edge& e = g_graph.edges[firstEdge[u]];
                result.move = e.move;
                result.toCol = e.to % W;
                result.toRow = e.to / W;
                result.dir = shortestDir(col, result.toCol);
                result.distance = dist[u];
                return result;
            }
        }
        for (int i = g_graph.first[u]; i < g_graph.first[u + 1]; i++) {
            const Edge& e = g_graph.edges[i];
            const int nd = dist[u] + e.cost;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                firstEdge[e.to] = u == start ? i : firstEdge[u];
                push(nd, e.to);
            }
        }
    }
}

}  // namespace ainav
