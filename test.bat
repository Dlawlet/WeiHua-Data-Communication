@echo off
REM Script batch pour tester facilement les programmes C++
REM Usage: test.bat program.exe [timeout] [memory]

setlocal

set PROGRAM=%1
set TIMEOUT=%2
set MEMORY=%3

if "%PROGRAM%"=="" (
    echo Usage: test.bat program.exe [timeout] [memory]
    echo.
    echo Exemples:
    echo   test.bat program.exe
    echo   test.bat program.exe 30 512
    echo   test.bat solver.exe 15 1024
    exit /b 1
)

if not exist "%PROGRAM%" (
    echo Erreur: %PROGRAM% n'existe pas
    exit /b 1
)

REM Valeurs par défaut
if "%TIMEOUT%"=="" set TIMEOUT=30
if "%MEMORY%"=="" set MEMORY=512

echo ============================================================
echo Test de %PROGRAM%
echo Timeout: %TIMEOUT%s, Memory: %MEMORY%MB
echo ============================================================
echo.

REM Exécuter le test
python scripts/resource_limiter.py "%PROGRAM%" -t %TIMEOUT% -m %MEMORY%

endlocal
