#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace
{
constexpr int kScreenWidth = 100;
constexpr int kScreenHeight = 32;
constexpr int kViewWidth = 76;
constexpr int kPanelX = kViewWidth + 1;
constexpr int kViewTop = 2;
constexpr int kViewHeight = kScreenHeight - kViewTop - 1;
constexpr float kMoveSpeed = 3.0f;
constexpr float kStrafeSpeed = 2.5f;
constexpr float kTurnSpeed = 2.0f;
constexpr float kFieldOfView = 3.14159265f / 3.0f;
constexpr float kViewDepth = 16.0f;

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
    bool forward = false;
    bool backward = false;
    bool strafeLeft = false;
    bool strafeRight = false;
    bool turnLeft = false;
    bool turnRight = false;
    bool shoot = false;
    bool quit = false;
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

// Owns the small amount of platform-specific terminal setup used by the game.
class Terminal
{
public:
    Terminal()
    {
#ifdef _WIN32
        output_ = GetStdHandle(STD_OUTPUT_HANDLE);
        if (output_ == INVALID_HANDLE_VALUE || !GetConsoleMode(output_, &oldOutputMode_))
            return;
        if (!SetConsoleMode(output_, oldOutputMode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            return;
        active_ = true;
#else
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
            return;
        if (tcgetattr(STDIN_FILENO, &oldTerminal_) != 0)
            return;

        termios raw = oldTerminal_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
            return;

        oldFlags_ = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (oldFlags_ == -1 || fcntl(STDIN_FILENO, F_SETFL, oldFlags_ | O_NONBLOCK) == -1)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldTerminal_);
            return;
        }
        active_ = true;
#endif
        std::cout << "\x1b[2J\x1b[H\x1b[?25l" << std::flush;
    }

    ~Terminal()
    {
        if (!active_)
            return;

#ifdef _WIN32
        SetConsoleMode(output_, oldOutputMode_);
#else
        tcsetattr(STDIN_FILENO, TCSANOW, &oldTerminal_);
        fcntl(STDIN_FILENO, F_SETFL, oldFlags_);
#endif
        std::cout << "\x1b[?25h\x1b[0m\n" << std::flush;
    }

    bool isActive() const { return active_; }

    Input readInput()
    {
        Input input;
#ifdef _WIN32
        input.forward = isKeyDown('W');
        input.backward = isKeyDown('S');
        input.strafeLeft = isKeyDown('A');
        input.strafeRight = isKeyDown('D');
        input.turnLeft = isKeyDown('Q') || isKeyDown(VK_LEFT);
        input.turnRight = isKeyDown('E') || isKeyDown(VK_RIGHT);
        const bool shootDown = isKeyDown(VK_SPACE);
        input.shoot = shootDown && !shootWasDown_;
        shootWasDown_ = shootDown;
        input.quit = isKeyDown(VK_ESCAPE) || isKeyDown('X');
#else
        std::string keys;
        char key = 0;
        while (read(STDIN_FILENO, &key, 1) > 0)
            keys.push_back(key);

        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            // Arrow keys arrive as a three-character escape sequence.
            if (keys[i] == 27 && i + 2 < keys.size() && keys[i + 1] == '[')
            {
                if (keys[i + 2] == 'D') input.turnLeft = true;
                else if (keys[i + 2] == 'C') input.turnRight = true;
                else if (keys[i + 2] == 'A') input.forward = true;
                else if (keys[i + 2] == 'B') input.backward = true;
                i += 2;
                continue;
            }

            switch (keys[i])
            {
            case 'w': case 'W': input.forward = true; break;
            case 's': case 'S': input.backward = true; break;
            case 'a': case 'A': input.strafeLeft = true; break;
            case 'd': case 'D': input.strafeRight = true; break;
            case 'q': case 'Q': input.turnLeft = true; break;
            case 'e': case 'E': input.turnRight = true; break;
            case ' ': input.shoot = true; break;
            case 'x': case 'X': case 27: input.quit = true; break;
            default: break;
            }
        }
#endif
        return input;
    }

    void present(const std::vector<char>& screen)
    {
        std::string frame;
        frame.reserve((kScreenWidth + 1) * kScreenHeight + 256);
        frame += "\x1b[H";
        for (int y = 0; y < kScreenHeight; ++y)
        {
            int activeColor = -1;
            for (int x = 0; x < kScreenWidth; ++x)
            {
                const char pixel = screen[y * kScreenWidth + x];
                const int color = cellColor(pixel, x, y);
                if (color != activeColor)
                {
                    frame += colorCode(color);
                    activeColor = color;
                }
                frame.push_back(pixel);
            }
            frame += "\x1b[0m";
            frame.push_back('\n');
        }
        std::cout << frame << std::flush;
    }

private:
    static int cellColor(char pixel, int x, int y)
    {
        constexpr int normal = 0;
        constexpr int dim = 1;
        constexpr int cyan = 2;
        constexpr int red = 3;
        constexpr int yellow = 4;
        constexpr int green = 5;

        if (x == kViewWidth)
            return cyan;
        if (y == 0 || y == kScreenHeight - 1)
            return green;

        if (x > kViewWidth)
        {
            const bool insideMap = y >= 3 && y < 3 + static_cast<int>(kMap.size()) &&
                                   x >= kPanelX + 2 && x < kPanelX + 18;
            if (insideMap)
            {
                if (pixel == 'T') return red;
                if (pixel == 'x') return green;
                if (pixel == '<' || pixel == '>' || pixel == '^' || pixel == 'v') return yellow;
                if (pixel == '#') return cyan;
                if (pixel == '.') return dim;
            }
            if (y == 1 || y == 20 || y == 21)
                return yellow;
            return normal;
        }

        if (pixel == 'O' || pixel == 'H')
            return red;
        if (x == kViewWidth / 2 && y == kViewTop + kViewHeight / 2)
            return yellow;
        if (y >= kViewTop && (pixel == '#' || pixel == 'X' || pixel == '+' || pixel == '|'))
            return cyan;
        if (pixel == '.' || pixel == ',' || pixel == ':')
            return dim;
        if (y == 1)
            return yellow;
        return normal;
    }

    static const char* colorCode(int color)
    {
        switch (color)
        {
        case 1: return "\x1b[2m";  // Dim floor and empty map cells.
        case 2: return "\x1b[36m"; // Cyan walls.
        case 3: return "\x1b[91m"; // Bright red targets.
        case 4: return "\x1b[93m"; // Yellow navigation information.
        case 5: return "\x1b[92m"; // Green status and cleared targets.
        default: return "\x1b[0m";
        }
    }

#ifdef _WIN32
    static bool isKeyDown(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }
    HANDLE output_ = INVALID_HANDLE_VALUE;
    DWORD oldOutputMode_ = 0;
    bool shootWasDown_ = false;
#else
    termios oldTerminal_{};
    int oldFlags_ = 0;
#endif
    bool active_ = false;
};

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
    constexpr float playerRadius = 0.18f;
    return !isWall(x - playerRadius, y - playerRadius) &&
           !isWall(x + playerRadius, y - playerRadius) &&
           !isWall(x - playerRadius, y + playerRadius) &&
           !isWall(x + playerRadius, y + playerRadius);
}

