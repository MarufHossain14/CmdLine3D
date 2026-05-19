#include <iostream>
#include <string>
#include <windows.h>

using namespace std;

/*
map = the world
screen = what you draw each frame
hConsole = where you display the screen
*/

int nScreenWidth = 120;			// Console Screen Size X (columns)
int nScreenHeight = 40;			// Console Screen Size Y (rows)
int nMapWidth = 16;				// World Dimensions
int nMapHeight = 16;

float fPlayerX = 14.7f;			// Player Start Position
float fPlayerY = 5.09f;
float fPlayerA = 0.0f;			// Player Start Rotation
float fFOV = 3.14159f / 4.0f;	// Field of View
float fDepth = 16.0f;			// Maximum rendering distance
float fSpeed = 5.0f;			// Walking Speed


int main() {

  // Create Map of world space # = wall block, . = space
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


  // Create Screen Buffer
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

  swprintf_s(screen, 40, L"Hello Raycaster!");

  // Write Screen Buffer to Console
  WriteConsoleOutputCharacter(
    hConsole,
    screen,
    nScreenWidth * nScreenHeight,
    { 0, 0 },
    &dwBytesWritten
);

  // Clean up
  delete[] screen;
  CloseHandle(hConsole);

  return 0;
}
