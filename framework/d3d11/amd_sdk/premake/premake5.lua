dofile ("../../../../premake/amd_premake_util.lua")

workspace "AMD_SDK"
   configurations { "Debug", "Release" }
   platforms { "x64" }
   location "../build"
   filename ("AMD_SDK" .. _AMD_VS_SUFFIX)
   startproject "AMD_SDK"

   filter "platforms:x64"
      system "Windows"
      architecture "x64"

project "AMD_SDK"
   kind "StaticLib"
   language "C++"
   location "../build"
   filename ("AMD_SDK" .. _AMD_VS_SUFFIX)
   uuid "EBB939DC-98E4-49DF-B1F1-D2E80A11F60A"
   targetdir "../lib"
   objdir "../build/%{_AMD_SAMPLE_DIR_LAYOUT}"
   warnings "Extra"
   floatingpoint "Fast"

   -- Specify WindowsTargetPlatformVersion here for VS2015
   systemversion (_AMD_WIN_SDK_VERSION)

   files { "../inc/**.h", "../src/**.h", "../src/**.cpp", "../src/**.hlsl" }
   includedirs { "../../dxut/Core", "../../dxut/Optional", "../../../../third_party/assimp/include" }

   filter "configurations:Debug"
      defines { "WIN32", "_DEBUG", "DEBUG", "PROFILE", "_WINDOWS", "_LIB", "_WIN32_WINNT=0x0601" }
      flags { "FatalWarnings" }
	  symbols "On"
	  characterset "Unicode"
      targetsuffix ("_Debug" .. _AMD_VS_SUFFIX)

   filter "configurations:Release"
      defines { "WIN32", "NDEBUG", "_WINDOWS", "_LIB", "_WIN32_WINNT=0x0601" }
      flags { "LinkTimeOptimization", "FatalWarnings" }
      targetsuffix ("_Release" .. _AMD_VS_SUFFIX)
	  symbols "On"
	  characterset "Unicode"
      optimize "On"
