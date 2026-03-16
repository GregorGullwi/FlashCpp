@echo off
cd /d "%~dp0"

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Sharded

echo Building FlashCpp (%CONFIG%)...
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" FlashCpp.vcxproj /m /p:Configuration=%CONFIG% /p:Platform=x64

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)

echo Build successful!
