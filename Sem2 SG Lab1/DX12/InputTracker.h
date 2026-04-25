#pragma once
#include <Windows.h>
#include <array>

class InputDevice
{
public:
    void Initialize(HWND hwnd);
    void BeginFrame();                
    void ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    
    bool IsKeyDown(uint8_t vk) const;
    bool WasKeyPressed(uint8_t vk) const;   
    bool WasKeyReleased(uint8_t vk) const;  

    
    POINT MousePos() const { return mMousePos; }
    POINT MouseDelta() const { return mMouseDelta; }
    int   WheelDelta() const { return mWheelDelta; }

    bool IsMouseDown(int button) const;     
    bool WasMousePressed(int button) const;

private:
    HWND mHwnd = nullptr;

    std::array<uint8_t, 256> mCurrKeys{};
    std::array<uint8_t, 256> mPrevKeys{};

    uint8_t mCurrMouse[3]{};
    uint8_t mPrevMouse[3]{};

    POINT mMousePos{ 0,0 };
    POINT mPrevMousePos{ 0,0 };
    POINT mMouseDelta{ 0,0 };

    int mWheelDelta = 0;

private:
    static uint8_t ToByteDown(bool down) { return down ? 0x80 : 0x00; }
};
