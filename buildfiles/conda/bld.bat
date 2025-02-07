set N_ARCH=\
set LIBRARY_INC=C:\Users\pikerc\git\vcpkg\installed\x64-windows-static\include
set LIBRARY_LIB=C:\Users\pikerc\git\vcpkg\installed\x64-windows-static\lib
set CSPICE_INC=C:\opt\cspice\include
set CSPICE_LIB=C:\opt\cspice\lib\cspice.lib
set CDF_INC=C:\opt\cdf\include
set CDF_LIB=C:\opt\cdf\lib\libcdf.lib

nmake.exe /nologo /f buildfiles\Windows.mak clean

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 1
)

nmake.exe /nologo /f buildfiles\Windows.mak SPICE=yes CDF=yes build

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 2
)

nmake.exe /nologo /f buildfiles\Windows.mak SPICE=yes CDF=yes run_test

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 3
)

nmake.exe /nologo /f buildfiles\Windows.mak SPICE=yes CDF=yes test_spice

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 3
)

nmake.exe /nologo /f buildfiles\Windows.mak SPICE=yes CDF=yes test_cdf

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 3
)

nmake.exe /nologo /f buildfiles\Windows.mak SPICE=yes CDF=yes install

if %ERRORLEVEL% NEQ 0 (
	EXIT /B 4
)
