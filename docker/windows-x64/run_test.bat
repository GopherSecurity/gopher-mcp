@echo off
REM Simple test runner for libgopher_mcp_auth on Windows

setlocal enabledelayedexpansion

cd /d "%~dp0"

REM Copy library to test directory
if exist "..\gopher_mcp_auth.dll" (
    copy /Y "..\gopher_mcp_auth.dll" "gopher_mcp_auth.dll" >nul
) else if exist "..\libgopher_mcp_auth.dll" (
    copy /Y "..\libgopher_mcp_auth.dll" "gopher_mcp_auth.dll" >nul
) else (
    echo Error: Library not found in parent directory
    exit /b 1
)

REM Copy any runtime dependencies if they exist
if exist "..\libssl-1_1-x64.dll" copy /Y "..\libssl-1_1-x64.dll" . >nul
if exist "..\libcrypto-1_1-x64.dll" copy /Y "..\libcrypto-1_1-x64.dll" . >nul
if exist "..\libcurl.dll" copy /Y "..\libcurl.dll" . >nul

REM Run the test
node simple-test.js
exit /b %errorlevel%