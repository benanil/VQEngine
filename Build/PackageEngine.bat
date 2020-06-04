@echo off

setlocal enabledelayedexpansion

::-------------------------------------------------------------------------------------------------------------------------------------------------------------

set VSWHERE="%PROGRAMFILES(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set MSBUILD_QUERY1=!VSWHERE! -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe
set MSBUILD_QUERY2=vswhere.exe -version "[15.0,16.0)" -products Microsoft.VisualStudio.Product.BuildTools -find MSBuild\**\Bin\MSBuild.exe
set MSBUILD_QUERY3=vswhere.exe -version "[15.0,16.0)" -find MSBuild\**\Bin\MSBuild.exe

set BUILD_FLAG_CLEAN=0
set MSBUILD_QUERY=!MSBUILD_QUERY1!
set MSBUILD=
call :FindMSBuild

set SOLUTION_DIRECTORY=VQE\
set SOLUTION_FILE_NAME=VQE.sln
set SOLUTION_FILE_PATH=!SOLUTION_DIRECTORY!!SOLUTION_FILE_NAME!

set ENGINE_PACKAGE_OUTPUT_DIRECTORY=_artifacts
set ENGINE_BUILD_COMMAND="!MSBUILD!" "%~dp0!SOLUTION_FILE_PATH!"

set BUILD_CONFIG_DEBUG=0
set BUILD_CONFIG_RELEASE=1
set BUILD_CONFIG_REL_WITH_DBG=0

:: Keep track of # build tasks. build, clean, copy/move, etc.
:: assume 2 for build+copy, add more depending on prebuild+clean tasks
set BUILD_NUM_TASKS=2
set BUILD_NUM_CURR_TASK=0

::-------------------------------------------------------------------------------------------------------------------------------------------------------------

:: Check FindMSBuild's status
if !errorlevel! neq 0 (
    echo [VQE-Package] Error: Couldn't find MSBuild
    exit /b -1
)

:: parameter scan
for %%i IN (%*) DO (
    if "%%i"=="-c"      call :AddBuildTask_Clean
    if "%%i"=="-clean"  call :AddBuildTask_Clean
    if "%%i"=="-Clean"  call :AddBuildTask_Clean
    if "%%i"=="-C"      call :AddBuildTask_Clean

    if "%%i"=="-DebugOnly" (
         set BUILD_CONFIG_DEBUG=1
         set BUILD_CONFIG_RELEASE=0
         set BUILD_CONFIG_REL_WITH_DBG=0
    )

    if "%%i"=="-Release" set BUILD_CONFIG_RELEASE=1
    if "%%i"=="-Debug"   (
        set BUILD_CONFIG_DEBUG=1
        set /A BUILD_NUM_TASKS=!BUILD_NUM_TASKS!+1
    )
    if "%%i"=="-RelWithDebInfo" (
        set BUILD_CONFIG_REL_WITH_DBG=1
        set /A BUILD_NUM_TASKS=!BUILD_NUM_TASKS!+1
    )
)
::-------------------------------------------------------------------------------------------------------------------------------------------------------------


::
:: MAIN()
::
echo.
echo [VQE-Package] Packaging Engine...

pushd %~dp0


:: Check if GenerateProjectFiles.bat has been run
if not exist !SOLUTION_DIRECTORY! (
    echo [VQE-Package] Solution directory '!SOLUTION_DIRECTORY!' doesn't exist.
    mkdir !SOLUTION_DIRECTORY!
)

call :ExecBuildTask_PreBuild

:: clean if specified
if !BUILD_FLAG_CLEAN! equ 1 (
    call :ExecBuildTask_Clean
)

:: Package the engine
call :ExecBuildTask_Build

:: move build artifacts into destination folder
call :ExecBuildTask_Move

popd
echo [VQE-Package] PACKAGING SUCCESSFUL!

start !ENGINE_PACKAGE_OUTPUT_DIRECTORY!

exit /b 0

::==============================================================================================================================
::==============================================================================================================================
::==============================================================================================================================

::
:: FindMSBuild()
::
:FindMSBuild

for /f "usebackq tokens=*" %%i IN (`%MSBUILD_QUERY%`) DO (
    set MSBUILD=%%i
    goto CHECK_MSBUILD
)
:CHECK_MSBUILD
if not exist !MSBUILD! (
    echo [VQE-Package] Build Error: MSBuild.exe could not be located.
    echo.
    exit /b -1
)

:: check arg1 == true (bPrintMSBuild)
if "%~1"=="true" (
    echo [VQE-Package] MSBuild Found: !MSBUILD!
)
exit /b 0

:: --------------------------------------------------------------------------

