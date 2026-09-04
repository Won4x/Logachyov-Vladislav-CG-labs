// main.cpp
#include "CubeDemoApp.h"


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
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
