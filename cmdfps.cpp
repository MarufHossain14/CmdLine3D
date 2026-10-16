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
    bool quit = false;
};

struct Player
{
    float x = 8.0f;
    float y = 8.0f;
    float angle = 0.0f;
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

void drawMap(std::vector<char>& screen, const Player& player)
{
    for (int y = 0; y < static_cast<int>(kMap.size()); ++y)
        drawText(screen, 2, y + 3, kMap[y]);

    const int playerX = 2 + static_cast<int>(player.x);
    const int playerY = 3 + static_cast<int>(player.y);
    screen[playerY * kScreenWidth + playerX] = 'P';
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

        std::vector<char> screen(kScreenWidth * kScreenHeight, ' ');
        drawText(screen, 2, 1, "CMDLINE3D  |  W/S move  A/D turn  X or Esc quit");
        drawMap(screen, player);
        terminal.present(screen);

        std::this_thread::sleep_until(frameStart + std::chrono::milliseconds(16));
    }

    return EXIT_SUCCESS;
}
