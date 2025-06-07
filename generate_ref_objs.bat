@echo off
setlocal enabledelayedexpansion

set "MSVC_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.38.33130"
set "REF_DIR=tests\reference"

echo Compiling reference object files...

:: Set up the environment for cl.exe
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

:: Compile each source file into an object file
cl.exe /c /Fo"%REF_DIR%\return1_ref.obj" "%REF_DIR%\return1.cpp"
cl.exe /c /Fo"%REF_DIR%\return2func_ref.obj" "%REF_DIR%\return2func.cpp"
cl.exe /c /Fo"%REF_DIR%\call_function_with_argument_ref.obj" "%REF_DIR%\call_function_with_argument.cpp"
cl.exe /c /Fo"%REF_DIR%\add_function_ref.obj" "%REF_DIR%\add_function.cpp"
cl.exe /c /Fo"%REF_DIR%\arithmetic_test_ref.obj" "%REF_DIR%\arithmetic_test.cpp"

echo Done! 