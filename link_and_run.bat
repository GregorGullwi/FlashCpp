@echo off
REM Link the object file to an executable
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe" /OUT:mytest.exe add_function.obj /SUBSYSTEM:CONSOLE /DEBUG /ENTRY:mainCRTStartup /DEFAULTLIB:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64\LIBCMT.lib" /DEFAULTLIB:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64\kernel32.lib" /DEFAULTLIB:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64\libvcruntime.lib" /DEFAULTLIB:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64\libucrt.lib" /DEFAULTLIB:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64\uuid.lib" /DEFAULTLIB:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64\OLDNAMES.lib" /DEFAULTLIB:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64\libcmt.lib" /DEFAULTLIB:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64\oldnames.lib" /NODEFAULTLIB:msvcrt.lib

REM Run the executable
mytest.exe
set TEST_ERRORLEVEL=%ERRORLEVEL%

REM Disassemble the executable
rem "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx86\x64\dumpbin.exe" /DISASM mytest.exe
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.38.33130\bin\Hostx64\x64\dumpbin.exe" /disasm add_function.obj

REM Print the saved errorlevel
echo.
echo mytest.exe exited with errorlevel %TEST_ERRORLEVEL%