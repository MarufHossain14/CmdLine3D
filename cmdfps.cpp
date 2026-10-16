#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <queue>
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
constexpr float kTurnSpeed = 1.45f;
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
    float forwardSpeed = 0.0f;
    float strafeSpeed = 0.0f;
    float turnSpeed = 0.0f;
};

struct Target
{
    float x;
    float y;
    bool alive = true;
};

struct WeaponAnimation
{
    float bobPhase = 0.0f;
    float recoilTime = 0.0f;
    float flashTime = 0.0f;
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
        std::cout << "\x1b[?1049h\x1b[2J\x1b[H\x1b[?25l";
#ifndef _WIN32
        // Terminals supporting the Kitty protocol now report key releases.
        // Others safely ignore this and use the timed legacy fallback below.
        std::cout << "\x1b[>11u";
#endif
        std::cout << std::flush;
    }

    ~Terminal()
    {
        if (!active_)
            return;

#ifdef _WIN32
        SetConsoleMode(output_, oldOutputMode_);
#else
        std::cout << "\x1b[<u";
        tcsetattr(STDIN_FILENO, TCSANOW, &oldTerminal_);
        fcntl(STDIN_FILENO, F_SETFL, oldFlags_);
#endif
        std::cout << "\x1b[?25h\x1b[0m\x1b[?1049l" << std::flush;
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
        char key = 0;
        while (read(STDIN_FILENO, &key, 1) > 0)
            inputBuffer_.push_back(key);

        parseInputBuffer();
        const auto now = std::chrono::steady_clock::now();
        input.forward = forwardDown_ || now < legacyUntil_[0];
        input.backward = backwardDown_ || now < legacyUntil_[1];
        input.strafeLeft = strafeLeftDown_ || now < legacyUntil_[2];
        input.strafeRight = strafeRightDown_ || now < legacyUntil_[3];
        input.turnLeft = turnLeftDown_ || now < legacyUntil_[4];
        input.turnRight = turnRightDown_ || now < legacyUntil_[5];
        input.shoot = pendingShoot_;
        input.quit = pendingQuit_;
        pendingShoot_ = false;
        pendingQuit_ = false;
#endif
        return input;
    }

    void present(const std::vector<char>& screen)
    {
        std::string frame;
        frame.reserve((kScreenWidth + 1) * kScreenHeight + 256);
        // Synchronized output prevents a half-drawn frame from flashing onscreen.
        frame += "\x1b[?2026h\x1b[H";
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
            if (y + 1 < kScreenHeight)
                frame.push_back('\n');
        }
        frame += "\x1b[?2026l";
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
        constexpr int white = 6;

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
                if (pixel == '*') return yellow;
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
        const int crosshairY = kViewTop + kViewHeight / 2;
        if (std::abs(x - kViewWidth / 2) <= 2 && std::abs(y - crosshairY) <= 1 &&
            pixel != ' ')
            return yellow;
        const bool insideWeapon = y >= kScreenHeight - 11 &&
                                  std::abs(x - kViewWidth / 2) <= 10;
        if (insideWeapon && pixel == '*')
            return yellow;
        if (insideWeapon && pixel != ' ' && pixel != '.' && pixel != ',' && pixel != ':')
            return white;
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
        case 6: return "\x1b[97m"; // Bright white weapon silhouette.
        default: return "\x1b[0m";
        }
    }

#ifdef _WIN32
    static bool isKeyDown(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }
    HANDLE output_ = INVALID_HANDLE_VALUE;
    DWORD oldOutputMode_ = 0;
    bool shootWasDown_ = false;
