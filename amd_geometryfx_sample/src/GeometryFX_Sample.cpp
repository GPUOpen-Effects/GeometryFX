//
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

//--------------------------------------------------------------------------------------
// File: GeometryFX_Sample.cpp
//
//--------------------------------------------------------------------------------------

// DXUT includes
#include "DXUT.h"
#include "DXUTcamera.h"
#include "DXUTgui.h"
#include "DXUTsettingsdlg.h"
#include "SDKmisc.h"

// AMD includes
#include "AMD_LIB.h"
#include "AMD_SDK.h"

// ComPtr
#include <wrl.h>
// unique_ptr
#include <memory>
#include <vector>

#include <assimp\cimport.h>        // Plain-C interface
#include <assimp\scene.h>          // Output data structure
#include <assimp\postprocess.h>    // Post processing flags
#include <assimp\config.h>

#include <map>
#include <locale>
#include <codecvt>
#include <fstream>
#include <random>
#include <functional>

#include "AMD_GeometryFX_Filtering.h"
#include "AMD_GeometryFX_Utility.h"

// Project includes
#include "resource.h"

#pragma warning(disable : 4100) // disable unreference formal parameter warnings for /W4 builds

namespace AMD
{
    struct ShaderType
    {
        enum Enum
        {
            Vertex,
            Domain,
            Hull,
            Geometry,
            Pixel,
            Compute
        };
    };
}

std::string WideToUTF8String(const std::wstring &wstr)
{
    typedef std::codecvt_utf8<wchar_t> converterSource;
    std::wstring_convert<converterSource, wchar_t> converter;

    return converter.to_bytes(wstr);
}

template <typename T> struct DefaultHandler;

template <> struct DefaultHandler<float>
{
    static float Function(const std::string &s)
    {
        return std::stof(s);
    }
};

template <> struct DefaultHandler<int>
{
    static int Function(const std::string &s)
    {
        return std::stoi(s);
    }
};

template <> struct DefaultHandler<AMD::uint32>
{
    static AMD::uint32 Function(const std::string &s)
    {
        return std::stoul(s);
    }
};

template <> struct DefaultHandler<bool>
{
    static bool Function(const std::string &s)
    {
        return s == "true" || s == "yes";
    }
};

template <> struct DefaultHandler<std::string>
{
    static const std::string &Function(const std::string &s)
    {
        return s;
    }
};

template <typename T, typename F>
bool HandleOption(
    const std::map<std::string, std::string> &options, const char *name, T &variable, F handler)
{
    if (options.find(name) != options.end())
    {
        variable = handler(options.find(name)->second);
        return true;
    }
    else
    {
        return false;
    }
}

template <typename T>
bool HandleOption(const std::map<std::string, std::string> &options, const char *name, T &variable)
{
    return HandleOption(options, name, variable, [](const std::string &s) -> T
                                                 {
                                                     return DefaultHandler<T>::Function(s);
                                                 });
}

template <typename U, typename T> bool TestFlag(const U m, T i)
{
    return (m & static_cast<U>(i)) == static_cast<U>(i);
}

template <typename U, typename T> U SetOrClearFlag(U &m, const T i, const bool set)
{
    if (set)
    {
        m |= static_cast<U>(i);
    }
    else
    {
        m &= ~static_cast<U>(i);
    }

    return m;
}

using namespace DirectX;
using namespace Microsoft::WRL;

//--------------------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------------------
CFirstPersonCamera g_Camera;                        //
CDXUTDialogResourceManager g_DialogResourceManager; // manager for shared resources of dialogs
CD3DSettingsDlg g_SettingsDlg;                      // Device settings dialog
CDXUTTextHelper *g_pTxtHelper = NULL;

// depth buffer data
AMD::Texture2D g_depthStencilTexture;

struct RenderMode
{
    enum Enum
    {
        Default,
        Filter
    };
};

struct ResolutionDependentResources
{
    ComPtr<ID3D11DepthStencilView> depthView;
    ComPtr<ID3D11ShaderResourceView> depthShaderView;
    ComPtr<ID3D11Texture2D> depthBuffer;

    void Create(ID3D11Device *device, int width, int height, int sampleCount)
    {
        AMD::CreateDepthStencilSurface(&depthBuffer, &depthShaderView, &depthView,
            DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, width, height, sampleCount);
    }

    void Destroy()
    {
        depthView.Reset();
        depthShaderView.Reset();
        depthBuffer.Reset();
    }
};

struct FullscreenConstantBuffer
{
    AMD::uint32 windowWidth;
    AMD::uint32 windowHeight;
    AMD::uint32 shadowMapWidth;
    AMD::uint32 shadowMapHeight;
};

void CompileShader(ID3D11Device *device, ID3D11DeviceChild **ppShader, const char *sourceFile,
    AMD::ShaderType::Enum shaderType, const char *entryPoint, const int macroCount = 0,
    const D3D_SHADER_MACRO *pMacros = nullptr, ID3D11InputLayout **inputLayout = nullptr,
    const int inputElementCount = 0, const D3D11_INPUT_ELEMENT_DESC *inputElements = nullptr)
{
    const auto data = AMD::GeometryFX_ReadBlobFromFile(sourceFile);

    const char *target = nullptr;
    switch (shaderType)
    {
    case AMD::ShaderType::Compute:
        target = "cs_5_0";
        break;
    case AMD::ShaderType::Geometry:
        target = "gs_5_0";
        break;
    case AMD::ShaderType::Pixel:
        target = "ps_5_0";
        break;
    case AMD::ShaderType::Hull:
        target = "hs_5_0";
        break;
    case AMD::ShaderType::Domain:
        target = "ds_5_0";
        break;
    case AMD::ShaderType::Vertex:
        target = "vs_5_0";
        break;
    }

    ComPtr<ID3DBlob> output;

    std::vector<D3D_SHADER_MACRO> macros(pMacros, pMacros + macroCount);

    D3D_SHADER_MACRO nullMacro = { nullptr, nullptr };
    macros.push_back(nullMacro);

    D3DCompile(data.data(), data.size(), nullptr, macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, target, 0, 0, &output, nullptr);

    const size_t shaderSize  = output->GetBufferSize();
    const void *shaderSource = output->GetBufferPointer();
    if (inputLayout)
    {
        device->CreateInputLayout(
            inputElements, inputElementCount, shaderSource, shaderSize, inputLayout);
    }

    switch (shaderType)
    {
    case AMD::ShaderType::Compute:
        device->CreateComputeShader(
            shaderSource, shaderSize, nullptr, (ID3D11ComputeShader **)ppShader);
        break;
    case AMD::ShaderType::Pixel:
        device->CreatePixelShader(
            shaderSource, shaderSize, nullptr, (ID3D11PixelShader **)ppShader);
        break;
    case AMD::ShaderType::Vertex:
        device->CreateVertexShader(
            shaderSource, shaderSize, nullptr, (ID3D11VertexShader **)ppShader);
        break;
    case AMD::ShaderType::Hull:
        device->CreateHullShader(
            shaderSource, shaderSize, nullptr, (ID3D11HullShader **)ppShader);
        break;
    case AMD::ShaderType::Domain:
        device->CreateDomainShader(
            shaderSource, shaderSize, nullptr, (ID3D11DomainShader **)ppShader);
        break;
    case AMD::ShaderType::Geometry:
        device->CreateGeometryShader(
            shaderSource, shaderSize, nullptr, (ID3D11GeometryShader **)ppShader);
        break;
    }
}

