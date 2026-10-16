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
constexpr float kMoveSpeed = 3.0f;
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
        input.turnLeft = isKeyDown('A');
        input.turnRight = isKeyDown('D');
        const bool shootDown = isKeyDown(VK_SPACE);
        input.shoot = shootDown && !shootWasDown_;
        shootWasDown_ = shootDown;
        input.quit = isKeyDown(VK_ESCAPE) || isKeyDown('X');
#else
        char key = 0;
        while (read(STDIN_FILENO, &key, 1) > 0)
        {
            switch (key)
            {
            case 'w': case 'W': input.forward = true; break;
            case 's': case 'S': input.backward = true; break;
            case 'a': case 'A': input.turnLeft = true; break;
            case 'd': case 'D': input.turnRight = true; break;
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
        frame.reserve((kScreenWidth + 1) * kScreenHeight + 3);
        frame += "\x1b[H";
        for (int y = 0; y < kScreenHeight; ++y)
        {
            frame.append(screen.data() + y * kScreenWidth, kScreenWidth);
            frame.push_back('\n');
        }
        std::cout << frame << std::flush;
    }

private:
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

void updatePlayer(Player& player, const Input& input, float elapsed)
{
    const float turn = (input.turnRight ? 1.0f : 0.0f) -
                       (input.turnLeft ? 1.0f : 0.0f);
    player.angle += turn * kTurnSpeed * elapsed;

    const float movement = (input.forward ? 1.0f : 0.0f) -
                           (input.backward ? 1.0f : 0.0f);
    const float nextX = player.x + std::cos(player.angle) * movement * kMoveSpeed * elapsed;
    const float nextY = player.y + std::sin(player.angle) * movement * kMoveSpeed * elapsed;

    // Test each axis separately so the player naturally slides along walls.
    if (!isWall(nextX, player.y))
        player.x = nextX;
    if (!isWall(player.x, nextY))
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
    if (distance < kViewDepth * 0.35f) return 'O';
    if (distance < kViewDepth * 0.55f) return 'o';
    if (distance < kViewDepth * 0.75f) return '.';
    return ' ';
}

std::vector<float> drawView(std::vector<char>& screen, const Player& player)
{
    constexpr float rayStep = 0.02f;
    constexpr int viewTop = 1;
    constexpr int viewHeight = kScreenHeight - 2;
    std::vector<float> depthBuffer(kScreenWidth, kViewDepth);

    for (int x = 0; x < kScreenWidth; ++x)
    {
        const float rayAngle = player.angle - kFieldOfView / 2.0f +
            (static_cast<float>(x) / kScreenWidth) * kFieldOfView;
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
            static_cast<int>(viewHeight / 2.0f - viewHeight / correctedDistance),
            0, viewHeight - 1);
        const int floor = viewHeight - ceiling;

        for (int viewY = 0; viewY < viewHeight; ++viewY)
        {
            char pixel = ' ';
            if (viewY >= ceiling && viewY <= floor && distance < kViewDepth)
            {
                pixel = wallShade(distance);
            }
            else if (viewY > floor)
            {
                const float closeness = static_cast<float>(viewY - viewHeight / 2) /
                                        (viewHeight / 2.0f);
                if (closeness > 0.75f) pixel = '=';
                else if (closeness > 0.50f) pixel = '-';
                else if (closeness > 0.25f) pixel = '.';
            }
            screen[(viewTop + viewY) * kScreenWidth + x] = pixel;
        }
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
    constexpr int viewHeight = kScreenHeight - 2;

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
            (angleFromPlayer / kFieldOfView + 0.5f) * kScreenWidth);
        const int spriteHeight = std::clamp(
            static_cast<int>(viewHeight / projectedDepth), 2, viewHeight);
        const int spriteWidth = std::max(1, spriteHeight / 3);
        const int top = 1 + viewHeight / 2 - spriteHeight / 2;
        const int bottom = top + spriteHeight;

        for (int x = centerX - spriteWidth / 2; x <= centerX + spriteWidth / 2; ++x)
        {
            if (x < 0 || x >= kScreenWidth || projectedDepth >= depthBuffer[x])
                continue;

            for (int y = top; y <= bottom; ++y)
            {
                if (y <= 0 || y >= kScreenHeight - 1)
                    continue;
                const bool isHead = y < top + spriteHeight / 3;
                screen[y * kScreenWidth + x] = isHead ? '@' : 'M';
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
        if (aimError < 0.035f && distance < bestDistance &&
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
    const int centerX = kScreenWidth / 2;
    screen[(kScreenHeight / 2) * kScreenWidth + centerX] = '+';
    drawText(screen, centerX - 4, kScreenHeight - 4, "  /||\\  ");
    drawText(screen, centerX - 4, kScreenHeight - 3, " /_||_\\ ");
    drawText(screen, centerX - 4, kScreenHeight - 2, "|______|");
}

void drawMiniMap(std::vector<char>& screen, const Player& player)
{
    for (int y = 0; y < static_cast<int>(kMap.size()); ++y)
    {
        for (int x = 0; x < static_cast<int>(kMap[y].size()); ++x)
        {
            // Empty map cells are transparent so the 3D view remains visible.
            if (kMap[y][x] == '#')
                screen[(y + 2) * kScreenWidth + (x + 1)] = '#';
        }
    }

    const int playerX = 1 + static_cast<int>(player.x);
    const int playerY = 2 + static_cast<int>(player.y);
    const char direction = std::cos(player.angle) > 0.5f ? '>' :
                           std::cos(player.angle) < -0.5f ? '<' :
                           std::sin(player.angle) > 0.0f ? 'v' : '^';
    screen[playerY * kScreenWidth + playerX] = direction;
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
        {12.5f, 8.5f},
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
        drawMiniMap(screen, player);
        drawWeapon(screen);

        const int targetsLeft = static_cast<int>(std::count_if(
            targets.begin(), targets.end(), [](const Target& target) { return target.alive; }));
        drawText(screen, 1, 0,
                 "CMDLINE3D  |  W/S move  A/D turn  Space shoot  X/Esc quit");
        drawText(screen, 1, kScreenHeight - 1,
                 "Targets: " + std::to_string(targetsLeft) +
                 "  Shots: " + std::to_string(shots));
        if (targetsLeft == 0)
            drawText(screen, kScreenWidth / 2 - 10, 2, "YOU CLEARED THE MAP!");
        else if (messageTime > 0.0f)
            drawText(screen, kScreenWidth / 2 - static_cast<int>(message.size()) / 2,
                     2, message);
        terminal.present(screen);

        std::this_thread::sleep_until(frameStart + std::chrono::milliseconds(16));
    }

    return EXIT_SUCCESS;
}