void updatePlayer(Player& player, const Input& input, float elapsed)
{
    const float turn = (input.turnRight ? 1.0f : 0.0f) -
                       (input.turnLeft ? 1.0f : 0.0f);
    player.angle += turn * kTurnSpeed * elapsed;

    const float movement = (input.forward ? 1.0f : 0.0f) -
                           (input.backward ? 1.0f : 0.0f);
    const float strafe = (input.strafeRight ? 1.0f : 0.0f) -
                         (input.strafeLeft ? 1.0f : 0.0f);
    const float nextX = player.x +
        (std::cos(player.angle) * movement * kMoveSpeed -
         std::sin(player.angle) * strafe * kStrafeSpeed) * elapsed;
    const float nextY = player.y +
        (std::sin(player.angle) * movement * kMoveSpeed +
         std::cos(player.angle) * strafe * kStrafeSpeed) * elapsed;

    // Test each axis separately so the player naturally slides along walls.
    if (canStandAt(nextX, player.y))
        player.x = nextX;
    if (canStandAt(player.x, nextY))
        player.y = nextY;
}

void drawText(std::vector<char>& screen, int x, int y, const std::string& text)
{
    if (y < 0 || y >= kScreenHeight)
        return;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const int screenX = x + static_cast<int>(i);
        if (screenX >= 0 && screenX < kScreenWidth)
            screen[y * kScreenWidth + screenX] = text[i];
    }
}