void GenerateGeometryChunk(
    const int faceCount, std::vector<float> &vertices, std::vector<int> &indices)
{
    const int quadCount = faceCount / 2;
    const int rows = static_cast<int>(std::sqrt(static_cast<float>(quadCount)));
    const int fullColumns =
        static_cast<int>(std::floor(static_cast<float>(quadCount) / static_cast<float>(rows)));
    const int columns =
        static_cast<int>(std::ceil(static_cast<float>(quadCount) / static_cast<float>(rows)));

    for (int i = 0; i < fullColumns + 1; ++i)
    {
        for (int j = 0; j < rows + 1; ++j)
        {
            vertices.push_back(static_cast<float>(i));
            vertices.push_back(static_cast<float>(j));
            vertices.push_back(4 * std::sin(i * XM_PI / rows * 3) * std::cos(j * XM_PI / rows * 4));
        }
    }

    for (int i = fullColumns + 1; i < columns + 1; ++i)
    {
        for (int j = 0; j < (quadCount - fullColumns * rows + 1); ++j)
        {
            vertices.push_back(static_cast<float>(i));
            vertices.push_back(static_cast<float>(j));
            vertices.push_back(0);
        }
    }

    for (int i = 0; i < fullColumns; ++i)
    {
        for (int j = 0; j < rows; ++j)
        {
            indices.push_back(j + i * (rows + 1));
            indices.push_back(j + 1 + i * (rows + 1));
            indices.push_back(j + (i + 1) * (rows + 1));

            indices.push_back(j + 1 + i * (rows + 1));
            indices.push_back(j + 1 + (i + 1) * (rows + 1));
            indices.push_back(j + (i + 1) * (rows + 1));
        }
    }

    for (int i = fullColumns; i < columns; ++i)
    {
        for (int j = 0; j < (quadCount - fullColumns * rows); ++j)
        {
            indices.push_back(j + i * (rows + 1));
            indices.push_back(j + 1 + i * (rows + 1));
            indices.push_back(j + (i + 1) * (rows + 1));

            indices.push_back(j + 1 + i * (rows + 1));
            indices.push_back(j + 1 + (i + 1) * (rows + 1));
            indices.push_back(j + (i + 1) * (rows + 1));
        }
    }
}

/**
Create test geometry.
*/
std::vector<AMD::GeometryFX_Filter::MeshHandle> CreateGeometry(
    const int chunkCount, const int chunkSize, const int chunkSizeVariance, AMD::GeometryFX_Filter &meshManager)
{
    std::vector<std::vector<float>> positions(chunkCount);
    std::vector<std::vector<int>> indices(chunkCount);

    std::vector<int> vertexCountPerMesh;
    std::vector<int> indexCountPerMesh;

    std::normal_distribution<float> distribution(
        static_cast<float>(chunkSize), static_cast<float>(chunkSizeVariance));
    std::mt19937 generator;

    for (int i = 0; i < chunkCount; ++i)
    {
        GenerateGeometryChunk(
            std::max(32, static_cast<int>(distribution(generator))), positions[i], indices[i]);
        vertexCountPerMesh.push_back(static_cast<int>(positions[i].size() / 3));
        indexCountPerMesh.push_back(static_cast<int>(indices[i].size()));
    }

    const auto handles =
        meshManager.RegisterMeshes(chunkCount, vertexCountPerMesh.data(), indexCountPerMesh.data());

    for (int i = 0; i < chunkCount; ++i)
    {
        meshManager.SetMeshData(handles[i], positions[i].data(), indices[i].data());
    }

    return handles;
}

