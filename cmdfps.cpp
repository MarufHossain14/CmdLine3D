#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

namespace
{
constexpr int kCanvasWidth = 320;
constexpr int kCanvasHeight = 200;
constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 600;
constexpr int kViewWidth = 248;
constexpr int kViewTop = 13;
constexpr int kHorizon = 101;
constexpr float kPi = 3.14159265f;
constexpr float kFieldOfView = kPi / 3.0f;
constexpr float kViewDepth = 18.0f;
constexpr float kMoveSpeed = 3.0f;
constexpr float kStrafeSpeed = 2.7f;
constexpr float kTurnSpeed = 1.35f;
constexpr float kMouseSensitivity = 0.0022f;

constexpr std::uint32_t rgb(int red, int green, int blue)
{
    return 0xff000000u | (static_cast<std::uint32_t>(red) << 16) |
           (static_cast<std::uint32_t>(green) << 8) | static_cast<std::uint32_t>(blue);
}

constexpr std::uint32_t kInk = rgb(225, 236, 238);
constexpr std::uint32_t kCyan = rgb(52, 213, 209);
constexpr std::uint32_t kGold = rgb(255, 190, 70);
constexpr std::uint32_t kRed = rgb(244, 76, 92);
constexpr std::uint32_t kGreen = rgb(92, 225, 126);
constexpr std::uint32_t kPanel = rgb(13, 18, 31);

const std::vector<std::string> kMap = {
    "################",
    "#..............#",
    "#..##..........#",
    "#..##.....###..#",
    "#.........#....#",
    "#.........#....#",
    "#....####.#....#",
    "#..............#",
    "#..............#",
    "#..#...........#",
    "#..#....####...#",
    "#..#...........#",
    "#..######......#",
    "#..............#",
    "#..............#",
    "################",
};

struct Input
{
    float forward = 0.0f;
    float strafe = 0.0f;
    float turn = 0.0f;
    float mouseTurn = 0.0f;
    bool shoot = false;
};

struct Player
{
    float x = 8.0f;
    float y = 8.0f;
    float angle = 0.0f;
};

struct Target
{
    float x;
    float y;
    bool alive = true;
};

struct GridPoint
{
    int x;
    int y;
};

struct WeaponAnimation
{
    float bobPhase = 0.0f;
    float recoilTime = 0.0f;
    float flashTime = 0.0f;
    float movement = 0.0f;
};

// A tiny pixel canvas keeps the renderer independent of GPU APIs. SDL only
// scales this finished 320x200 image into the window.
class Canvas
{
public:
    Canvas() : pixels(kCanvasWidth * kCanvasHeight, 0) {}

    void clear(std::uint32_t color) { std::fill(pixels.begin(), pixels.end(), color); }

    void set(int x, int y, std::uint32_t color)
    {
        if (x >= 0 && x < kCanvasWidth && y >= 0 && y < kCanvasHeight)
            pixels[y * kCanvasWidth + x] = color;
    }

    void rectangle(int x, int y, int width, int height, std::uint32_t color)
    {
        const int left = std::max(0, x);
        const int right = std::min(kCanvasWidth, x + width);
        const int top = std::max(0, y);
        const int bottom = std::min(kCanvasHeight, y + height);
        for (int row = top; row < bottom; ++row)
            std::fill(pixels.begin() + row * kCanvasWidth + left,
                      pixels.begin() + row * kCanvasWidth + right, color);
    }

    void line(int x0, int y0, int x1, int y1, std::uint32_t color)
    {
        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        while (true)
        {
            set(x0, y0, color);
            if (x0 == x1 && y0 == y1)
                break;
            const int twiceError = error * 2;
            if (twiceError >= dy) { error += dy; x0 += sx; }
            if (twiceError <= dx) { error += dx; y0 += sy; }
        }
    }

    void text(int x, int y, const std::string& value, std::uint32_t color, int scale = 1)
    {
        for (char letter : value)
        {
            const auto rows = glyph(letter >= 'a' && letter <= 'z' ? letter - 32 : letter);
            for (int row = 0; row < 7; ++row)
                for (int column = 0; column < 5; ++column)
                    if ((rows[row] & (1u << (4 - column))) != 0)
                        rectangle(x + column * scale, y + row * scale, scale, scale, color);
            x += 6 * scale;
        }
    }

