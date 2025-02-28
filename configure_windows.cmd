@echo off
set local

IF "%GGPO_SHARED_LIB%" == "" (
   echo GGPO_SHARED_LIB not set.  Defaulting to off
   set GGPO_SHARED_LIB=off
)

echo Generating GGPO Visual Studio solution files.
echo    GGPO_SHARED_LIB=%GGPO_SHARED_LIB%

cmake -G "Visual Studio 17 2022" -A x64 -B build -DBUILD_SHARED_LIBS=%GGPO_SHARED_LIB%

:: run following in wsl
:: cmake -G "Visual Studio 17 2022" -A Win32 -B build -DBUILD_SHARED_LIBS=off
:: & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -G "Visual Studio 17 2022" -A Win32 -B build -DBUILD_SHARED_LIBS=off

echo Finished!  Open build/GGPO.sln in Visual Studio to build.

IF "%1"=="--no-prompt" goto :done
:: pause so the user can see the output if they double clicked the configure script
pause 

:done
