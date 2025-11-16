@echo off
cd /d "C:\Projects\FlashCpp"

echo Building FlashCpp...
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" FlashCpp.vcxproj /p:Configuration=Debug /p:Platform=x64

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)

echo Build successful!