    std::vector<std::uint32_t> pixels;

private:
    static std::array<std::uint8_t, 7> glyph(char value)
    {
        using G = std::array<std::uint8_t, 7>;
        switch (value)
        {
        case 'A': return G{14,17,17,31,17,17,17};
        case 'B': return G{30,17,17,30,17,17,30};
        case 'C': return G{14,17,16,16,16,17,14};
        case 'D': return G{30,17,17,17,17,17,30};
        case 'E': return G{31,16,16,30,16,16,31};
        case 'F': return G{31,16,16,30,16,16,16};
        case 'G': return G{14,17,16,23,17,17,14};
        case 'H': return G{17,17,17,31,17,17,17};
        case 'I': return G{14,4,4,4,4,4,14};
        case 'J': return G{7,2,2,2,18,18,12};
        case 'K': return G{17,18,20,24,20,18,17};
        case 'L': return G{16,16,16,16,16,16,31};
        case 'M': return G{17,27,21,21,17,17,17};
        case 'N': return G{17,25,21,19,17,17,17};
        case 'O': return G{14,17,17,17,17,17,14};
        case 'P': return G{30,17,17,30,16,16,16};
        case 'Q': return G{14,17,17,17,21,18,13};
        case 'R': return G{30,17,17,30,20,18,17};
        case 'S': return G{15,16,16,14,1,1,30};
        case 'T': return G{31,4,4,4,4,4,4};
        case 'U': return G{17,17,17,17,17,17,14};
        case 'V': return G{17,17,17,17,17,10,4};
        case 'W': return G{17,17,17,21,21,21,10};
        case 'X': return G{17,17,10,4,10,17,17};
        case 'Y': return G{17,17,10,4,4,4,4};
        case 'Z': return G{31,1,2,4,8,16,31};
        case '0': return G{14,17,19,21,25,17,14};
        case '1': return G{4,12,4,4,4,4,14};
        case '2': return G{14,17,1,2,4,8,31};
        case '3': return G{30,1,1,14,1,1,30};
        case '4': return G{2,6,10,18,31,2,2};
        case '5': return G{31,16,16,30,1,1,30};
        case '6': return G{14,16,16,30,17,17,14};
        case '7': return G{31,1,2,4,8,8,8};
        case '8': return G{14,17,17,14,17,17,14};
        case '9': return G{14,17,17,15,1,1,14};
        case ':': return G{0,4,4,0,4,4,0};
        case '/': return G{1,2,2,4,8,8,16};
        case '-': return G{0,0,0,31,0,0,0};
        case '.': return G{0,0,0,0,0,12,12};
        case '+': return G{0,4,4,31,4,4,0};
        default: return G{0,0,0,0,0,0,0};
        }
    }
};

float wrappedAngle(float angle)
{
    while (angle > kPi) angle -= 2.0f * kPi;
    while (angle < -kPi) angle += 2.0f * kPi;
    return angle;
}

bool isWall(float x, float y)
{
    const int mapX = static_cast<int>(x);
    const int mapY = static_cast<int>(y);
    if (mapX < 0 || mapY < 0 || mapY >= static_cast<int>(kMap.size()) ||
        mapX >= static_cast<int>(kMap[0].size()))
        return true;
    return kMap[mapY][mapX] == '#';
}

bool canStandAt(float x, float y)
{
    constexpr float radius = 0.18f;
    return !isWall(x - radius, y - radius) && !isWall(x + radius, y - radius) &&
           !isWall(x - radius, y + radius) && !isWall(x + radius, y + radius);
}

void updatePlayer(Player& player, const Input& input, float elapsed)
{
    player.angle += input.turn * kTurnSpeed * elapsed + input.mouseTurn * kMouseSensitivity;

    float forward = input.forward;
    float strafe = input.strafe;
    const float length = std::hypot(forward, strafe);
    if (length > 1.0f) { forward /= length; strafe /= length; }

    const float nextX = player.x +
        (std::cos(player.angle) * forward * kMoveSpeed -
         std::sin(player.angle) * strafe * kStrafeSpeed) * elapsed;
    const float nextY = player.y +
        (std::sin(player.angle) * forward * kMoveSpeed +
         std::cos(player.angle) * strafe * kStrafeSpeed) * elapsed;
    if (canStandAt(nextX, player.y)) player.x = nextX;
    if (canStandAt(player.x, nextY)) player.y = nextY;
}

struct RayHit
{
    float distance = kViewDepth;
    float wallCoordinate = 0.0f;
    int side = 0;
};

RayHit castRay(const Player& player, float angle)
{
    const float rayX = std::cos(angle);
    const float rayY = std::sin(angle);
    int mapX = static_cast<int>(player.x);
    int mapY = static_cast<int>(player.y);
    const float deltaX = std::abs(rayX) < 0.0001f ? 1e30f : std::abs(1.0f / rayX);
    const float deltaY = std::abs(rayY) < 0.0001f ? 1e30f : std::abs(1.0f / rayY);
    const int stepX = rayX < 0.0f ? -1 : 1;
    const int stepY = rayY < 0.0f ? -1 : 1;
    float sideX = rayX < 0.0f ? (player.x - mapX) * deltaX : (mapX + 1.0f - player.x) * deltaX;
    float sideY = rayY < 0.0f ? (player.y - mapY) * deltaY : (mapY + 1.0f - player.y) * deltaY;
    int side = 0;

    for (int step = 0; step < 64; ++step)
    {
        if (sideX < sideY) { sideX += deltaX; mapX += stepX; side = 0; }
        else { sideY += deltaY; mapY += stepY; side = 1; }
        if (isWall(static_cast<float>(mapX), static_cast<float>(mapY)))
            break;
    }

    float distance = side == 0 ?
        (mapX - player.x + (1 - stepX) * 0.5f) / rayX :
        (mapY - player.y + (1 - stepY) * 0.5f) / rayY;
    distance = std::clamp(distance, 0.05f, kViewDepth);
    float wallCoordinate = side == 0 ? player.y + distance * rayY : player.x + distance * rayX;
    wallCoordinate -= std::floor(wallCoordinate);
    return {distance, wallCoordinate, side};
}

std::vector<float> drawWorld(Canvas& canvas, const Player& player)
{
    for (int y = kViewTop; y < kHorizon; ++y)
    {
        const int glow = (y - kViewTop) * 18 / (kHorizon - kViewTop);
        canvas.rectangle(0, y, kViewWidth, 1, rgb(9 + glow / 3, 14 + glow / 2, 28 + glow));
    }
    for (int y = kHorizon; y < kCanvasHeight; ++y)
    {
        const int near = (y - kHorizon) * 30 / (kCanvasHeight - kHorizon);
        canvas.rectangle(0, y, kViewWidth, 1, rgb(20 + near, 23 + near, 29 + near));
    }

    std::vector<float> depth(kViewWidth, kViewDepth);
    std::vector<int> wallTop(kViewWidth, kHorizon);
    std::vector<int> wallBottom(kViewWidth, kHorizon);
    for (int x = 0; x < kViewWidth; ++x)
    {
        const float rayAngle = player.angle - kFieldOfView * 0.5f +
            (static_cast<float>(x) + 0.5f) / kViewWidth * kFieldOfView;
        const RayHit hit = castRay(player, rayAngle);
        const float corrected = std::max(0.05f, hit.distance * std::cos(rayAngle - player.angle));
        depth[x] = corrected;
        const int wallHeight = std::min(260, static_cast<int>(152.0f / corrected));
        const int top = std::max(kViewTop, kHorizon - wallHeight / 2);
        const int bottom = std::min(kCanvasHeight - 1, kHorizon + wallHeight / 2);
        wallTop[x] = top;
        wallBottom[x] = bottom;

        float brightness = 1.0f / (1.0f + corrected * 0.12f);
        if (hit.side == 1) brightness *= 0.78f;
        if ((static_cast<int>(hit.wallCoordinate * 8.0f) & 1) != 0) brightness *= 0.86f;
        const int light = std::clamp(static_cast<int>(235.0f * brightness), 25, 230);
        const std::uint32_t wall = rgb(light / 4, light * 3 / 4, light);
        canvas.rectangle(x, top, 1, bottom - top + 1, wall);
    }

    for (int x = 1; x < kViewWidth; ++x)
        if (std::abs(depth[x] - depth[x - 1]) > 0.65f)
            canvas.rectangle(x, wallTop[x], 1, wallBottom[x] - wallTop[x] + 1, rgb(7, 13, 22));
    return depth;
}

bool clearShot(const Player& player, const Target& target, float distance)
{
    const float directionX = (target.x - player.x) / distance;
    const float directionY = (target.y - player.y) / distance;
    for (float step = 0.08f; step < distance; step += 0.04f)
        if (isWall(player.x + directionX * step, player.y + directionY * step))
            return false;
    return true;
}

bool fireShot(const Player& player, std::vector<Target>& targets)
{
    Target* best = nullptr;
    float bestDistance = kViewDepth;
    for (Target& target : targets)
    {
        if (!target.alive) continue;
        const float distance = std::hypot(target.x - player.x, target.y - player.y);
        const float error = std::abs(wrappedAngle(
            std::atan2(target.y - player.y, target.x - player.x) - player.angle));
        const float hitWidth = std::max(0.025f, std::atan(0.32f / distance));
        if (error < hitWidth && distance < bestDistance && clearShot(player, target, distance))
        {
            best = &target;
            bestDistance = distance;
        }
    }
    if (best == nullptr) return false;
    best->alive = false;
    return true;
}

void drawTargets(Canvas& canvas, const Player& player, const std::vector<Target>& targets,
                 const std::vector<float>& depth)
{
    std::vector<const Target*> ordered;
    for (const Target& target : targets) if (target.alive) ordered.push_back(&target);
    std::sort(ordered.begin(), ordered.end(), [&](const Target* a, const Target* b) {
        return std::hypot(a->x - player.x, a->y - player.y) >
               std::hypot(b->x - player.x, b->y - player.y);
    });

    for (const Target* target : ordered)
    {
        const float dx = target->x - player.x;
        const float dy = target->y - player.y;
        const float distance = std::hypot(dx, dy);
        const float relative = wrappedAngle(std::atan2(dy, dx) - player.angle);
        if (std::abs(relative) > kFieldOfView * 0.58f) continue;
        const float projected = distance * std::cos(relative);
        const int centerX = static_cast<int>((relative / kFieldOfView + 0.5f) * kViewWidth);
        const int size = std::clamp(static_cast<int>(112.0f / projected), 7, 100);
        const int top = kHorizon - size / 2;

        for (int sy = 0; sy < size; ++sy)
        {
            const float ny = static_cast<float>(sy) / size;
            for (int sx = -size / 2; sx <= size / 2; ++sx)
            {
                const int screenX = centerX + sx;
                if (screenX < 0 || screenX >= kViewWidth || projected >= depth[screenX]) continue;
                const float nx = static_cast<float>(sx) / (size * 0.5f);
                bool body = false;
                std::uint32_t color = kRed;
                if (ny < 0.30f)
                {
                    const float headY = (ny - 0.15f) / 0.15f;
                    body = nx * nx + headY * headY < 0.72f;
                    color = rgb(255, 151, 65);
                }
                else if (ny < 0.70f)
                {
                    body = std::abs(nx) < (ny < 0.48f ? 0.62f : 0.43f);
                }
                else
                {
                    body = std::abs(nx - 0.25f) < 0.17f || std::abs(nx + 0.25f) < 0.17f;
                    color = rgb(185, 45, 67);
                }
                if (body) canvas.set(screenX, top + sy, color);
            }
        }

        if (size >= 16 && centerX >= 2 && centerX < kViewWidth - 2 && projected < depth[centerX])
        {
            canvas.set(centerX - size / 7, top + size / 7, rgb(20, 15, 20));
            canvas.set(centerX + size / 7, top + size / 7, rgb(20, 15, 20));
        }
    }
}

std::vector<GridPoint> findRoute(const Player& player, const std::vector<Target>& targets)
{
    const int width = static_cast<int>(kMap[0].size());
    const int height = static_cast<int>(kMap.size());
    const int start = static_cast<int>(player.y) * width + static_cast<int>(player.x);
    std::vector<int> parent(width * height, -1);
    std::queue<int> frontier;
    parent[start] = start;
    frontier.push(start);
    int goal = -1;
    constexpr std::array<GridPoint, 4> directions = {
        GridPoint{1, 0}, GridPoint{-1, 0}, GridPoint{0, 1}, GridPoint{0, -1}
    };
    const auto isTarget = [&](int x, int y) {
        return std::any_of(targets.begin(), targets.end(), [&](const Target& target) {
            return target.alive && static_cast<int>(target.x) == x && static_cast<int>(target.y) == y;
        });
    };

    while (!frontier.empty() && goal < 0)
    {
        const int cell = frontier.front();
        frontier.pop();
        const int x = cell % width;
        const int y = cell / width;
        if (isTarget(x, y)) { goal = cell; break; }
        for (const GridPoint direction : directions)
        {
            const int nx = x + direction.x;
            const int ny = y + direction.y;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height || kMap[ny][nx] == '#') continue;
            const int next = ny * width + nx;
            if (parent[next] != -1) continue;
            parent[next] = cell;
            frontier.push(next);
        }
    }