char wallShade(float distance)
{
    if (distance < kViewDepth * 0.20f) return '#';
    if (distance < kViewDepth * 0.35f) return 'X';
    if (distance < kViewDepth * 0.55f) return '+';
    if (distance < kViewDepth * 0.75f) return '.';
    return ' ';
}

std::vector<float> drawView(std::vector<char>& screen, const Player& player)
{
    constexpr float rayStep = 0.02f;
    std::vector<float> depthBuffer(kViewWidth, kViewDepth);
    std::vector<int> wallTops(kViewWidth, 0);
    std::vector<int> wallBottoms(kViewWidth, 0);

    for (int x = 0; x < kViewWidth; ++x)
    {
        const float rayAngle = player.angle - kFieldOfView / 2.0f +
            (static_cast<float>(x) / kViewWidth) * kFieldOfView;
        const float eyeX = std::cos(rayAngle);
        const float eyeY = std::sin(rayAngle);

        float distance = 0.0f;
        while (distance < kViewDepth &&
               !isWall(player.x + eyeX * distance, player.y + eyeY * distance))
        {
            distance += rayStep;
        }

        // Correct the curved "fish-eye" effect caused by measuring along each ray.
        const float correctedDistance = std::max(
            distance * std::cos(rayAngle - player.angle), 0.1f);
        depthBuffer[x] = correctedDistance;
        const int ceiling = std::clamp(
            static_cast<int>(kViewHeight / 2.0f - kViewHeight / correctedDistance),
            0, kViewHeight - 1);
        const int floor = kViewHeight - ceiling;
        wallTops[x] = ceiling;
        wallBottoms[x] = floor;

        for (int viewY = 0; viewY < kViewHeight; ++viewY)
        {
            char pixel = ' ';
            if (viewY >= ceiling && viewY <= floor && distance < kViewDepth)
            {
                pixel = wallShade(correctedDistance);
            }
            else if (viewY > floor)
            {
                const float closeness = static_cast<float>(viewY - kViewHeight / 2) /
                                        (kViewHeight / 2.0f);
                if (closeness > 0.75f) pixel = ':';
                else if (closeness > 0.50f) pixel = ',';
                else if (closeness > 0.25f) pixel = '.';
            }
            screen[(kViewTop + viewY) * kScreenWidth + x] = pixel;
        }
    }

    // Strong vertical edges make corners and doorways much easier to read.
    for (int x = 1; x < kViewWidth; ++x)
    {
        if (std::abs(depthBuffer[x] - depthBuffer[x - 1]) < 0.45f)
            continue;
        for (int y = wallTops[x]; y <= wallBottoms[x]; ++y)
            if (y >= 0 && y < kViewHeight)
                screen[(kViewTop + y) * kScreenWidth + x] = '|';
    }

    return depthBuffer;
}

float wrappedAngle(float angle)
{
    while (angle > 3.14159265f) angle -= 2.0f * 3.14159265f;
    while (angle < -3.14159265f) angle += 2.0f * 3.14159265f;
    return angle;
}