std::vector<AMD::GeometryFX_Filter::MeshHandle> LoadGeometry(
    const char *filename, AMD::GeometryFX_Filter &meshManager, const int chunkSize = 65535)
{
    const auto propertyStore = aiCreatePropertyStore ();
    aiSetImportPropertyInteger (propertyStore,
        AI_CONFIG_PP_SLM_TRIANGLE_LIMIT, chunkSize);
    aiSetImportPropertyInteger (propertyStore,
        AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
    aiSetImportPropertyInteger (propertyStore,
        AI_CONFIG_PP_PTV_NORMALIZE, 1);

    const auto pScene = aiImportFileExWithProperties (filename,
        aiProcess_Triangulate | aiProcess_ConvertToLeftHanded | aiProcess_SortByPType |
        aiProcess_JoinIdenticalVertices | aiProcess_SplitLargeMeshes |
        aiProcess_PreTransformVertices,
        nullptr,
        propertyStore);

    if (pScene)
    {
        std::vector<int> indexCounts;
        std::vector<int> vertexCounts;
        for (unsigned int i = 0; i < pScene->mNumMeshes; ++i)
        {
            indexCounts.push_back(pScene->mMeshes[i]->mNumFaces * 3);
            vertexCounts.push_back(pScene->mMeshes[i]->mNumVertices);
        }

        auto handles =
            meshManager.RegisterMeshes(pScene->mNumMeshes, vertexCounts.data(), indexCounts.data());

        for (unsigned int i = 0; i < pScene->mNumMeshes; ++i)
        {
            // The mesh is triangulated, so we can use 3 indices per face here
            std::vector<int> indices(pScene->mMeshes[i]->mNumFaces * 3);
            for (unsigned j = 0; j < pScene->mMeshes[i]->mNumFaces; ++j)
            {
                for (int k = 0; k < 3; ++k)
                {
                    indices[j * 3 + k] = pScene->mMeshes[i]->mFaces[j].mIndices[k];
                }
            }

            meshManager.SetMeshData(handles[i], pScene->mMeshes[i]->mVertices, indices.data());
        }

        aiReleaseImport (pScene);
        aiReleasePropertyStore (propertyStore);

        return handles;
    }

    aiReleasePropertyStore (propertyStore);

    return std::vector<AMD::GeometryFX_Filter::MeshHandle>();
}

class Application
{
  public:
    Application()
        : enableFiltering(true)
        , instrumentIndirectRender(false)
        , generateGeometry(false)
        , geometryChunkSize(65535)
        , geometryChunkSizeVariance(16384)
        , frustumCoverage(0.9f)
        , frontfaceCoverage(0.5f)
        , useCameraForBenchmark(false)
        , emulateMultiIndirectDraw(false)
        , shadowMapResolution(-1)
        , pipelineStatsTrianglesIn(0)
        , pipelineStatsTrianglesOut(0)
        , enabledFilters(0xFF)
        , benchmarkMode(false)
        , benchmarkFrameCount(32)
        , benchmarkActive(false)
        , warmupFrames(32)
        , fullscreenVs(nullptr)
        , fullscreenPs(nullptr)
    {
    }

    bool enableFiltering;
    bool instrumentIndirectRender;
    int windowWidth;
    int windowHeight;
    bool generateGeometry;
    int geometryChunkSize;
    int geometryChunkSizeVariance;
    float frustumCoverage;
    float frontfaceCoverage;
    bool useCameraForBenchmark;
    bool emulateMultiIndirectDraw;
    int shadowMapResolution;

    int64_t pipelineStatsTrianglesIn;
    int64_t pipelineStatsTrianglesOut;

    uint32_t enabledFilters;

    bool benchmarkMode;

  public:
    int GetMeshCount() const
    {
        return static_cast<int>(meshHandles_.size());
    }

  private:
    std::vector<double> frameTimes;
    int benchmarkFrameCount;
    bool benchmarkActive;
    int warmupFrames;
    std::string benchmarkFilename;
    std::string meshFileName;
    std::string cameraName;
    ID3D11VertexShader *fullscreenVs;
    ID3D11PixelShader *fullscreenPs;

    ComPtr<ID3D11Buffer> fullscreenConstantBuffer;

    ResolutionDependentResources resolutionDependentResources;

  public:
    void Setup(const std::map<std::string, std::string> &options)
    {
        HandleOption(options, "generate-geometry", generateGeometry);
        HandleOption(options, "frustum-coverage", frustumCoverage);
        HandleOption(options, "frontface-coverage", frontfaceCoverage);
        HandleOption(options, "geometry-chunk-size", geometryChunkSize);
        HandleOption(options, "geometry-chunk-size-variance", geometryChunkSizeVariance);
        HandleOption(options, "use-camera-for-benchmark", useCameraForBenchmark);
        HandleOption(options, "emulate-multi-indirect-draw", emulateMultiIndirectDraw);
        HandleOption(options, "resolution", shadowMapResolution);

        if (!HandleOption(options, "mesh", meshFileName))
        {
            meshFileName = "house.obj";
        }

        HandleOption(options, "enabled-filters", enabledFilters);
        HandleOption(options, "enable-filtering", enableFiltering);

        if (!HandleOption(options, "camera", cameraName))
        {
            cameraName = "camera.bin";
        }

        HandleOption(options, "benchmark", benchmarkMode);
        HandleOption(options, "benchmark-frames", benchmarkFrameCount);
        if (!HandleOption(options, "benchmark-filename", benchmarkFilename))
        {
            benchmarkFilename = "result.txt";
        }

        if (!HandleOption(options, "window-width", windowWidth))
        {
            windowWidth = 1024;
        }

        if (!HandleOption(options, "window-height", windowHeight))
        {
            windowHeight = 1024;
        }
    }

  public:
    struct CameraBlob
    {
        XMVECTOR eye, lookAt;
        float nearClip, farClip;
    };

    void StoreViewProjection(const CBaseCamera &camera) const
    {
        CameraBlob cb;
        cb.eye = camera.GetEyePt();
        cb.lookAt = camera.GetLookAtPt();
        cb.nearClip = camera.GetNearClip();
        cb.farClip = camera.GetFarClip();

        AMD::GeometryFX_WriteBlobToFile(cameraName.c_str(), sizeof(cb), &cb);
    }

    void LoadViewProjection(CBaseCamera &camera)
    {
        const auto blob = AMD::GeometryFX_ReadBlobFromFile(cameraName.c_str());
        const CameraBlob *cb = reinterpret_cast<const CameraBlob *>(blob.data());
        camera.SetViewParams(cb->eye, cb->lookAt);
        camera.SetProjParams(camera.GetFOV(), camera.GetAspect(), cb->nearClip, cb->farClip);
    }

    // Create resolution-independent resources
    void Create(ID3D11Device *device)
    {
        assert(device);

        AMD::GeometryFX_FilterDesc ci;
        ci.pDevice = device;
        ci.emulateMultiIndirectDraw = emulateMultiIndirectDraw;

        staticMeshRenderer_ = new AMD::GeometryFX_Filter(&ci);

        if (generateGeometry)
        {
            meshHandles_ = CreateGeometry(
                384, geometryChunkSize, geometryChunkSizeVariance, *staticMeshRenderer_);
        }
        else
        {
            std::string pathToMesh = "..\\media\\" + meshFileName;
            meshHandles_ =
                LoadGeometry(pathToMesh.c_str(), *staticMeshRenderer_, geometryChunkSize);
        }

        D3D11_BUFFER_DESC desc = {};
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.ByteWidth = sizeof(FullscreenConstantBuffer);
        desc.Usage = D3D11_USAGE_DEFAULT;
        device->CreateBuffer(&desc, nullptr, &fullscreenConstantBuffer);

        CreateShaders();
    }

    void CreateShaders()
    {
        CompileShader(DXUTGetD3D11Device(), (ID3D11DeviceChild **)&fullscreenVs,
            "..\\src\\Shaders\\GeometryFX_Sample.hlsl", AMD::ShaderType::Vertex, "FullscreenVS");

        CompileShader(DXUTGetD3D11Device(), (ID3D11DeviceChild **)&fullscreenPs,
            "..\\src\\Shaders\\GeometryFX_Sample.hlsl", AMD::ShaderType::Pixel, "FullscreenPS");
    }

  private:
    void Blit(ID3D11DeviceContext *context, ID3D11RenderTargetView *target)
    {
        assert(context);
        assert(target);

        // Set render resources
        ID3D11RenderTargetView *renderTargets[] = {target};
        context->OMSetRenderTargets(1, renderTargets, g_depthStencilTexture._dsv);
        context->IASetInputLayout(nullptr);
        context->VSSetShader(fullscreenVs, NULL, 0);
        context->PSSetShader(fullscreenPs, NULL, 0);

        FullscreenConstantBuffer fcb;
        fcb.shadowMapWidth = shadowMapResolution == -1 ? DXUTGetWindowWidth() : shadowMapResolution;
        fcb.shadowMapHeight =
            shadowMapResolution == -1 ? DXUTGetWindowHeight() : shadowMapResolution;
        fcb.windowWidth = DXUTGetWindowWidth();
        fcb.windowHeight = DXUTGetWindowHeight();

        context->UpdateSubresource(
            fullscreenConstantBuffer.Get(), 0, nullptr, &fcb, sizeof(fcb), sizeof(fcb));

        ID3D11Buffer *buffers[] = {fullscreenConstantBuffer.Get()};

        context->PSSetConstantBuffers(0, 1, buffers);

        ID3D11ShaderResourceView *resources[] = { resolutionDependentResources.depthShaderView.Get() };
        context->PSSetShaderResources(0, 1, resources);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->Draw(3, 0);
    }

  public:
    void OnFrameRender(ID3D11DeviceContext *context, const CBaseCamera &camera,
        ID3D11RenderTargetView *renderTarget)
    {
        if (benchmarkMode)
        {
            if (useCameraForBenchmark)
            {
                LoadViewProjection(g_Camera);
            }
            benchmarkActive = true;
        }

        AMD::GeometryFX_FilterRenderOptions options;

        options.enableFiltering = enableFiltering;
        options.enabledFilters = enabledFilters;

        AMD::GeometryFX_FilterStatistics filterStatistics;
        if (instrumentIndirectRender)
        {
            options.statistics = &filterStatistics;
        }

        int width, height;
        if (shadowMapResolution == -1)
        {
            width = DXUTGetWindowWidth();
            height = DXUTGetWindowHeight();
        }
        else
        {
            width = height = shadowMapResolution;
        }

        TIMER_Begin(0, L"Depth pass");
        staticMeshRenderer_->BeginRender(
            context, options, camera.GetViewMatrix(), camera.GetProjMatrix(), width, height);

        std::mt19937 generator;
        std::uniform_real_distribution<float> dis01(0.0f, 1.0f);
        std::normal_distribution<float> rotYdis((1 - frontfaceCoverage) * XM_PI, XM_PI / 180 * 8);

        int i = 0;
        const int rows = static_cast<int>(std::sqrt(static_cast<float>(meshHandles_.size())));
        for (std::vector<AMD::GeometryFX_Filter::MeshHandle>::const_iterator it = meshHandles_.begin(),
                                                               end = meshHandles_.end();
             it != end; ++it)
        {
            if (generateGeometry)
            {
                const auto rotate = XMMatrixRotationY(rotYdis(generator));
                const auto scale = XMMatrixScaling(1 / 1024.0f, 1 / 1024.0f, 1 / 1024.0f);
                const auto translate =
                    XMMatrixTranslation((1 - frustumCoverage) * 1.66f + i / rows / 16.0f - 0.66f,
                        i % rows / 16.0f - 0.66f, dis01(generator) * 0.001f);
                staticMeshRenderer_->RenderMesh(*it, rotate * scale * translate);
            }
            else
            {
                staticMeshRenderer_->RenderMesh(*it, XMMatrixIdentity());
            }
            ++i;
        }
        staticMeshRenderer_->EndRender();
        TIMER_End();

        pipelineStatsTrianglesIn = filterStatistics.trianglesProcessed;
        pipelineStatsTrianglesOut = filterStatistics.trianglesRendered;

        D3D11_VIEWPORT viewport = {};
        viewport.MaxDepth = 1.0f;
        viewport.Width = static_cast<float>(DXUTGetWindowWidth());
        viewport.Height = static_cast<float>(DXUTGetWindowHeight());
        context->RSSetViewports(1, &viewport);

        Blit(context, renderTarget);
    }

    void OnFrameEnd()
    {
        if (benchmarkMode && benchmarkActive)
        {
            if (warmupFrames-- > 0)
            {
                return;
            }

            const auto effectTime = TIMER_GetTime(Gpu, L"Depth pass");
            frameTimes.push_back(effectTime);

            if (frameTimes.size() == benchmarkFrameCount)
            {
                // Write out results, and exit
                std::ofstream result;
                result.open(benchmarkFilename.c_str(), std::ios_base::out | std::ios_base::trunc);
                for (std::vector<double>::const_iterator it = frameTimes.begin(),
                                                         end = frameTimes.end();
                     it != end; ++it)
                {
                    result << *it << "\n";
                }
                result.close();
                exit(0);
            }
        }
    }

    void OnFrameBegin(ID3D11DeviceContext *context, const CBaseCamera &camera)
    {
        pipelineStatsTrianglesIn = 0;
        pipelineStatsTrianglesOut = 0;

        context->ClearDepthStencilView(
            resolutionDependentResources.depthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        context->OMSetRenderTargets(0, nullptr, resolutionDependentResources.depthView.Get());

        if (shadowMapResolution != -1)
        {
            D3D11_VIEWPORT viewport = {};
            viewport.MaxDepth = 1.0f;
            viewport.Width = static_cast<float>(shadowMapResolution);
            viewport.Height = static_cast<float>(shadowMapResolution);
            context->RSSetViewports(1, &viewport);
        }
    }

    void Destroy()
    {
        SAFE_RELEASE(fullscreenPs);
        SAFE_RELEASE(fullscreenVs);

        fullscreenConstantBuffer.Reset();

        delete staticMeshRenderer_;
    }

    void CreateResolutionDependentResources(
        ID3D11Device *device, const int width, const int height, const int sampleCount)
    {
        assert(device);
        assert(width > 0);
        assert(height > 0);
        assert(sampleCount > 0);

        if (shadowMapResolution == -1)
        {
            resolutionDependentResources.Create(device, width, height, sampleCount);
        }
        else
        {
            resolutionDependentResources.Create(
                device, shadowMapResolution, shadowMapResolution, sampleCount);
        }
    }

    void DestroyResolutionDependentResources()
    {
        resolutionDependentResources.Destroy();
    }

  private:
    AMD::GeometryFX_Filter *staticMeshRenderer_;
    std::vector<AMD::GeometryFX_Filter::MeshHandle> meshHandles_;
};

Application g_Application;

//--------------------------------------------------------------------------------------
// AMD helper classes defined here
//--------------------------------------------------------------------------------------
static AMD::MagnifyTool g_MagnifyTool;
static AMD::HUD g_HUD;

// Global boolean for HUD rendering
bool g_bRenderHUD = true;

//--------------------------------------------------------------------------------------
// UI control IDs
//--------------------------------------------------------------------------------------
enum GEOMETRYFX_SAMPLE_IDC
{
    IDC_TOGGLEFULLSCREEN = 1,
    IDC_TOGGLEREF,
    IDC_CHANGEDEVICE,
    IDC_SET_RENDERING_MODE,
    IDC_TOGGLE_PIPELINE_INTSTRUMENTATION,
    IDC_TOGGLE_CULL_INDEX_FILTER,
    IDC_TOGGLE_CULL_BACKFACE,
    IDC_TOGGLE_CULL_CLIP,
    IDC_CULL_SMALL_PRIMITIVES,
    IDC_NUM_CONTROL_IDS // THIS ONE SHOULD ALWAYS BE LAST!!!!!
};

CDXUTCheckBox *g_UI_enableFilterCheckBox;
CDXUTCheckBox *g_UI_pipelineInstrumentationCheckBox;
CDXUTCheckBox *g_UI_cullIndexFilterCheckBox;
CDXUTCheckBox *g_UI_cullBackfaceCheckBox;
CDXUTCheckBox *g_UI_cullClipCheckBox;
CDXUTCheckBox *g_UI_cullSmallPrimitivesCheckBox;

const int g_MaxApplicationControlID = IDC_NUM_CONTROL_IDS;

//--------------------------------------------------------------------------------------
// Forward declarations
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    bool *pbNoFurtherProcessing, void *pUserContext);
void CALLBACK OnKeyboard(UINT nChar, bool bKeyDown, bool bAltDown, void *pUserContext);
void CALLBACK OnGUIEvent(UINT nEvent, int nControlID, CDXUTControl *pControl, void *pUserContext);
void CALLBACK OnFrameMove(double fTime, float fElapsedTime, void *pUserContext);
bool CALLBACK ModifyDeviceSettings(DXUTDeviceSettings *pDeviceSettings, void *pUserContext);

bool CALLBACK IsD3D11DeviceAcceptable(const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output,
    const CD3D11EnumDeviceInfo *DeviceInfo, DXGI_FORMAT BackBufferFormat, bool bWindowed,
    void *pUserContext);
HRESULT CALLBACK OnD3D11CreateDevice(
    ID3D11Device *pd3dDevice, const DXGI_SURFACE_DESC *pBackBufferSurfaceDesc, void *pUserContext);
HRESULT CALLBACK OnD3D11ResizedSwapChain(ID3D11Device *pd3dDevice, IDXGISwapChain *pSwapChain,
    const DXGI_SURFACE_DESC *pBackBufferSurfaceDesc, void *pUserContext);
void CALLBACK OnD3D11ReleasingSwapChain(void *pUserContext);
void CALLBACK OnD3D11DestroyDevice(void *pUserContext);
void CALLBACK OnD3D11FrameRender(ID3D11Device *pd3dDevice,
    ID3D11DeviceContext *pd3dImmediateContext, double fTime, float fElapsedTime,
    void *pUserContext);

void InitApp();
void RenderText();

std::map<std::string, std::string> ParseCommandLine(int argc, wchar_t *argv[])
{
    std::map<std::string, std::string> result;

    for (int i = 0; i < argc; ++i)
    {
        if (argv[i][0] != '-')
        {
            // Error
        }
        else
        {
            std::wstring key, value;

            int start = 1;
            // long option
            if (argv[i][1] == '-')
            {
                start = 2;
            }

            // Search for both : and = as separators
            auto separator = wcsstr(argv[i] + start, L"=");
            if (!separator)
            {
                separator = wcsstr(argv[i] + start, L":");
            }

            if (separator)
            {
                key = std::wstring(argv[i] + start, separator);
                value = std::wstring(separator + 1);
            }
            else
            {
                key = std::wstring(argv[i] + start);
            }

            result[WideToUTF8String(key)] = WideToUTF8String(value);
        }
    }

    return result;
}

//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message
// processing
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
// Enable run-time memory check for debug builds.
#if defined(DEBUG) || defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // DXUT will create and use the best device (either D3D9 or D3D11)
    // that is available on the system depending on which D3D callbacks are set
    // below

    // Set DXUT callbacks
    DXUTSetCallbackMsgProc(MsgProc);
    DXUTSetCallbackKeyboard(OnKeyboard);
    DXUTSetCallbackFrameMove(OnFrameMove);
    DXUTSetCallbackDeviceChanging(ModifyDeviceSettings);

    DXUTSetCallbackD3D11DeviceAcceptable(IsD3D11DeviceAcceptable);
    DXUTSetCallbackD3D11DeviceCreated(OnD3D11CreateDevice, &g_Application);
    DXUTSetCallbackD3D11SwapChainResized(OnD3D11ResizedSwapChain, &g_Application);
    DXUTSetCallbackD3D11SwapChainReleasing(OnD3D11ReleasingSwapChain, &g_Application);
    DXUTSetCallbackD3D11DeviceDestroyed(OnD3D11DestroyDevice, &g_Application);
    DXUTSetCallbackD3D11FrameRender(OnD3D11FrameRender, &g_Application);

    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(lpCmdLine, &argc);
    const auto cmdLineOptions = ParseCommandLine(argc, argv);
    g_Application.Setup(cmdLineOptions);

    unsigned int major, minor, patch;
    AMD::GeometryFX_GetVersion(&major, &minor, &patch);

    WCHAR windowTitle[64];
    swprintf_s(windowTitle, 64, L"AMD GeometryFX v%d.%d.%d", major, minor, patch);

    InitApp();
    DXUTInit(true, true, NULL); // Parse the command line, show msgboxes on error, no extra
                                // command line params
    DXUTSetCursorSettings(true, true);
    DXUTCreateWindow(windowTitle);

    DXUTCreateDevice(
        D3D_FEATURE_LEVEL_11_0, true, g_Application.windowWidth, g_Application.windowHeight);

    DXUTMainLoop(); // Enter into the DXUT render loop

    return DXUTGetExitCode();
}