    std::vector<GridPoint> route;
    if (goal < 0) return route;
    for (int cell = goal; cell != start; cell = parent[cell])
        route.push_back({cell % width, cell / width});
    std::reverse(route.begin(), route.end());
    return route;
}

std::string routeInstruction(const Player& player, const std::vector<GridPoint>& route,
                             bool targetsRemain)
{
    if (!targetsRemain) return "AREA CLEAR";
    if (route.empty()) return "AIM HERE";
    const GridPoint next = route.front();
    const float angle = wrappedAngle(
        std::atan2(next.y + 0.5f - player.y, next.x + 0.5f - player.x) - player.angle);
    if (std::abs(angle) < 0.30f) return "GO FORWARD";
    return angle < 0.0f ? "TURN LEFT" : "TURN RIGHT";
}

void drawMiniMap(Canvas& canvas, const Player& player, const std::vector<Target>& targets,
                 const std::vector<GridPoint>& route)
{
    constexpr int tile = 3;
    constexpr int mapX = 260;
    constexpr int mapY = 17;
    for (int y = 0; y < static_cast<int>(kMap.size()); ++y)
        for (int x = 0; x < static_cast<int>(kMap[y].size()); ++x)
            canvas.rectangle(mapX + x * tile, mapY + y * tile, tile, tile,
                             kMap[y][x] == '#' ? rgb(40, 125, 158) : rgb(20, 28, 43));
    for (const GridPoint point : route)
        canvas.rectangle(mapX + point.x * tile + 1, mapY + point.y * tile + 1, 2, 2, kGold);
    for (const Target& target : targets)
    {
        const std::uint32_t color = target.alive ? kRed : rgb(70, 82, 91);
        canvas.rectangle(mapX + static_cast<int>(target.x) * tile,
                         mapY + static_cast<int>(target.y) * tile, tile, tile, color);
    }
    const int px = mapX + static_cast<int>(player.x) * tile + 1;
    const int py = mapY + static_cast<int>(player.y) * tile + 1;
    canvas.rectangle(px - 1, py - 1, 3, 3, kGreen);
    canvas.line(px, py, px + static_cast<int>(std::cos(player.angle) * 5.0f),
                py + static_cast<int>(std::sin(player.angle) * 5.0f), kGreen);
}

