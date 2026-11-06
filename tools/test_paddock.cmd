@echo off
setlocal
for /d %%V in ("C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\*") do set "testMsvc=%%~fV"
for /d %%V in ("C:\Program Files (x86)\Windows Kits\10\Include\*") do set "testSdkVersion=%%~nxV"
set "testSdk=C:\Program Files (x86)\Windows Kits\10"
set "INCLUDE=%testMsvc%\include;%testSdk%\Include\%testSdkVersion%\ucrt;%testSdk%\Include\%testSdkVersion%\shared;%testSdk%\Include\%testSdkVersion%\um"
set "LIB=%testMsvc%\lib\x64;%testSdk%\Lib\%testSdkVersion%\ucrt\x64;%testSdk%\Lib\%testSdkVersion%\um\x64"
set "PATH=%testMsvc%\bin\Hostx64\x64;%PATH%"
if not exist "%~dp0..\.pio\test-native" mkdir "%~dp0..\.pio\test-native"
cl /nologo /EHsc /std:c++17 /I"%~dp0..\include" "%~dp0test_paddock.cpp" /Fo"%~dp0..\.pio\test-native\paddock.obj" /Fe"%~dp0..\.pio\test-native\paddock.exe"
if errorlevel 1 exit /b 1
"%~dp0..\.pio\test-native\paddock.exe"
