@echo off

REM Set this to point to the Windows SDK version where fxc is located
REM Recommend Windows SDK 8.1 or higher.
set windows_sdk=10

REM Batch file for compiling the GeometryFX library shaders
echo --- Compiling shaders for GeometryFX ---

set startdir=%cd%
cd "%~dp0"

set fxc_exe=%ProgramFiles(x86)%\Windows Kits\%windows_sdk%\bin\x86\fxc.exe

REM If fxc.exe exists in the same directory as the batch file, it will be used instead
if exist .\fxc.exe set fxc_exe=.\fxc.exe

echo --- Using "%fxc_exe%" ---

"%fxc_exe%" /nologo /DAMD_COMPILE_COMPUTE_SHADER=1 /E FilterCS                 /T cs_5_0 /Fh ..\inc\AMD_GeometryFX_FilterCS.inc                 /Vn AMD_GeometryFX_FilterCS  /DSMALL_BATCH_SIZE=256 /DSMALL_BATCH_COUNT=384 ../AMD_GeometryFX_Filtering.hlsl
"%fxc_exe%" /nologo /DAMD_COMPILE_COMPUTE_SHADER=1 /E ClearDrawIndirectArgsCS  /T cs_5_0 /Fh ..\inc\AMD_GeometryFX_ClearDrawIndirectArgsCS.inc  /Vn AMD_GeometryFX_ClearDrawIndirectArgsCS  ../AMD_GeometryFX_Filtering.hlsl
"%fxc_exe%" /nologo /DAMD_COMPILE_VERTEX_SHADER=1  /E DepthOnlyVS              /T vs_5_0 /Fh ..\inc\AMD_GeometryFX_DepthOnlyVS.inc              /Vn AMD_GeometryFX_DepthOnlyVS              ../AMD_GeometryFX_Filtering.hlsl
"%fxc_exe%" /nologo /DAMD_COMPILE_VERTEX_SHADER=1  /E DepthOnlyMultiIndirectVS /T vs_5_0 /Fh ..\inc\AMD_GeometryFX_DepthOnlyMultiIndirectVS.inc /Vn AMD_GeometryFX_DepthOnlyMultiIndirectVS ../AMD_GeometryFX_Filtering.hlsl

cd "%startdir%"
pause