void updateWeapon(WeaponAnimation& weapon, const Input& input, float elapsed)
{
    const float targetMovement = std::min(1.0f, std::hypot(input.forward, input.strafe));
    weapon.movement += (targetMovement - weapon.movement) *
        (1.0f - std::exp(-12.0f * elapsed));
    if (weapon.movement > 0.02f)
        weapon.bobPhase += elapsed * 9.0f;
    weapon.recoilTime = std::max(0.0f, weapon.recoilTime - elapsed);
    weapon.flashTime = std::max(0.0f, weapon.flashTime - elapsed);
}

void drawWeapon(Canvas& canvas, const WeaponAnimation& weapon)
{
    const int center = kViewWidth / 2;
    const int bobX = static_cast<int>(std::round(std::sin(weapon.bobPhase) * 2.0f * weapon.movement));
    const int bobY = static_cast<int>(std::round(std::abs(std::cos(weapon.bobPhase)) * 2.0f * weapon.movement));
    const int recoil = weapon.recoilTime > 0.0f ? static_cast<int>(weapon.recoilTime * 55.0f) : 0;
    const int x = center + bobX;
    const int bottom = 202 + bobY - recoil;
    const std::uint32_t outline = rgb(9, 12, 19);
    const std::uint32_t dark = rgb(48, 59, 72);
    const std::uint32_t metal = rgb(102, 124, 139);
    const std::uint32_t light = rgb(176, 202, 207);

    canvas.rectangle(x - 5, bottom - 49, 10, 28, outline);
    canvas.rectangle(x - 3, bottom - 48, 6, 28, metal);
    canvas.rectangle(x - 6, bottom - 50, 12, 5, outline);
    canvas.rectangle(x - 4, bottom - 49, 8, 3, light);
    canvas.rectangle(x - 24, bottom - 25, 48, 20, outline);
    canvas.rectangle(x - 21, bottom - 23, 42, 16, dark);
    canvas.rectangle(x - 17, bottom - 21, 34, 5, metal);
    canvas.rectangle(x - 29, bottom - 20, 8, 11, outline);
    canvas.rectangle(x + 21, bottom - 20, 8, 11, outline);
    canvas.rectangle(x - 6, bottom - 8, 12, 13, outline);
    canvas.rectangle(x - 4, bottom - 7, 8, 12, rgb(35, 43, 55));
    canvas.line(x - 21, bottom - 7, x - 28, bottom, light);
    canvas.line(x + 21, bottom - 7, x + 28, bottom, light);

    if (weapon.flashTime > 0.0f)
    {
        const int muzzleY = bottom - 55;
        canvas.line(x, muzzleY - 11, x, muzzleY + 1, rgb(255, 241, 126));
        canvas.line(x - 8, muzzleY - 7, x + 8, muzzleY + 1, kGold);
        canvas.line(x + 8, muzzleY - 7, x - 8, muzzleY + 1, kGold);
        canvas.rectangle(x - 2, muzzleY - 5, 5, 7, rgb(255, 245, 180));
    }
}

