#include <iostream>
#include <string>
#include <windows.h>

using namespace std;

/*
map = the world
screen = what you draw each frame
hConsole = where you display the screen
*/

int nScreenWidth = 120;
int nScreenHeight = 40;
int nMapWidth = 16;
int nMapHeight = 16;

float fPlayerX = 14.7f;
float fPlayerY = 5.09f;
float fPlayerA = 0.0f;
float fFOV = 3.14159f / 4.0f;
float fDepth = 16.0f;
float fSpeed = 5.0f;

int main()
{
    wstring map;
    map += L"#########.......";
    map += L"#...............";
    map += L"#.......########";
    map += L"#..............#";
    map += L"#......##......#";
    map += L"#......##......#";
    map += L"#..............#";
    map += L"###............#";
    map += L"##.............#";
    map += L"#......####..###";
    map += L"#......#.......#";
    map += L"#......#.......#";
    map += L"#..............#";
    map += L"#......#########";
    map += L"#..............#";
    map += L"################";

    wchar_t* screen = new wchar_t[nScreenWidth * nScreenHeight];

    HANDLE hConsole = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CONSOLE_TEXTMODE_BUFFER,
        NULL
    );

    SetConsoleActiveScreenBuffer(hConsole);

    DWORD dwBytesWritten = 0;

    while (true)
    {
        for (int i = 0; i < nScreenWidth * nScreenHeight; i++)
            screen[i] = L' ';

        swprintf_s(screen, 40, L"Hello Raycaster!");

        for (int x = 0; x < nMapWidth; x++)
        {
            for (int y = 0; y < nMapHeight; y++)
            {
                screen[(y + 1) * nScreenWidth + x] =
                    map[y * nMapWidth + x];
            }
        }

        screen[nScreenWidth * nScreenHeight - 1] = L'\0';

        WriteConsoleOutputCharacter(
            hConsole,
            screen,
            nScreenWidth * nScreenHeight,
            { 0, 0 },
            &dwBytesWritten
        );
    }

    delete[] screen;
    CloseHandle(hConsole);

    return 0;
}