:: 
:: AddBuildTask_Clean()
:: 
:AddBuildTask_Clean
set BUILD_FLAG_CLEAN=1
set /A BUILD_NUM_TASKS=!BUILD_NUM_TASKS!+1
exit /b 0

:: --------------------------------------------------------------------------


::
:: ExecBuildTask_PreBuild()
::
:ExecBuildTask_PreBuild
if not exist !SOLUTION_FILE_PATH! (
    echo [VQE-Package] Couldn't find !SOLUTION_FILE_PATH!
    echo.
    echo **********************************************************************
    echo        [!BUILD_NUM_CURR_TASK!/!BUILD_NUM_TASKS!] Running GenerateProjectFiles.bat...
    echo **********************************************************************
    echo.
    call %~dp0GenerateProjectFiles.bat -noVS
    if !errorlevel! neq 0 (
        echo [VQE-Package] Error: Couldn't generate project files.
        exit /b -1
    )
)
set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1
exit /b 0


::
:: ExecBuildTask_Clean()
::
:ExecBuildTask_Clean
call :PrintBuildStage Clean
!ENGINE_BUILD_COMMAND! /t:Clean /p:Configuration=Release
if !BUILD_CONFIG_DEBUG!        neq 0   !ENGINE_BUILD_COMMAND! /t:Clean /p:Configuration=Debug
if !BUILD_CONFIG_REL_WITH_DBG! neq 0   !ENGINE_BUILD_COMMAND! /t:Clean /p:Configuration=RelWithDebInfo 
set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1
exit /b 0


::
:: ExecBuildTask_Build()
::
:ExecBuildTask_Build
::echo [VQE-Package] ENGINE_BUILD_COMMAND = !ENGINE_BUILD_COMMAND!
call :PrintBuildStage Release
call !ENGINE_BUILD_COMMAND! /p:Configuration=Release
set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1

if !BUILD_CONFIG_DEBUG! neq 0 (
    call :PrintBuildStage Debug
    call !ENGINE_BUILD_COMMAND! /p:Configuration=Debug
    set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1
    if !errorlevel! neq 0 (
        echo ERROR: BUILD ERROR
        exit /b -1
    )
)
if !BUILD_CONFIG_REL_WITH_DBG! neq 0 (
    call :PrintBuildStage RelWithDebInfo
    call !ENGINE_BUILD_COMMAND! /p:Configuration=RelWithDebInfo
    set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1
    if !errorlevel! neq 0 (
        echo ERROR: BUILD ERROR
        exit /b -1
    )
)

if !errorlevel! neq 0 (
    echo ERROR: BUILD ERROR
    exit /b -1
)

echo [VQE-Package] BUILD SUCCESSFUL
echo               - Release
if !BUILD_CONFIG_DEBUG!        neq 0 echo               - Debug
if !BUILD_CONFIG_REL_WITH_DBG! neq 0 echo               - RelWithDbgInfo
exit /b 0



::
:: ExecBuildTask_Move()
::
:ExecBuildTask_Move
echo.
echo **********************************************************************
echo            [!BUILD_NUM_CURR_TASK!/!BUILD_NUM_TASKS!] Move artifacts 
echo **********************************************************************

:: Check engine packaging output directory and clean it if there's a previous engine package
if exist !ENGINE_PACKAGE_OUTPUT_DIRECTORY! (
    echo [VQE-Package] Cleaning... ENGINE_PACKAGE_OUTPUT_DIRECTORY = !ENGINE_PACKAGE_OUTPUT_DIRECTORY!
    rmdir /S /Q !ENGINE_PACKAGE_OUTPUT_DIRECTORY!
)

mkdir !ENGINE_PACKAGE_OUTPUT_DIRECTORY!

echo [VQE-Package] Moving build artifacts to package output directory...
robocopy ../Bin/RELEASE !ENGINE_PACKAGE_OUTPUT_DIRECTORY!/Win64 > nul
if !BUILD_CONFIG_DEBUG!        neq 0  robocopy ../Bin/DEBUG !ENGINE_PACKAGE_OUTPUT_DIRECTORY!/Win64-Debug > nul
if !BUILD_CONFIG_REL_WITH_DBG! neq 0  robocopy ../Bin/RELEASE !ENGINE_PACKAGE_OUTPUT_DIRECTORY!/Win64-PDB > nul
exit /b 0

:: --------------------------------------------------------------------------

::
::
::
:PrintBuildStage
    echo.
    echo.
    echo **********************************************************************
    echo                [!BUILD_NUM_CURR_TASK!/!BUILD_NUM_TASKS!] Build %1
    echo **********************************************************************
exit /b 0