void drawCrosshair(Canvas& canvas)
{
    constexpr int x = kViewWidth / 2;
    constexpr int y = kHorizon;
    canvas.rectangle(x - 6, y, 4, 1, kInk);
    canvas.rectangle(x + 3, y, 4, 1, kInk);
    canvas.rectangle(x, y - 6, 1, 4, kInk);
    canvas.rectangle(x, y + 3, 1, 4, kInk);
    canvas.rectangle(x, y, 1, 1, kGold);
}

void drawInterface(Canvas& canvas, const Player& player, const std::vector<Target>& targets,
                   const std::vector<GridPoint>& route, int shots,
                   const std::string& message, float messageTime,
                   float wallDistance, bool mouseCaptured)
{
    const int targetsLeft = static_cast<int>(std::count_if(
        targets.begin(), targets.end(), [](const Target& target) { return target.alive; }));
    canvas.rectangle(0, 0, kCanvasWidth, kViewTop, rgb(10, 16, 27));
    canvas.text(4, 3, "CMDLINE3D", kCyan);
    canvas.text(76, 3, "TARGETS " + std::to_string(targetsLeft), kGreen);
    canvas.text(154, 3, "SHOTS " + std::to_string(shots), kInk);

    canvas.rectangle(kViewWidth, 0, kCanvasWidth - kViewWidth, kCanvasHeight, kPanel);
    canvas.rectangle(kViewWidth, 0, 1, kCanvasHeight, rgb(48, 133, 169));
    canvas.text(255, 4, "NAVIGATION", kGold);
    drawMiniMap(canvas, player, targets, route);
    canvas.text(255, 70, "NEXT", kGold);
    canvas.text(255, 79, routeInstruction(player, route, targetsLeft > 0), kInk);
    canvas.text(255, 88, std::to_string(route.size()) + " STEPS", rgb(140, 157, 170));
    canvas.text(255, 103, "CONTROLS", kGold);
    canvas.text(255, 113, "W/S MOVE", kInk);
    canvas.text(255, 122, "A/D STRAFE", kInk);
    canvas.text(255, 131, "Q/E TURN", kInk);
    canvas.text(255, 140, "MOUSE AIM", kInk);
    canvas.text(255, 149, "CLICK FIRE", kInk);
    canvas.text(255, 158, "SPACE FIRE", kInk);
    canvas.text(255, 167, "TAB MOUSE", kInk);
    canvas.text(255, 176, "ESC QUIT", kInk);

    std::string notice;
    std::uint32_t noticeColor = kGold;
    if (!mouseCaptured) notice = "MOUSE FREE - CLICK GAME TO CAPTURE";
    else if (targetsLeft == 0) { notice = "AREA CLEAR"; noticeColor = kGreen; }
    else if (messageTime > 0.0f) { notice = message; noticeColor = message == "HIT" ? kGreen : kGold; }
    else if (wallDistance < 0.75f) notice = "WALL AHEAD - BACK UP OR TURN";
    if (!notice.empty())
    {
        const int width = static_cast<int>(notice.size()) * 6;
        canvas.rectangle(std::max(0, (kViewWidth - width) / 2 - 3), 17, width + 6, 11, rgb(8, 12, 20));
        canvas.text(std::max(2, (kViewWidth - width) / 2), 19, notice, noticeColor);
    }
}