//--------------------------------------------------------------------------------------
// Initialize the app
//--------------------------------------------------------------------------------------
void InitApp()
{
    D3DCOLOR DlgColor = 0x88888888; // Semi-transparent background for the dialog

    g_SettingsDlg.Init(&g_DialogResourceManager);
    g_HUD.m_GUI.Init(&g_DialogResourceManager);
    g_HUD.m_GUI.SetBackgroundColors(DlgColor);
    g_HUD.m_GUI.SetCallback(OnGUIEvent, &g_Application);

    // This sample does not support MSAA, so disable it in the GUI
    g_SettingsDlg.GetDialogControl()->GetControl(DXUTSETTINGSDLG_D3D11_MULTISAMPLE_COUNT)->SetEnabled(false);
    g_SettingsDlg.GetDialogControl()->GetControl(DXUTSETTINGSDLG_D3D11_MULTISAMPLE_QUALITY)->SetEnabled(false);

    int iY = AMD::HUD::iElementDelta;

    g_HUD.m_GUI.AddButton(IDC_TOGGLEFULLSCREEN, L"Toggle full screen", AMD::HUD::iElementOffset, iY,
        AMD::HUD::iElementWidth, AMD::HUD::iElementHeight);
    g_HUD.m_GUI.AddButton(IDC_TOGGLEREF, L"Toggle REF (F3)", AMD::HUD::iElementOffset,
        iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, VK_F3);
    g_HUD.m_GUI.AddButton(IDC_CHANGEDEVICE, L"Change device (F2)", AMD::HUD::iElementOffset,
        iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, VK_F2);

    iY += AMD::HUD::iGroupDelta;

    // Add the magnify tool UI to our HUD
    g_MagnifyTool.InitApp(&g_HUD.m_GUI, iY);

    g_HUD.m_GUI.AddCheckBox(IDC_SET_RENDERING_MODE, L"Enable filter", AMD::HUD::iElementOffset,
        iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight,
        g_Application.enableFiltering, 0U, false, &g_UI_enableFilterCheckBox);

    g_HUD.m_GUI.AddCheckBox(IDC_TOGGLE_PIPELINE_INTSTRUMENTATION, L"Instrument indirect rendering",
        AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth,
        AMD::HUD::iElementHeight, g_Application.instrumentIndirectRender, 0, false,
        &g_UI_pipelineInstrumentationCheckBox);

    g_HUD.m_GUI.AddCheckBox(IDC_TOGGLE_CULL_INDEX_FILTER, L"Index filter", AMD::HUD::iElementOffset,
        iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight,
        TestFlag(g_Application.enabledFilters, AMD::GeometryFX_FilterDuplicateIndices), 0, false,
        &g_UI_cullIndexFilterCheckBox);

    g_HUD.m_GUI.AddCheckBox(IDC_TOGGLE_CULL_BACKFACE, L"Backface", AMD::HUD::iElementOffset,
        iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight,
        TestFlag(g_Application.enabledFilters, AMD::GeometryFX_FilterBackface), 0, false,
        &g_UI_cullBackfaceCheckBox);

    g_HUD.m_GUI.AddCheckBox(IDC_TOGGLE_CULL_CLIP, L"Frustum cull", AMD::HUD::iElementOffset,
        iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight,
        TestFlag(g_Application.enabledFilters, AMD::GeometryFX_FilterFrustum), 0, false,
        &g_UI_cullClipCheckBox);

    g_HUD.m_GUI.AddCheckBox(IDC_CULL_SMALL_PRIMITIVES, L"Small primitives",
        AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth,
        AMD::HUD::iElementHeight,
        TestFlag(g_Application.enabledFilters, AMD::GeometryFX_FilterSmallPrimitives), 0, false,
        &g_UI_cullSmallPrimitivesCheckBox);
}

