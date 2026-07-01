@echo off
REM ============================================================
REM  unblock.bat — Removes Windows "Mark of the Web", adds
REM  Windows Defender exclusions, and clears the icon cache so
REM  the UAC shield overlay disappears from xbox-install.exe.
REM
REM  Usage:
REM    1. Copy xbox-install.exe and this unblock.bat to the
REM       same folder on Windows.
REM    2. Right-click unblock.bat → Run as administrator
REM       (some steps require admin)
REM ============================================================

setlocal
cd /d "%~dp0"

echo ============================================================
echo  Xbox 360 Content Tool — Unblock and Clear Shield Utility
echo ============================================================
echo.

REM --- Step 1: Remove Mark-of-the-Web (Zone.Identifier ADS) ---
echo [1/4] Removing Mark-of-the-Web (Zone.Identifier)...
if exist "xbox-install.exe" (
    powershell -NoProfile -Command "Unblock-File -Path 'xbox-install.exe' -ErrorAction SilentlyContinue"
    if errorlevel 1 (
        echo     Trying alternate method...
        type "xbox-install.exe:Zone.Identifier" 2>nul && (
            echo     Zone.Identifier found, removing...
            del "xbox-install.exe:Zone.Identifier" 2>nul
        )
    )
    echo     Done.
) else (
    echo     ERROR: xbox-install.exe not found in current folder.
    echo     Place this script in the same folder as xbox-install.exe.
    pause
    exit /b 1
)

REM --- Step 2: Clear Windows icon cache (CRITICAL for shield removal) ---
echo.
echo [2/4] Clearing Windows icon cache (to remove UAC shield overlay)...
echo       This is necessary because Windows caches the shield icon overlay.
echo       Without clearing the cache, the shield may persist even after
echo       the manifest is properly embedded.
echo.
echo       Stopping Explorer...
taskkill /IM explorer.exe /F >nul 2>&1
echo       Deleting icon cache files...
del /a /q "%localappdata%\iconcache.db" 2>nul
del /a /f /q "%localappdata%\Microsoft\Windows\Explorer\iconcache*" 2>nul
del /a /f /q "%localappdata%\Microsoft\Windows\Explorer\thumbcache*" 2>nul
echo       Restarting Explorer...
start explorer.exe
echo       Done. Icon cache will rebuild automatically.

REM --- Step 3: Add folder to Windows Defender exclusions (OPTIONAL) ---
echo.
echo [3/4] Add this folder to Windows Defender exclusions?
echo       This prevents Defender from scanning files in this folder.
echo       (Already running as administrator)
echo.
set /p ADD_EXCLUSION="Add folder to Defender exclusions? [y/N]: "
if /i "%ADD_EXCLUSION%"=="y" (
    echo     Adding exclusion for: %CD%
    powershell -NoProfile -Command "Add-MpPreference -ExclusionPath '%CD%' -ErrorAction SilentlyContinue"
    if errorlevel 1 (
        echo     ERROR: Could not add exclusion.
        echo     Make sure you ran this script as Administrator.
    ) else (
        echo     Done. Folder is now excluded from Defender scans.
    )
) else (
    echo     Skipped.
)

REM --- Step 4: Verify manifest is embedded (informational) ---
echo.
echo [4/4] Verifying manifest is embedded in xbox-install.exe...
powershell -NoProfile -Command "$exe = 'xbox-install.exe'; $size = (Get-Item $exe).Length; Write-Host ('  File size: ' + $size + ' bytes'); $content = [System.IO.File]::ReadAllBytes($exe); $manifestStr = 'asInvoker'; $found = $false; for ($i = 0; $i -lt $content.Length - 9; $i++) { if ($content[$i] -eq 0x61 -and $content[$i+1] -eq 0x73 -and $content[$i+2] -eq 0x49 -and $content[$i+3] -eq 0x6e -and $content[$i+4] -eq 0x76 -and $content[$i+5] -eq 0x6f -and $content[$i+6] -eq 0x6b -and $content[$i+7] -eq 0x65 -and $content[$i+8] -eq 0x72) { $found = $true; break } }; if ($found) { Write-Host '  Manifest asInvoker: FOUND (good)' } else { Write-Host '  Manifest asInvoker: NOT FOUND (bad)' }"

echo.
echo ============================================================
echo  All done. You can now run xbox-install.exe directly.
echo ============================================================
echo.
echo  IMPORTANT: After running this script:
echo    1. The UAC shield icon overlay should be GONE from xbox-install.exe
echo    2. The "This program might not have installed correctly" dialog
echo       should no longer appear
echo    3. The -h flag should work correctly
echo.
echo  If the shield still appears after icon cache clear:
echo    - Restart Windows (sometimes needed for icon cache to fully rebuild)
echo    - Or right-click the exe → Properties → check "Unblock" checkbox
echo.
echo  Note: SmartScreen may still show "Windows protected your PC" on first
echo  run because the exe is NOT code-signed. Click:
echo     More info  →  Run anyway
echo  This warning disappears after the file builds reputation.
echo.
pause
