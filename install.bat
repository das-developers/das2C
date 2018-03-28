@echo off
:install
:: Copy first path to second path, creating output directories as needed
:: %1 - the file to read
:: %2 - the file to write
:: 

setlocal enableExtensions
set in=%1
set in=%in:/=\%
set out=%2
set out=%out:/=\%

if not exist %in% (
  echo Input file %1 missing 
  exit /b 3
)
  
:: Get the output directory name, only FOR allows the needed syntax, batch scripts
:: are the most wobbly junk pile I've ever seen. --cwp
for %%F in ( "%out%" ) do set outDir=%%~dpF

if not exist %outDir% (
  echo making directory %outDir%
  mkdir "%outDir%"
)

if "%errorlevel%" neq "0" exit /b 3

copy /V /Y /B "%in%" "%out%"

if "%errorlevel%" neq "0" exit /b 3

exit /b 0
endlocal