//--------------------------------------------------------------------------------------
// Render the help and statistics text. This function uses the ID3DXFont
// interface for
// efficient text rendering.
//--------------------------------------------------------------------------------------
void RenderText()
{
    g_pTxtHelper->Begin();
    g_pTxtHelper->SetInsertionPos(5, 5);
    g_pTxtHelper->SetForegroundColor(XMVectorSet(1.0f, 1.0f, 0.0f, 1.0f));
    g_pTxtHelper->DrawTextLine(DXUTGetFrameStats(DXUTIsVsyncEnabled()));
    g_pTxtHelper->DrawTextLine(DXUTGetDeviceStats());

    float fEffectTime = (float)TIMER_GetTime(Gpu, L"Depth pass") * 1000.0f;
    WCHAR wcbuf[512] = {};
    swprintf_s(wcbuf, 256, L"Depth pass time: Total = %.3f ms, %d meshes", fEffectTime,
        g_Application.GetMeshCount());
    g_pTxtHelper->DrawTextLine(wcbuf);

    // Only display filter stats if the filter is actually running
    if (g_Application.instrumentIndirectRender && g_Application.enableFiltering)
    {
        wchar_t buffer[512] = {};
        swprintf_s(buffer, L"Triangle stats: In %I64d, out %I64d (filtered: %.2f%%) ",
            g_Application.pipelineStatsTrianglesIn, g_Application.pipelineStatsTrianglesOut,
            100 -
                static_cast<float>(g_Application.pipelineStatsTrianglesOut) /
                    static_cast<float>(g_Application.pipelineStatsTrianglesIn) * 100.0f);
        g_pTxtHelper->DrawTextLine(buffer);
    }

    g_pTxtHelper->SetInsertionPos(
        5, DXUTGetDXGIBackBufferSurfaceDesc()->Height - AMD::HUD::iElementDelta);
    g_pTxtHelper->DrawTextLine(L"Toggle GUI    : F1");

    g_pTxtHelper->End();
}