bool initializeSdl(SDL_Window*& window, SDL_Renderer*& renderer, SDL_Texture*& texture,
                   bool smokeTest)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }
    const SDL_WindowFlags flags = smokeTest ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
    if (!SDL_CreateWindowAndRenderer("CmdLine3D", kWindowWidth, kWindowHeight,
                                     flags, &window, &renderer))
    {
        std::cerr << "Could not create window: " << SDL_GetError() << '\n';
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderLogicalPresentation(renderer, kCanvasWidth, kCanvasHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, kCanvasWidth, kCanvasHeight);
    if (texture == nullptr)
    {
        std::cerr << "Could not create screen texture: " << SDL_GetError() << '\n';
        return false;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    const bool smokeTest = argc > 1 && std::string(argv[1]) == "--smoke-test";
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    if (!initializeSdl(window, renderer, texture, smokeTest))
    {
        if (texture != nullptr) SDL_DestroyTexture(texture);
        if (renderer != nullptr) SDL_DestroyRenderer(renderer);
        if (window != nullptr) SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    bool mouseCaptured = !smokeTest;
    if (mouseCaptured) SDL_SetWindowRelativeMouseMode(window, true);
    Player player;
    std::vector<Target> targets = {
        {12.5f, 8.0f}, {13.5f, 4.5f}, {5.5f, 3.5f},
        {3.5f, 13.5f}, {12.5f, 13.5f},
    };
    WeaponAnimation weapon;
    Canvas canvas;
    std::string message = "CLEAR EVERY TARGET";
    float messageTime = 3.0f;
    int shots = 0;
    bool running = true;
    int renderedFrames = 0;
    std::uint64_t previousTime = SDL_GetTicksNS();

    while (running)
    {
        const std::uint64_t frameTime = SDL_GetTicksNS();
        const float elapsed = std::min((frameTime - previousTime) / 1'000'000'000.0f, 0.05f);
        previousTime = frameTime;
        Input input;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
            {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
                else if (event.key.scancode == SDL_SCANCODE_SPACE) input.shoot = true;
                else if (event.key.scancode == SDL_SCANCODE_TAB)
                {
                    mouseCaptured = !mouseCaptured;
                    SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (mouseCaptured) input.shoot = true;
                else
                {
                    mouseCaptured = true;
                    SDL_SetWindowRelativeMouseMode(window, true);
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured)
                input.mouseTurn += event.motion.xrel;
        }

        const bool* keys = SDL_GetKeyboardState(nullptr);
        input.forward = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? 1.0f : 0.0f) -
                        (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);
        input.strafe = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) -
                       (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
        input.turn = (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) -
                     (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);

        updatePlayer(player, input, elapsed);
        updateWeapon(weapon, input, elapsed);
        messageTime = std::max(0.0f, messageTime - elapsed);
        if (input.shoot)
        {
            ++shots;
            const bool hit = fireShot(player, targets);
            message = hit ? "HIT" : "MISS";
            messageTime = 0.7f;
            weapon.recoilTime = 0.13f;
            weapon.flashTime = 0.065f;
        }

        canvas.clear(rgb(7, 10, 18));
        const std::vector<float> depth = drawWorld(canvas, player);
        drawTargets(canvas, player, targets, depth);
        drawCrosshair(canvas);
        drawWeapon(canvas, weapon);
        const std::vector<GridPoint> route = findRoute(player, targets);
        drawInterface(canvas, player, targets, route, shots, message, messageTime,
                      depth[kViewWidth / 2], mouseCaptured);

        SDL_UpdateTexture(texture, nullptr, canvas.pixels.data(),
                          kCanvasWidth * static_cast<int>(sizeof(std::uint32_t)));
        SDL_SetRenderDrawColor(renderer, 3, 5, 9, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        if (smokeTest && ++renderedFrames >= 3)
            running = false;
    }

    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
