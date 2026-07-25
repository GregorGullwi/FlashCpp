@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "CONFIG=Sharded"
set "EXTRA_DEFINES="
set "ENABLE_PERF_STATS=0"
set "ENABLE_ALLOC_STATS=0"
set "ENABLE_ALLOC_STACKS=0"

:parse_args
if "%~1"=="" goto done_parse
if /i "%~1"=="--perf-stats" (
	set "ENABLE_PERF_STATS=1"
	shift
	goto parse_args
)
if /i "%~1"=="--alloc-stats" (
	set "ENABLE_ALLOC_STATS=1"
	shift
	goto parse_args
)
if /i "%~1"=="--alloc-stacks" (
	set "ENABLE_ALLOC_STATS=1"
	set "ENABLE_ALLOC_STACKS=1"
	shift
	goto parse_args
)
if /i "%~1"=="--profile" (
	set "ENABLE_PERF_STATS=1"
	set "ENABLE_ALLOC_STATS=1"
	set "ENABLE_ALLOC_STACKS=1"
	shift
	goto parse_args
)
if /i "%~1"=="--help" (
	goto show_help
)
if "%~1:~0,2%"=="--" (
	echo Unknown option: %~1
	goto show_help
)
set "CONFIG=%~1"
shift
goto parse_args

:done_parse
if "%ENABLE_PERF_STATS%"=="1" (
	set "EXTRA_DEFINES=!EXTRA_DEFINES!WITH_PARSER_RUNTIME_STATS=1;"
)
if "%ENABLE_ALLOC_STATS%"=="1" (
	set "EXTRA_DEFINES=!EXTRA_DEFINES!FLASHCPP_TRACK_ALLOCATIONS=1;"
)
if "%ENABLE_ALLOC_STACKS%"=="1" (
	set "EXTRA_DEFINES=!EXTRA_DEFINES!FLASHCPP_TRACK_ALLOCATION_STACKS=1;"
)

echo Building FlashCpp (%CONFIG%)...
if not "%EXTRA_DEFINES%"=="" (
	echo Profiling defines: %EXTRA_DEFINES%
	"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" FlashCppMSVC.vcxproj /t:Rebuild /m /p:Configuration=%CONFIG% /p:Platform=x64 /p:AdditionalPreprocessorDefinitions="%EXTRA_DEFINES%"
) else (
	"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" FlashCppMSVC.vcxproj /t:Rebuild /m /p:Configuration=%CONFIG% /p:Platform=x64
)

if %ERRORLEVEL% neq 0 (
	echo Build failed!
	exit /b %ERRORLEVEL%
)

echo Build successful!
exit /b 0

:show_help
echo Usage: build_flashcpp.bat [CONFIG] [options]
echo.
echo CONFIG defaults to Sharded.
echo.
echo Profiling options (opt-in, can be combined):
echo   --perf-stats    Enable WITH_PARSER_RUNTIME_STATS for --perf-stats
echo   --alloc-stats   Enable FLASHCPP_TRACK_ALLOCATIONS for --alloc-stats
echo   --alloc-stacks  Enable FLASHCPP_TRACK_ALLOCATION_STACKS (implies --alloc-stats)
echo   --profile       Enable all profiling defines above
echo.
echo Examples:
echo   build_flashcpp.bat
echo   build_flashcpp.bat Sharded --alloc-stats
echo   build_flashcpp.bat --profile
exit /b 1