//--------------------------------------------------------------------------------------
// Reject any D3D11 devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool CALLBACK IsD3D11DeviceAcceptable(const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output,
    const CD3D11EnumDeviceInfo *DeviceInfo, DXGI_FORMAT BackBufferFormat, bool bWindowed,
    void *pUserContext)
{
    return true;
}

//--------------------------------------------------------------------------------------
// Create any D3D11 resources that aren't dependant on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11CreateDevice(
    ID3D11Device *pd3dDevice, const DXGI_SURFACE_DESC *pBackBufferSurfaceDesc, void *pUserContext)
{
    HRESULT hr;

    ID3D11DeviceContext *pd3dImmediateContext = DXUTGetD3D11DeviceContext();
    V_RETURN(g_DialogResourceManager.OnD3D11CreateDevice(pd3dDevice, pd3dImmediateContext));
    V_RETURN(g_SettingsDlg.OnD3D11CreateDevice(pd3dDevice));
    g_pTxtHelper =
        new CDXUTTextHelper(pd3dDevice, pd3dImmediateContext, &g_DialogResourceManager, 15);

    // Setup the camera's view parameters
    g_Camera.SetViewParams(
        XMVectorSet(0.0f, 0.0f, -2.0f, 1.0f), XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));
    if (g_Application.shadowMapResolution == -1)
    {
        // Setup the camera's projection parameters
        float fAspectRatio = pBackBufferSurfaceDesc->Width / (FLOAT)pBackBufferSurfaceDesc->Height;
        g_Camera.SetProjParams(XM_PI / 4, fAspectRatio, 0.1f, 512.0f);
    }
    else
    {
        g_Camera.SetProjParams(XM_PI / 4, 1.0f, 0.1f, 512.0f);
    }
    g_Camera.SetScalers(0.005f, 0.5f);
    g_Camera.SetRotateButtons(true, false, false);
    // Create AMD_SDK resources here
    g_HUD.OnCreateDevice(pd3dDevice);
    g_MagnifyTool.OnCreateDevice(pd3dDevice);

    auto pApp = static_cast<Application *>(pUserContext);
    pApp->Create(pd3dDevice);

    TIMER_Init(pd3dDevice)

        return S_OK;
}

