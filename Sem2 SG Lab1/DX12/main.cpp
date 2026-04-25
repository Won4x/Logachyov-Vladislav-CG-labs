// main.cpp
#include "CubeDemoApp.h"


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, int)
{
    try
    {
        CubeApp app(hInstance);
        if (!app.Initialize())
            return 0;

        return app.Run();
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Error", MB_OK);
        return -1;
    }
}
