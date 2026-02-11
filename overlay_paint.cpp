#include "overlay_paint.h"

// Include necessary headers
#include <windows.h>
#include <dwmapi.h>

// Function to handle WM_PAINT messages
void HandlePaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    HBRUSH hbr = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(hdc, &rc, hbr);
    EndPaint(hwnd, &ps);
}

// Function to handle WM_ERASEBKGND messages
LRESULT HandleEraseBackground(HWND hwnd)
{
    return 1; // Prevent flicker
}
