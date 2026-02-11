#ifndef OVERLAY_PAINT_H
#define OVERLAY_PAINT_H

#include <windows.h>

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

#endif // OVERLAY_PAINT_H
