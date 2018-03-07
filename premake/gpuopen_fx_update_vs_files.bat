@echo off

if %1.==. goto usage

set arg1=%1

:: strip off relative path
if "%arg1:~0,3%" == "..\" set arg1=%arg1:~3%

set startdir=%cd%
cd "%~dp0"

echo --- amd_lib ---
cd ..\amd_lib\shared\d3d11\premake
call :createvsfilesincluding2010
call :createvsfileswithminimaldependencies

echo --- amd_sdk ---
cd ..\..\..\..\framework\d3d11\amd_sdk\premake
call :createvsfilesincluding2010
call :createvsfileswithminimaldependencies

echo --- dxut core ---
cd ..\..\dxut\Core
call :createvsfilesincluding2010

echo --- dxut optional ---
cd ..\Optional
call :createvsfilesincluding2010
cd ..\..\..\..\
:: we don't keep solution files for amd_lib, amd_sdk, or dxut
call :cleanslnfiles

echo --- %arg1% ---
cd %arg1%\premake
call :createvsfiles
cd ..\..\

:: sample, capture_viewer, etc.
for /f %%a in ('dir /a:d /b %arg1%_* 2^>nul') do call :createvsfilesforsamples %%a

cd "%startdir%"

goto :EOF

::--------------------------
:: SUBROUTINES
::--------------------------

:: sample, capture_viewer, etc.
:createvsfilesforsamples
if exist %1\premake (
    echo --- %1 ---
    cd %1\premake
    call :createvsfiles
    cd ..\..\
)
goto :EOF

:: run premake for vs2015 and vs2017
:createvsfiles
..\..\premake\premake5.exe vs2015
..\..\premake\premake5.exe vs2017
goto :EOF

:: run premake for vs2015 and vs2017
:createvsfilesincluding2010
..\..\..\..\premake\premake5.exe vs2015
..\..\..\..\premake\premake5.exe vs2017
goto :EOF

:: run premake for vs2015 and vs2017
:createvsfileswithminimaldependencies
..\..\..\..\premake\premake5.exe --file=premake5_minimal.lua vs2015
..\..\..\..\premake\premake5.exe --file=premake5_minimal.lua vs2017
goto :EOF

:: delete unnecessary sln files
:cleanslnfiles
del /f /q amd_lib\shared\d3d11\build\AMD_LIB_2015.sln
del /f /q amd_lib\shared\d3d11\build\AMD_LIB_2017.sln

del /f /q amd_lib\shared\d3d11\build\AMD_LIB_Minimal_2015.sln
del /f /q amd_lib\shared\d3d11\build\AMD_LIB_Minimal_2017.sln

del /f /q framework\d3d11\amd_sdk\build\AMD_SDK_2015.sln
del /f /q framework\d3d11\amd_sdk\build\AMD_SDK_2017.sln

del /f /q framework\d3d11\amd_sdk\build\AMD_SDK_Minimal_2015.sln
del /f /q framework\d3d11\amd_sdk\build\AMD_SDK_Minimal_2017.sln

del /f /q framework\d3d11\dxut\Core\DXUT_2015.sln
del /f /q framework\d3d11\dxut\Core\DXUT_2017.sln

del /f /q framework\d3d11\dxut\Optional\DXUTOpt_2015.sln
del /f /q framework\d3d11\dxut\Optional\DXUTOpt_2017.sln
goto :EOF

::--------------------------
:: usage should be last
::--------------------------

:usage
echo   usage: %0 library_dir_name
echo      or: %0 ..\library_dir_name
echo example: %0 AMD_AOFX
echo      or: %0 ..\AMD_AOFX