//--------------------------------------------------------------------------------------
// Create any D3D11 resources that depend on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11ResizedSwapChain(ID3D11Device *pd3dDevice, IDXGISwapChain *pSwapChain,
    const DXGI_SURFACE_DESC *pBackBufferSurfaceDesc, void *pUserContext)
{
    HRESULT hr;

    V_RETURN(g_DialogResourceManager.OnD3D11ResizedSwapChain(pd3dDevice, pBackBufferSurfaceDesc));
    V_RETURN(g_SettingsDlg.OnD3D11ResizedSwapChain(pd3dDevice, pBackBufferSurfaceDesc));

    if (g_Application.shadowMapResolution == -1)
    {
        // Setup the camera's projection parameters
        float fAspectRatio = pBackBufferSurfaceDesc->Width / (FLOAT)pBackBufferSurfaceDesc->Height;
        g_Camera.SetProjParams(
            XM_PI / 4, fAspectRatio, g_Camera.GetNearClip(), g_Camera.GetFarClip());
    }
    else
    {
        g_Camera.SetProjParams(XM_PI / 4, 1.0f, g_Camera.GetNearClip(), g_Camera.GetFarClip());
    }

    // Set the location and size of the AMD standard HUD
    g_HUD.m_GUI.SetLocation(pBackBufferSurfaceDesc->Width - AMD::HUD::iDialogWidth, 0);
    g_HUD.m_GUI.SetSize(AMD::HUD::iDialogWidth, pBackBufferSurfaceDesc->Height);
    g_HUD.OnResizedSwapChain(pBackBufferSurfaceDesc);

    g_depthStencilTexture.CreateSurface(pd3dDevice, pBackBufferSurfaceDesc->Width,
        pBackBufferSurfaceDesc->Height, 1, 1, 1, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, D3D11_USAGE_DEFAULT, false, 0, NULL, NULL, 0);

    // Magnify tool will capture from the color buffer
    g_MagnifyTool.OnResizedSwapChain(pd3dDevice, pSwapChain, pBackBufferSurfaceDesc, pUserContext,
        pBackBufferSurfaceDesc->Width - AMD::HUD::iDialogWidth, 0);
    D3D11_RENDER_TARGET_VIEW_DESC RTDesc;
    ID3D11Resource *pTempRTResource;
    DXUTGetD3D11RenderTargetView()->GetResource(&pTempRTResource);
    DXUTGetD3D11RenderTargetView()->GetDesc(&RTDesc);
    g_MagnifyTool.SetSourceResources(pTempRTResource, RTDesc.Format,
        DXUTGetDXGIBackBufferSurfaceDesc()->Width, DXUTGetDXGIBackBufferSurfaceDesc()->Height,
        DXUTGetDXGIBackBufferSurfaceDesc()->SampleDesc.Count);
    g_MagnifyTool.SetPixelRegion(128);
    g_MagnifyTool.SetScale(5);
    SAFE_RELEASE(pTempRTResource);

    static_cast<Application *>(pUserContext)
        ->CreateResolutionDependentResources(pd3dDevice, pBackBufferSurfaceDesc->Width,
            pBackBufferSurfaceDesc->Height, pBackBufferSurfaceDesc->SampleDesc.Count);

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Render the scene using the D3D11 device
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11FrameRender(ID3D11Device *pd3dDevice,
    ID3D11DeviceContext *pd3dImmediateContext, double fTime, float fElapsedTime, void *pUserContext)
{
    // Reset the timer at start of frame
    TIMER_Reset();

    // If the settings dialog is being shown, then render it instead of
    // rendering the app's
    // scene
    if (g_SettingsDlg.IsActive())
    {
        g_SettingsDlg.OnRender(fElapsedTime);
        return;
    }

    // Clear the backbuffer and depth stencil
    float ClearColor[4] = {0.176f, 0.196f, 0.667f, 0.0f};
    ID3D11RenderTargetView *pRTV = DXUTGetD3D11RenderTargetView();
    pd3dImmediateContext->ClearRenderTargetView(
        (ID3D11RenderTargetView *)DXUTGetD3D11RenderTargetView(), ClearColor);
    pd3dImmediateContext->ClearDepthStencilView(
        g_depthStencilTexture._dsv, D3D11_CLEAR_DEPTH, 1.0, 0);

    auto pApp = static_cast<Application *>(pUserContext);
    pApp->OnFrameBegin(pd3dImmediateContext, g_Camera);
    pApp->OnFrameRender(pd3dImmediateContext, g_Camera, pRTV);
    pApp->OnFrameEnd();

    DXUT_BeginPerfEvent(DXUT_PERFEVENTCOLOR, L"HUD / Stats");

    // Render the HUD
    if (g_bRenderHUD)
    {
        g_MagnifyTool.Render();
        g_HUD.OnRender(fElapsedTime);
    }

    RenderText();

    DXUT_EndPerfEvent();

    static DWORD dwTimefirst = GetTickCount();
    if (GetTickCount() - dwTimefirst > 5000)
    {
        OutputDebugString(DXUTGetFrameStats(DXUTIsVsyncEnabled()));
        OutputDebugString(L"\n");
        dwTimefirst = GetTickCount();
    }
}

//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11ResizedSwapChain
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11ReleasingSwapChain(void *pUserContext)
{
    g_DialogResourceManager.OnD3D11ReleasingSwapChain();

    g_depthStencilTexture.Release();

    static_cast<Application *>(pUserContext)->DestroyResolutionDependentResources();
}

//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11CreateDevice
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11DestroyDevice(void *pUserContext)
{
    g_DialogResourceManager.OnD3D11DestroyDevice();
    g_SettingsDlg.OnD3D11DestroyDevice();
    DXUTGetGlobalResourceCache().OnDestroyDevice();
    SAFE_DELETE(g_pTxtHelper);

    // Destroy AMD_SDK resources here
    g_HUD.OnDestroyDevice();
    g_MagnifyTool.OnDestroyDevice();

    auto pApp = static_cast<Application *>(pUserContext);
    pApp->Destroy();
    TIMER_Destroy()
}

//--------------------------------------------------------------------------------------
// Called right before creating a D3D9 or D3D11 device, allowing the app to
// modify the device
// settings as needed
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings(DXUTDeviceSettings *pDeviceSettings, void *pUserContext)
{
    // For the first device created if its a REF device, optionally display a
    // warning dialog box
    static bool s_bFirstTime = true;
    if (s_bFirstTime)
    {
        s_bFirstTime = false;
        if (pDeviceSettings->d3d11.DriverType == D3D_DRIVER_TYPE_REFERENCE)
        {
            DXUTDisplaySwitchingToREFWarning();
        }

        // Start with vsync disabled
        pDeviceSettings->d3d11.SyncInterval = 0;
    }

    // This sample does not support MSAA
    pDeviceSettings->d3d11.sd.SampleDesc.Count = 1;

    // Multisample quality is always zero
    pDeviceSettings->d3d11.sd.SampleDesc.Quality = 0;

    // Don't auto create a depth buffer, as this sample requires a depth buffer
    // be created such that it's bindable as a shader resource
    pDeviceSettings->d3d11.AutoCreateDepthStencil = false;

    return true;
}

//--------------------------------------------------------------------------------------
// Handle updates to the scene.  This is called regardless of which D3D API is
// used
//--------------------------------------------------------------------------------------
void CALLBACK OnFrameMove(double fTime, float fElapsedTime, void *pUserContext)
{
    // Update the camera's position based on user input
    g_Camera.FrameMove(fElapsedTime);
}

//--------------------------------------------------------------------------------------
// Handle messages to the application
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    bool *pbNoFurtherProcessing, void *pUserContext)
{
    // Pass messages to dialog resource manager calls so GUI state is updated
    // correctly
    *pbNoFurtherProcessing = g_DialogResourceManager.MsgProc(hWnd, uMsg, wParam, lParam);
    if (*pbNoFurtherProcessing)
    {
        return 0;
    }

    // Pass messages to settings dialog if its active
    if (g_SettingsDlg.IsActive())
    {
        g_SettingsDlg.MsgProc(hWnd, uMsg, wParam, lParam);
        return 0;
    }

    // Give the dialogs a chance to handle the message first
    *pbNoFurtherProcessing = g_HUD.m_GUI.MsgProc(hWnd, uMsg, wParam, lParam);
    if (*pbNoFurtherProcessing)
    {
        return 0;
    }

    // Pass all remaining windows messages to camera so it can respond to user
    // input
    g_Camera.HandleMessages(hWnd, uMsg, wParam, lParam);

    return 0;
}