#else
    void setLegacyKey(int action)
    {
        // Legacy terminals have no key-up event. A short grace period plus
        // velocity easing keeps auto-repeat from producing visible jolts.
        const int graceMilliseconds = action >= 4 ? 80 : 180;
        legacyUntil_[action] = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(graceMilliseconds);
    }

    void setEnhancedKey(int keyCode, bool down, bool initialPress)
    {
        bool* state = nullptr;
        if (keyCode == 'w') state = &forwardDown_;
        else if (keyCode == 's') state = &backwardDown_;
        else if (keyCode == 'a') state = &strafeLeftDown_;
        else if (keyCode == 'd') state = &strafeRightDown_;
        else if (keyCode == 'q') state = &turnLeftDown_;
        else if (keyCode == 'e') state = &turnRightDown_;
        if (state != nullptr)
            *state = down;
        else if (keyCode == ' ' && initialPress)
            pendingShoot_ = true;
        else if ((keyCode == 'x' || keyCode == 27) && initialPress)
            pendingQuit_ = true;
    }

    void handleLegacyKey(char key)
    {
        switch (key)
        {
        case 'w': case 'W': setLegacyKey(0); break;
        case 's': case 'S': setLegacyKey(1); break;
        case 'a': case 'A': setLegacyKey(2); break;
        case 'd': case 'D': setLegacyKey(3); break;
        case 'q': case 'Q': setLegacyKey(4); break;
        case 'e': case 'E': setLegacyKey(5); break;
        case ' ': pendingShoot_ = true; break;
        case 'x': case 'X': case 27: pendingQuit_ = true; break;
        default: break;
        }
    }

    static int eventTypeFrom(const std::string& body)
    {
        const std::size_t colon = body.find(':');
        if (colon == std::string::npos || colon + 1 >= body.size())
            return 1;
        return body[colon + 1] - '0';
    }

    void handleControlSequence(const std::string& sequence)
    {
        const char final = sequence.back();
        const std::string body = sequence.substr(2, sequence.size() - 3);
        const int eventType = eventTypeFrom(body);
        const bool down = eventType != 3;
        const bool initialPress = eventType == 1;

        if (final == 'u')
        {
            const int keyCode = std::atoi(body.c_str());
            setEnhancedKey(keyCode, down, initialPress);
            return;
        }

        int action = -1;
        if (final == 'A') action = 0;
        else if (final == 'B') action = 1;
        else if (final == 'D') action = 4;
        else if (final == 'C') action = 5;
        if (action < 0)
            return;

        if (body.find(':') != std::string::npos)
        {
            bool* state = action == 0 ? &forwardDown_ :
                          action == 1 ? &backwardDown_ :
                          action == 4 ? &turnLeftDown_ : &turnRightDown_;
            *state = down;
        }
        else
        {
            setLegacyKey(action);
        }
    }

    void parseInputBuffer()
    {
        while (!inputBuffer_.empty())
        {
            if (inputBuffer_[0] != 27)
            {
                handleLegacyKey(inputBuffer_[0]);
                inputBuffer_.erase(0, 1);
                continue;
            }

            if (inputBuffer_.size() == 1)
            {
                handleLegacyKey(27);
                inputBuffer_.clear();
                continue;
            }
            if (inputBuffer_[1] != '[')
            {
                handleLegacyKey(27);
                inputBuffer_.erase(0, 1);
                continue;
            }

            std::size_t end = 2;
            while (end < inputBuffer_.size() &&
                   inputBuffer_[end] != 'u' && inputBuffer_[end] != '~' &&
                   (inputBuffer_[end] < 'A' || inputBuffer_[end] > 'D'))
                ++end;
            if (end == inputBuffer_.size())
                break;

            handleControlSequence(inputBuffer_.substr(0, end + 1));
            inputBuffer_.erase(0, end + 1);
        }
    }

    termios oldTerminal_{};
    int oldFlags_ = 0;
    std::string inputBuffer_;
    std::array<std::chrono::steady_clock::time_point, 6> legacyUntil_{};
    bool forwardDown_ = false;
    bool backwardDown_ = false;
    bool strafeLeftDown_ = false;
    bool strafeRightDown_ = false;
    bool turnLeftDown_ = false;
    bool turnRightDown_ = false;
    bool pendingShoot_ = false;
    bool pendingQuit_ = false;
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

