@echo off
setlocal

if "%~1"=="" (
    echo Usage: link.bat input.obj [output.exe]
    echo   If output.exe is not specified, uses input name with .exe extension
    exit /b 1
)

set INPUT=%~1
set OUTPUT=%~2

if "%OUTPUT%"=="" (
    set OUTPUT=%~n1.exe
)

"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe" ^
  /NOLOGO ^
  /OUT:"%OUTPUT%" ^
  /LIBPATH:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64" ^
  /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64" ^
  /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64" ^
  /DEBUG ^
  /SUBSYSTEM:CONSOLE ^
  "%INPUT%" ^
  kernel32.lib