//--------------------------------------------------------------------------------------
// Handle key presses
//--------------------------------------------------------------------------------------
void CALLBACK OnKeyboard(UINT nChar, bool bKeyDown, bool bAltDown, void *pUserContext)
{
    if (bKeyDown)
    {
        switch (nChar)
        {
            case VK_F1:
                g_bRenderHUD = !g_bRenderHUD;
                break;

            case 'I':
            {
                g_Application.StoreViewProjection(g_Camera);
                break;
            }

            case 'O':
            {
                g_Application.LoadViewProjection(g_Camera);
                break;
            }
        }
    }
}

//--------------------------------------------------------------------------------------
// Handles the GUI events
//--------------------------------------------------------------------------------------
void CALLBACK OnGUIEvent(UINT nEvent, int nControlID, CDXUTControl *pControl, void *pUserContext)
{
    switch (nControlID)
    {
        case IDC_TOGGLEFULLSCREEN:
            DXUTToggleFullScreen();
            break;
        case IDC_TOGGLEREF:
            DXUTToggleREF();
            break;
        case IDC_CHANGEDEVICE:
            g_SettingsDlg.SetActive(!g_SettingsDlg.IsActive());
            break;

        case IDC_SET_RENDERING_MODE:
        {
            static_cast<Application *>(pUserContext)->enableFiltering =
                g_UI_enableFilterCheckBox->GetChecked();
            break;
        }

        case IDC_TOGGLE_PIPELINE_INTSTRUMENTATION:
        {
            static_cast<Application *>(pUserContext)->instrumentIndirectRender =
                g_UI_pipelineInstrumentationCheckBox->GetChecked();
            break;
        }

        case IDC_TOGGLE_CULL_BACKFACE:
        {
            SetOrClearFlag(static_cast<Application *>(pUserContext)->enabledFilters,
                AMD::GeometryFX_FilterBackface, g_UI_cullBackfaceCheckBox->GetChecked());
            break;
        }

        case IDC_TOGGLE_CULL_INDEX_FILTER:
        {
            SetOrClearFlag(static_cast<Application *>(pUserContext)->enabledFilters,
                AMD::GeometryFX_FilterDuplicateIndices, g_UI_cullIndexFilterCheckBox->GetChecked());
            break;
        }

        case IDC_TOGGLE_CULL_CLIP:
        {
            SetOrClearFlag(static_cast<Application *>(pUserContext)->enabledFilters,
                AMD::GeometryFX_FilterFrustum, g_UI_cullClipCheckBox->GetChecked());
            break;
        }

        case IDC_CULL_SMALL_PRIMITIVES:
        {
            SetOrClearFlag(static_cast<Application *>(pUserContext)->enabledFilters,
                AMD::GeometryFX_FilterSmallPrimitives, g_UI_cullSmallPrimitivesCheckBox->GetChecked());
            break;
        }
    }

    // Call the MagnifyTool gui event handler
    g_MagnifyTool.OnGUIEvent(nEvent, nControlID, pControl, pUserContext);
}
