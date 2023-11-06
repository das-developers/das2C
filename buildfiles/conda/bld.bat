set N_ARCH=/

nmake.exe /nologo /f buildfiles\Windows.mak clean

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 1
)

nmake.exe /nologo /f buildfiles\Windows.mak build

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 2
)

nmake.exe /nologo /f buildfiles\Windows.mak run_test

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 3
)

nmake.exe /nologo /f buildfiles\Windows.mak install

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 4
)
