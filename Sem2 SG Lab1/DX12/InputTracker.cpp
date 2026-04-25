#include "InputTracker.h"
#include <windowsx.h>
void InputDevice::Initialize(HWND hwnd)
{
    mHwnd = hwnd;
    GetCursorPos(&mMousePos);
    ScreenToClient(mHwnd, &mMousePos);
    mPrevMousePos = mMousePos;
}

void InputDevice::BeginFrame()
{
    mPrevKeys = mCurrKeys;
    mPrevMouse[0] = mCurrMouse[0];
    mPrevMouse[1] = mCurrMouse[1];
    mPrevMouse[2] = mCurrMouse[2];

    
    mWheelDelta = 0;

    
    mMouseDelta.x = mMousePos.x - mPrevMousePos.x;
    mMouseDelta.y = mMousePos.y - mPrevMousePos.y;
    mPrevMousePos = mMousePos;

   
    for (int vk = 0; vk < 256; vk++)
        mCurrKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) ? 0x80 : 0x00;
}

void InputDevice::ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
    {
        mMousePos.x = GET_X_LPARAM(lParam);
        mMousePos.y = GET_Y_LPARAM(lParam);
    } break;

    case WM_LBUTTONDOWN: mCurrMouse[0] = 0x80; SetCapture(mHwnd); break;
    case WM_LBUTTONUP:   mCurrMouse[0] = 0x00; ReleaseCapture();  break;

    case WM_RBUTTONDOWN: mCurrMouse[1] = 0x80; SetCapture(mHwnd); break;
    case WM_RBUTTONUP:   mCurrMouse[1] = 0x00; ReleaseCapture();  break;

    case WM_MBUTTONDOWN: mCurrMouse[2] = 0x80; SetCapture(mHwnd); break;
    case WM_MBUTTONUP:   mCurrMouse[2] = 0x00; ReleaseCapture();  break;

    case WM_MOUSEWHEEL:
    {
        mWheelDelta += GET_WHEEL_DELTA_WPARAM(wParam); 
    } break;

    default: break;
    }
}

bool InputDevice::IsKeyDown(uint8_t vk) const
{
    return (mCurrKeys[vk] & 0x80) != 0;
}

bool InputDevice::WasKeyPressed(uint8_t vk) const
{
    return (mCurrKeys[vk] & 0x80) && !(mPrevKeys[vk] & 0x80);
}

bool InputDevice::WasKeyReleased(uint8_t vk) const
{
    return !(mCurrKeys[vk] & 0x80) && (mPrevKeys[vk] & 0x80);
}

bool InputDevice::IsMouseDown(int button) const
{
    return (mCurrMouse[button] & 0x80) != 0;
}

bool InputDevice::WasMousePressed(int button) const
{
    return (mCurrMouse[button] & 0x80) && !(mPrevMouse[button] & 0x80);
}
