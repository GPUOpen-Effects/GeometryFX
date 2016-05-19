# AMD GeometryFX
![AMD GeometryFX](http://gpuopen-effects.github.io/media/effects/geometryfx_thumbnail.png)

The GeometryFX library provides convenient access to compute-based triangle filtering (CTF), which improves triangle throughput by filtering out triangles that do not contribute to the final image using a compute-preprocess.

<div>
  <a href="https://github.com/GPUOpen-Effects/GeometryFX/releases/latest/"><img src="http://gpuopen-effects.github.io/media/latest-release-button.svg" alt="Latest release" title="Latest release"></a>
</div>

### Prerequisites
* AMD Radeon&trade; GCN-based GPU (HD 7000 series or newer)
  * Or other DirectX&reg; 11 compatible discrete GPU with Shader Model 5 support 
* 64-bit Windows&reg; 7 (SP1 with the [Platform Update](https://msdn.microsoft.com/en-us/library/windows/desktop/jj863687.aspx)), Windows&reg; 8.1, or Windows&reg; 10
* Visual Studio&reg; 2012, Visual Studio&reg; 2013, or Visual Studio&reg; 2015

### Getting started
* Visual Studio solutions for VS2012, VS2013, and VS2015 can be found in the `amd_geometryfx_sample\build` directory.
* There are also solutions for just the core library in the `amd_geometryfx\build` directory.

### Premake
The Visual Studio solutions and projects in this repo were generated with Premake. If you need to regenerate the Visual Studio files, double-click on `gpuopen_geometryfx_update_vs_files.bat` in the `premake` directory.

This version of Premake has been modified from the stock version to use the property sheet technique for the Windows SDK from this [Visual C++ Team blog post](http://blogs.msdn.com/b/vcblog/archive/2012/11/23/using-the-windows-8-sdk-with-visual-studio-2010-configuring-multiple-projects.aspx). The technique was originally described for using the Windows 8.0 SDK with Visual Studio 2010, but it applies more generally to using newer versions of the Windows SDK with older versions of Visual Studio.

The default SDK for a particular version of Visual Studio (for 2012 or higher) is installed as part of Visual Studio installation. This default (Windows 8.0 SDK for Visual Studio 2012 and Windows 8.1 SDK for Visual Studio 2013) will be used if newer SDKs do not exist on the user's machine. However, the projects generated with this version of Premake will use the next higher SDK (Windows 8.1 SDK for Visual Studio 2012 and Windows 10 SDK with Visual Studio 2013), if the newer SDKs exist on the user's machine.

For Visual Studio 2015, this version of Premake adds the `WindowsTargetPlatformVersion` element to the project file to specify which version of the Windows SDK will be used. To change `WindowsTargetPlatformVersion` for Visual Studio 2015, change the value for `_AMD_WIN_SDK_VERSION` in `premake\amd_premake_util.lua` and regenerate the Visual Studio files.

### How GeometryFX Works

GeometryFX improves the rasterizer efficiency by culling triangles that do not contribute to the output in a pre-pass. This allows the full chip to be used to process geometry, and ensures that the rasterizer only processes triangles that are visible.

A good use case for the GeometryFX library is depth-only rendering of opaque geometry – for example, in shadow maps:
* Depth-only rendering leaves most compute units idle, which can be used by GeometryFX.
* Opaque geometry has no ordering requirements, so GeometryFX can cull triangles in arbitrary order and regroup/split draw calls.
* All geometry can be rendered using the same vertex shader, which allows the GeometryFX library to merge draw calls for maximum efficiency

At its core, GeometryFX works by generating an intermediate index buffer which consists of visible triangles only. Intermediate buffers are reused as much as possible to minimize memory usage. GeometryFX also buffers up draw calls to execute the filtering on one batch while the previous batch is being rendered, allowing the filtering to overlap with the actual draw call.

The library makes heavy use of multi-draw indirect. This is a DirectX 11 driver extension exposed through the AMD GPU Services (AGS) library. It allows multiple draw calls to be prepared on the GPU and executed with a single API call. For more information on AGS, including samples, visit the [AGS SDK repository on GitHub](https://github.com/GPUOpen-LibrariesAndSDKs/AGS_SDK/).

### The Filters

GeometryFX comes with several built-in filters:

* Backface culling: This is generally the most efficient filter, which removes back-facing triangles. In order to avoid clipping, the culling is performed in homogeneous coordinates.
* Small primitive filtering: Triangles which are guaranteed to not hit a sample are removed. This filter tests the bounding box of the triangle against the sample grid, and requires the triangle to be projected.
* Frustum culling: Cull triangles against the view frustum. While most games perform per-object culling, this filter runs per-triangle.
* Cluster culling: Filter complete clusters of triangles before going into per-triangle filtering.

The filters are executed in a compute shader which writes the new index buffer and the draw calls for each batch.

### Integration

Applications which want to integrate GeometryFX as-is are expected to allocate all static geometry through GeometryFX. The API exposes a function to obtain the storage location, which can be used for normal rendering of the geometry. Notice that GeometryFX will aggressively pool all data to allow as many draw calls as possible to be served from the same buffer.

At run-time, the application has to provide the view/projection matrix to GeometryFX and the list of objects that have to be rendered. Once everything has been submitted, GeometryFX will execute the filtering and rendering.

### Learn More
* [Cluster culling blog post on GPUOpen](http://gpuopen.com/geometryfx-1-2-cluster-culling/)

### Third-Party Software
* DXUT is distributed under the terms of the MIT License. See `dxut\MIT.txt`.
* Premake is distributed under the terms of the BSD License. See `premake\LICENSE.txt`.
* The Open Asset Import Library (assimp) is distributed under the terms of the BSD License. See `assimp\LICENSE`.

DXUT and assimp are only used by the sample, not the core library. Only first-party software (specifically `ags_lib`, `amd_geometryfx`, and `amd_lib`) is needed to build the GeometryFX library.

### Attribution
* AMD, the AMD Arrow logo, Radeon, and combinations thereof are either registered trademarks or trademarks of Advanced Micro Devices, Inc. in the United States and/or other countries.
* Microsoft, DirectX, Visual Studio, and Windows are either registered trademarks or trademarks of Microsoft Corporation in the United States and/or other countries.
