// CubeDemoApp.h
#pragma once
#include "RenderApp.h"
#include "GeneratedCubeRenderer.h"

class CubeApp : public D3DApp
{
public:
    CubeApp(HINSTANCE hInstance);
    virtual ~CubeApp();

    virtual bool Initialize() override;

    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

private:
    std::unique_ptr<CubeRenderer> mCube;
};

