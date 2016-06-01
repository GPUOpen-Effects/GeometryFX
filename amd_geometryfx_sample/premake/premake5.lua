_AMD_LIBRARY_NAME = "GeometryFX"
_AMD_LIBRARY_NAME_ALL_CAPS = string.upper(_AMD_LIBRARY_NAME)

-- Set _AMD_LIBRARY_NAME before including amd_premake_util.lua
dofile ("../../premake/amd_premake_util.lua")

workspace (_AMD_LIBRARY_NAME .. "_Sample")
   configurations { "Debug", "Release" }
   platforms { "x64" }
   location "../build"
   filename (_AMD_LIBRARY_NAME .. "_Sample" .. _AMD_VS_SUFFIX)
   startproject (_AMD_LIBRARY_NAME .. "_Sample")

   filter "platforms:x64"
      system "Windows"
      architecture "x64"

externalproject ("AMD_" .. _AMD_LIBRARY_NAME)
   kind "SharedLib"
   language "C++"
   location "../../AMD_%{_AMD_LIBRARY_NAME}/build"
   filename ("AMD_" .. _AMD_LIBRARY_NAME .. _AMD_VS_SUFFIX)
   uuid "E05C77A9-1EE7-4F02-AF03-575FB4829AC5"
   configmap {
      ["Debug"] = "DLL_Debug",
      ["Release"] = "DLL_Release" }

externalproject "AMD_LIB"
   kind "StaticLib"
   language "C++"
   location "../../amd_lib/shared/d3d11/build"
   filename ("AMD_LIB" .. _AMD_VS_SUFFIX)
   uuid "0D2AEA47-7909-69E3-8221-F4B9EE7FCF44"

externalproject "AMD_SDK"
   kind "StaticLib"
   language "C++"
   location "../../framework/d3d11/amd_sdk/build"
   filename ("AMD_SDK" .. _AMD_VS_SUFFIX)
   uuid "EBB939DC-98E4-49DF-B1F1-D2E80A11F60A"

externalproject "DXUT"
   kind "StaticLib"
   language "C++"
   location "../../framework/d3d11/dxut/Core"
   filename ("DXUT" .. _AMD_VS_SUFFIX)
   uuid "85344B7F-5AA0-4E12-A065-D1333D11F6CA"

externalproject "DXUTOpt"
   kind "StaticLib"
   language "C++"
   location "../../framework/d3d11/dxut/Optional"
   filename ("DXUTOpt" .. _AMD_VS_SUFFIX)
   uuid "61B333C2-C4F7-4CC1-A9BF-83F6D95588EB"

project (_AMD_LIBRARY_NAME .. "_Sample")
   kind "WindowedApp"
   language "C++"
   location "../build"
   filename (_AMD_LIBRARY_NAME .. "_Sample" .. _AMD_VS_SUFFIX)
   uuid "947E81B0-804C-75B6-69E0-E5DC554D511B"
   targetdir "../bin"
   objdir "../build/%{_AMD_SAMPLE_DIR_LAYOUT}"
   warnings "Extra"
   floatingpoint "Fast"

   -- Specify WindowsTargetPlatformVersion here for VS2015
   windowstarget (_AMD_WIN_SDK_VERSION)

   -- Copy DLLs to the local bin directory
   postbuildcommands { amdSamplePostbuildCommands(true, true) }
   postbuildmessage "Copying dependencies..."

   files { "../src/**.h", "../src/**.cpp", "../src/**.rc", "../src/**.manifest", "../src/**.hlsl" }
   includedirs { "../src/ResourceFiles", "../../AMD_%{_AMD_LIBRARY_NAME}/inc", "../../amd_lib/shared/common/inc", "../../amd_lib/shared/d3d11/inc", "../../amd_lib/ags_lib/inc", "../../framework/d3d11/amd_sdk/inc", "../../framework/d3d11/dxut/Core", "../../framework/d3d11/dxut/Optional", "../../third_party/assimp/include" }
   libdirs { "../../third_party/assimp/lib", "../../amd_lib/ags_lib/lib" }
   links { "AMD_%{_AMD_LIBRARY_NAME}", "AMD_LIB", "AMD_SDK", "DXUT", "DXUTOpt", "assimp_%{cfg.platform}%{_AMD_VS_SUFFIX}", "amd_ags_%{cfg.platform}", "d3dcompiler", "dxguid", "winmm", "comctl32", "Usp10", "Shlwapi" }
   defines { "ASSIMP_DLL", "AMD_%{_AMD_LIBRARY_NAME_ALL_CAPS}_COMPILE_DYNAMIC_LIB=1" }

   filter "configurations:Debug"
      defines { "WIN32", "_DEBUG", "DEBUG", "PROFILE", "_WINDOWS", "_WIN32_WINNT=0x0601" }
      flags { "Symbols", "FatalWarnings", "Unicode", "WinMain" }
      targetsuffix ("_Debug" .. _AMD_VS_SUFFIX)

   filter "configurations:Release"
      defines { "WIN32", "NDEBUG", "PROFILE", "_WINDOWS", "_WIN32_WINNT=0x0601" }
      flags { "LinkTimeOptimization", "Symbols", "FatalWarnings", "Unicode", "WinMain" }
      targetsuffix ("_Release" .. _AMD_VS_SUFFIX)
      optimize "On"
