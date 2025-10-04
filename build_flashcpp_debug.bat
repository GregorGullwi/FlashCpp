@echo off
setlocal

echo Building FlashCpp Debug Test...

REM Set paths
set SOLUTION_DIR=%~dp0
set SOURCE_FILE=%SOLUTION_DIR%tests\FlashCppDebugTest\flashcpp_debug_test.cpp
set OUTPUT_DIR=%SOLUTION_DIR%Debug
set OBJ_FILE=%OUTPUT_DIR%\flashcpp_debug_test.obj
set EXE_FILE=%OUTPUT_DIR%\flashcpp_debug_test.exe
set PDB_FILE=%OUTPUT_DIR%\flashcpp_debug_test.pdb

REM Create output directory if it doesn't exist
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

REM Check if FlashCpp.exe exists
if not exist "%SOLUTION_DIR%x64\Debug\FlashCpp.exe" (
    echo Error: FlashCpp.exe not found at %SOLUTION_DIR%x64\Debug\FlashCpp.exe
    echo Please build the FlashCpp project first.
    exit /b 1
)

echo Compiling with FlashCpp compiler...
echo Source: %SOURCE_FILE%
echo Object: %OBJ_FILE%

REM Compile with FlashCpp
"%SOLUTION_DIR%x64\Debug\FlashCpp.exe" "%SOURCE_FILE%" -o "%OBJ_FILE%"

if %ERRORLEVEL% neq 0 (
    echo FlashCpp compilation failed!
    exit /b %ERRORLEVEL%
)

echo Linking with MSVC linker...

REM Link with MSVC linker to create executable
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe" ^
    /OUT:"%EXE_FILE%" ^
    "%OBJ_FILE%" ^
    /SUBSYSTEM:CONSOLE ^
    /DEBUG:FULL ^
    /ENTRY:mainCRTStartup ^
    /DEFAULTLIB:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64\LIBCMT.lib" ^
    /DEFAULTLIB:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64\kernel32.lib" ^
    /DEFAULTLIB:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64\libvcruntime.lib" ^
    /DEFAULTLIB:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64\libucrt.lib" ^
    /DEFAULTLIB:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64\uuid.lib" ^
    /DEFAULTLIB:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64\OLDNAMES.lib" ^
    /NODEFAULTLIB:msvcrt.lib

if %ERRORLEVEL% neq 0 (
    echo Linking failed!
    exit /b %ERRORLEVEL%
)

echo Build successful!
echo Executable: %EXE_FILE%
echo PDB file: %PDB_FILE%

REM Verify files exist
if exist "%EXE_FILE%" (
    echo Executable created successfully
) else (
    echo Executable not found
)

if exist "%PDB_FILE%" (
    echo PDB file created successfully
) else (
    echo PDB file not found
)

echo.
echo You can now debug this executable in Visual Studio!
echo 1. Set FlashCppDebugTest as startup project
echo 2. Set breakpoints in flashcpp_debug_test.cpp
echo 3. Press F5 to start debugging

endlocal