void updatePlayer(Player& player, const Input& input, float elapsed, float turnScale)
{
    float movement = (input.forward ? 1.0f : 0.0f) -
                     (input.backward ? 1.0f : 0.0f);
    float strafe = (input.strafeRight ? 1.0f : 0.0f) -
                   (input.strafeLeft ? 1.0f : 0.0f);
    const float inputLength = std::hypot(movement, strafe);
    if (inputLength > 1.0f)
    {
        movement /= inputLength;
        strafe /= inputLength;
    }

    const float turn = (input.turnRight ? 1.0f : 0.0f) -
                       (input.turnLeft ? 1.0f : 0.0f);
    const auto ease = [elapsed](float current, float target, float response) {
        const float blend = 1.0f - std::exp(-response * elapsed);
        return current + (target - current) * blend;
    };
    const float forwardTarget = movement * kMoveSpeed;
    const float strafeTarget = strafe * kStrafeSpeed;
    player.forwardSpeed = ease(
        player.forwardSpeed, forwardTarget, movement == 0.0f ? 22.0f : 12.0f);
    player.strafeSpeed = ease(
        player.strafeSpeed, strafeTarget, strafe == 0.0f ? 22.0f : 12.0f);

    // Turning is direct on purpose: releasing the key must stop the crosshair
    // immediately instead of carrying momentum past the target.
    player.turnSpeed = turn * kTurnSpeed * turnScale;
    player.angle += player.turnSpeed * elapsed;

    const float nextX = player.x +
        (std::cos(player.angle) * player.forwardSpeed -
         std::sin(player.angle) * player.strafeSpeed) * elapsed;
    const float nextY = player.y +
        (std::sin(player.angle) * player.forwardSpeed +
         std::cos(player.angle) * player.strafeSpeed) * elapsed;

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

float aimingTurnScale(const Player& player, const std::vector<Target>& targets)
{
    for (const Target& target : targets)
    {
        if (!target.alive)
            continue;

        const float distance = std::hypot(target.x - player.x, target.y - player.y);
        const float error = std::abs(wrappedAngle(
            std::atan2(target.y - player.y, target.x - player.x) - player.angle));
        if (error < 0.20f && hasClearShot(player, target, distance))
        {
            // Gradually enter fine-aim mode as the target approaches the crosshair.
            return 0.35f + 0.35f * (error / 0.20f);
        }
    }
    return 1.0f;
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

void updateWeapon(WeaponAnimation& weapon, const Player& player, float elapsed)
{
    const float movementSpeed = std::hypot(player.forwardSpeed, player.strafeSpeed);
    if (movementSpeed > 0.05f)
        weapon.bobPhase += elapsed * (5.0f + movementSpeed * 1.5f);
    weapon.recoilTime = std::max(0.0f, weapon.recoilTime - elapsed);
    weapon.flashTime = std::max(0.0f, weapon.flashTime - elapsed);
}

void drawWeapon(std::vector<char>& screen, const Player& player,
                const WeaponAnimation& weapon)
{
    const int centerX = kViewWidth / 2;
    const int crosshairY = kViewTop + kViewHeight / 2;
    drawText(screen, centerX - 2, crosshairY, "--+--");
    screen[(crosshairY - 1) * kScreenWidth + centerX] = '|';
    screen[(crosshairY + 1) * kScreenWidth + centerX] = '|';

    const float movement = std::clamp(
        std::hypot(player.forwardSpeed, player.strafeSpeed) / kMoveSpeed, 0.0f, 1.0f);
    const int bobX = static_cast<int>(std::round(std::sin(weapon.bobPhase) * movement));
    const int bobY = static_cast<int>(std::round(
        (0.5f + 0.5f * std::sin(weapon.bobPhase * 2.0f)) * movement));
    const int recoilY = weapon.recoilTime > 0.0f ?
        static_cast<int>(std::ceil(weapon.recoilTime / 0.07f)) : 0;
    const int gunX = centerX - 7 + bobX;
    const int gunY = kScreenHeight - 7 - bobY - recoilY;

    if (weapon.flashTime > 0.0f)
    {
        drawText(screen, centerX - 2 + bobX, gunY - 3, "\\ | /");
        drawText(screen, centerX - 2 + bobX, gunY - 2, "--*--");
    }

    // A compact first-person silhouette: barrel, receiver, grip, and stock.
    drawText(screen, gunX, gunY,     "      ||      ");
    drawText(screen, gunX, gunY + 1, "     /==\\     ");
    drawText(screen, gunX, gunY + 2, "  __/|  |\\__  ");
    drawText(screen, gunX, gunY + 3, " /___|==|___\\ ");
    drawText(screen, gunX, gunY + 4, "     |[]|     ");
    drawText(screen, gunX, gunY + 5, "    /____\\    ");
}

struct GridPoint
{
    int x;
    int y;
};

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

    const auto isLiveTarget = [&](int x, int y) {
        return std::any_of(targets.begin(), targets.end(), [&](const Target& target) {
            return target.alive && static_cast<int>(target.x) == x &&
                   static_cast<int>(target.y) == y;
        });
    };
    constexpr std::array<GridPoint, 4> directions = {
        GridPoint{1, 0}, GridPoint{-1, 0}, GridPoint{0, 1}, GridPoint{0, -1}
    };

    while (!frontier.empty() && goal < 0)
    {
        const int cell = frontier.front();
        frontier.pop();
        const int x = cell % width;
        const int y = cell / width;
        if (isLiveTarget(x, y))
        {
            goal = cell;
            break;
        }

        for (const GridPoint direction : directions)
        {
            const int nextX = x + direction.x;
            const int nextY = y + direction.y;
            if (nextX < 0 || nextY < 0 || nextX >= width || nextY >= height ||
                kMap[nextY][nextX] == '#')
                continue;
            const int next = nextY * width + nextX;
            if (parent[next] != -1)
                continue;
            parent[next] = cell;
            frontier.push(next);
        }
    }

    std::vector<GridPoint> route;
    if (goal < 0)
        return route;
    for (int cell = goal; cell != start; cell = parent[cell])
        route.push_back({cell % width, cell / width});
    std::reverse(route.begin(), route.end());
    return route;
}

std::string routeGuide(const Player& player, const std::vector<GridPoint>& route,
                       bool targetsRemain)
{
    if (!targetsRemain)
        return "AREA CLEAR!";
    if (route.empty())
        return "TARGET HERE - AIM";

    const GridPoint next = route.front();
    const float routeAngle = wrappedAngle(
        std::atan2(next.y + 0.5f - player.y, next.x + 0.5f - player.x) - player.angle);
    if (std::abs(routeAngle) < 0.30f)
        return "MOVE FORWARD";
    return routeAngle < 0.0f ? "TURN LEFT" : "TURN RIGHT";
}

void drawSidePanel(std::vector<char>& screen, const Player& player,
                   const std::vector<Target>& targets,
                   const std::vector<GridPoint>& route)
{
    for (int y = 0; y < kScreenHeight; ++y)
        screen[y * kScreenWidth + kViewWidth] = '|';

    constexpr int mapX = kPanelX + 2;
    constexpr int mapY = 3;
    drawText(screen, kPanelX + 1, 1, "NAVIGATION");
    for (int y = 0; y < static_cast<int>(kMap.size()); ++y)
        drawText(screen, mapX, mapY + y, kMap[y]);

    for (const GridPoint point : route)
        screen[(mapY + point.y) * kScreenWidth + mapX + point.x] = '*';

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

    const bool targetsRemain = std::any_of(
        targets.begin(), targets.end(), [](const Target& target) { return target.alive; });
    drawText(screen, kPanelX + 1, 20, "ROUTE TO TARGET");
    drawText(screen, kPanelX + 1, 21, routeGuide(player, route, targetsRemain));
    drawText(screen, kPanelX + 1, 22,
             std::to_string(route.size()) + " steps  * = route");
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
    WeaponAnimation weapon;
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
        updatePlayer(player, input, elapsed, aimingTurnScale(player, targets));
        updateWeapon(weapon, player, elapsed);
        messageTime = std::max(0.0f, messageTime - elapsed);

        if (input.shoot)
        {
            ++shots;
            const bool hit = fireShot(player, targets);
            message = hit ? "HIT!" : "Miss";
            messageTime = 0.8f;
            weapon.recoilTime = 0.14f;
            weapon.flashTime = 0.07f;
        }

        std::vector<char> screen(kScreenWidth * kScreenHeight, ' ');
        const std::vector<float> depthBuffer = drawView(screen, player);
        const std::vector<GridPoint> route = findRoute(player, targets);
        drawTargets(screen, player, targets, depthBuffer);
        drawSidePanel(screen, player, targets, route);
        drawWeapon(screen, player, weapon);

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
        else if (depthBuffer[kViewWidth / 2] < 0.85f)
            drawText(screen, kViewWidth / 2 - 15, 1,
                     "WALL AHEAD - BACK UP OR TURN");
        terminal.present(screen);

        std::this_thread::sleep_until(frameStart + std::chrono::milliseconds(16));
    }

    return EXIT_SUCCESS;
}