void drawTargets(std::vector<char>& screen, const Player& player,
                 const std::vector<Target>& targets,
                 const std::vector<float>& depthBuffer)
{
    // Draw far targets first so a closer target naturally appears in front.
    std::vector<const Target*> visibleTargets;
    for (const Target& target : targets)
        if (target.alive)
            visibleTargets.push_back(&target);

    std::sort(visibleTargets.begin(), visibleTargets.end(), [&](const Target* left, const Target* right) {
        return std::hypot(left->x - player.x, left->y - player.y) >
               std::hypot(right->x - player.x, right->y - player.y);
    });

    for (const Target* target : visibleTargets)
    {
        const float offsetX = target->x - player.x;
        const float offsetY = target->y - player.y;
        const float distance = std::hypot(offsetX, offsetY);
        const float angleFromPlayer = wrappedAngle(std::atan2(offsetY, offsetX) - player.angle);
        if (std::abs(angleFromPlayer) > kFieldOfView * 0.6f || distance < 0.2f)
            continue;

        const float projectedDepth = distance * std::cos(angleFromPlayer);
        const int centerX = static_cast<int>(
            (angleFromPlayer / kFieldOfView + 0.5f) * kViewWidth);
        const int spriteHeight = std::clamp(
            static_cast<int>(kViewHeight / projectedDepth), 3, kViewHeight);
        const int spriteWidth = std::max(1, spriteHeight / 3);
        const int top = kViewTop + kViewHeight / 2 - spriteHeight / 2;
        const int bottom = top + spriteHeight;

        for (int x = centerX - spriteWidth / 2; x <= centerX + spriteWidth / 2; ++x)
        {
            if (x < 0 || x >= kViewWidth || projectedDepth >= depthBuffer[x])
                continue;

            for (int y = top; y <= bottom; ++y)
            {
                if (y < kViewTop || y >= kViewTop + kViewHeight)
                    continue;
                const bool isHead = y < top + spriteHeight / 3;
                screen[y * kScreenWidth + x] = isHead ? 'O' : 'H';
            }
        }
    }
}

bool hasClearShot(const Player& player, const Target& target, float distance)
{
    const float directionX = (target.x - player.x) / distance;
    const float directionY = (target.y - player.y) / distance;
    for (float step = 0.1f; step < distance; step += 0.05f)
        if (isWall(player.x + directionX * step, player.y + directionY * step))
            return false;
    return true;
}

bool fireShot(const Player& player, std::vector<Target>& targets)
{
    Target* bestTarget = nullptr;
    float bestDistance = kViewDepth;

    for (Target& target : targets)
    {
        if (!target.alive)
            continue;

        const float offsetX = target.x - player.x;
        const float offsetY = target.y - player.y;
        const float distance = std::hypot(offsetX, offsetY);
        const float aimError = std::abs(wrappedAngle(
            std::atan2(offsetY, offsetX) - player.angle));

        // The small angular allowance makes the center crosshair the hit area.
        if (aimError < 0.055f && distance < bestDistance &&
            hasClearShot(player, target, distance))
        {
            bestTarget = &target;
            bestDistance = distance;
        }
    }

    if (bestTarget == nullptr)
        return false;

    bestTarget->alive = false;
    return true;
}

void drawWeapon(std::vector<char>& screen)
{
    const int centerX = kViewWidth / 2;
    screen[(kViewTop + kViewHeight / 2) * kScreenWidth + centerX] = '+';
    drawText(screen, centerX - 4, kScreenHeight - 4, "  /||\\  ");
    drawText(screen, centerX - 4, kScreenHeight - 3, " /_||_\\ ");
    drawText(screen, centerX - 4, kScreenHeight - 2, "|______|");
}

std::string targetGuide(const Player& player, const std::vector<Target>& targets)
{
    const Target* nearest = nullptr;
    float nearestDistance = kViewDepth;
    for (const Target& target : targets)
    {
        if (!target.alive)
            continue;
        const float distance = std::hypot(target.x - player.x, target.y - player.y);
        if (distance < nearestDistance)
        {
            nearest = &target;
            nearestDistance = distance;
        }
    }

    if (nearest == nullptr)
        return "AREA CLEAR!";

    const float targetAngle = wrappedAngle(
        std::atan2(nearest->y - player.y, nearest->x - player.x) - player.angle);
    const std::string direction = std::abs(targetAngle) < 0.15f ? "AHEAD" :
                                  targetAngle < 0.0f ? "TURN LEFT" : "TURN RIGHT";
    return direction + "  " + std::to_string(static_cast<int>(nearestDistance + 0.5f)) + "m";
}

