@echo off

call "..\..\4.18\Engine\Build\BatchFiles\GenerateProjectFiles.bat" %*
exit /B %ERRORLEVEL%

:Error_BatchFileInWrongLocation
echo GenerateProjectFiles ERROR: The batch file does not appear to be located in the root UE4 directory.  This script must be run from within that directory.
pause
exit /B 1