void drawSidePanel(std::vector<char>& screen, const Player& player,
                   const std::vector<Target>& targets)
{
    for (int y = 0; y < kScreenHeight; ++y)
        screen[y * kScreenWidth + kViewWidth] = '|';

    constexpr int mapX = kPanelX + 2;
    constexpr int mapY = 3;
    drawText(screen, kPanelX + 1, 1, "NAVIGATION");
    for (int y = 0; y < static_cast<int>(kMap.size()); ++y)
        drawText(screen, mapX, mapY + y, kMap[y]);

    for (const Target& target : targets)
    {
        const int targetX = mapX + static_cast<int>(target.x);
        const int targetY = mapY + static_cast<int>(target.y);
        screen[targetY * kScreenWidth + targetX] = target.alive ? 'T' : 'x';
    }

    const int playerX = mapX + static_cast<int>(player.x);
    const int playerY = mapY + static_cast<int>(player.y);
    const char direction = std::cos(player.angle) > 0.5f ? '>' :
                           std::cos(player.angle) < -0.5f ? '<' :
                           std::sin(player.angle) > 0.0f ? 'v' : '^';
    screen[playerY * kScreenWidth + playerX] = direction;

    drawText(screen, kPanelX + 1, 20, "NEAREST TARGET");
    drawText(screen, kPanelX + 1, 21, targetGuide(player, targets));
    drawText(screen, kPanelX + 1, 23, "W/S   forward/back");
    drawText(screen, kPanelX + 1, 24, "A/D   strafe");
    drawText(screen, kPanelX + 1, 25, "Q/E   turn");
    drawText(screen, kPanelX + 1, 26, "Arrows move/turn");
    drawText(screen, kPanelX + 1, 27, "Space shoot");
    drawText(screen, kPanelX + 1, 28, "X/Esc quit");
    drawText(screen, kPanelX + 1, 30, "T target  x cleared");
}
} // namespace

int main()
{
    Terminal terminal;
    if (!terminal.isActive())
    {
        std::cerr << "CmdLine3D needs to run in an interactive terminal.\n";
        return EXIT_FAILURE;
    }

    Player player;
    std::vector<Target> targets = {
        {12.5f, 8.0f},
        {13.5f, 4.5f},
        {5.5f, 3.5f},
        {3.5f, 13.5f},
        {12.5f, 13.5f},
    };
    int shots = 0;
    std::string message = "Find and hit every target";
    float messageTime = 3.0f;
    bool running = true;
    auto previousTime = std::chrono::steady_clock::now();

    while (running)
    {
        const auto frameStart = std::chrono::steady_clock::now();
        const float elapsed = std::min(
            std::chrono::duration<float>(frameStart - previousTime).count(), 0.1f);
        previousTime = frameStart;

        const Input input = terminal.readInput();
        running = !input.quit;
        updatePlayer(player, input, elapsed);
        messageTime = std::max(0.0f, messageTime - elapsed);

        if (input.shoot)
        {
            ++shots;
            const bool hit = fireShot(player, targets);
            message = hit ? "HIT!" : "Miss";
            messageTime = 0.8f;
        }

        std::vector<char> screen(kScreenWidth * kScreenHeight, ' ');
        const std::vector<float> depthBuffer = drawView(screen, player);
        drawTargets(screen, player, targets, depthBuffer);
        drawSidePanel(screen, player, targets);
        drawWeapon(screen);

        const int targetsLeft = static_cast<int>(std::count_if(
            targets.begin(), targets.end(), [](const Target& target) { return target.alive; }));
        drawText(screen, 1, 0,
                 "CMDLINE3D  //  CLEAR EVERY TARGET");
        drawText(screen, 1, kScreenHeight - 1,
                 " TARGETS " + std::to_string(targetsLeft) +
                 "   SHOTS " + std::to_string(shots));
        if (targetsLeft == 0)
            drawText(screen, kViewWidth / 2 - 10, 1, "*** AREA CLEARED! ***");
        else if (messageTime > 0.0f)
            drawText(screen, kViewWidth / 2 - static_cast<int>(message.size()) / 2,
                     1, message);
        terminal.present(screen);

        std::this_thread::sleep_until(frameStart + std::chrono::milliseconds(16));
    }

    return EXIT_SUCCESS;
}
