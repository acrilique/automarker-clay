@echo off
rem Auto install AutoMarker extension to Premiere on windows

echo Downloading Adobe Extension Manager
curl "http://download.macromedia.com/pub/extensionmanager/ExManCmd_win.zip" --output %temp%\ExManCmd_win.zip
echo.

echo Using local AutoMarker extension
set "extension_path=%~dp0AutoMarker.zxp"
echo.

echo Unzip Extension Manager
rem require powershell
powershell Expand-Archive %temp%\ExManCmd_win.zip -DestinationPath %temp%\ExManCmd_win -Force
echo.

echo Install Extension
call %temp%\ExManCmd_win\ExManCmd.exe /install "%extension_path%"
if %ERRORLEVEL% NEQ 0 (
    echo Installation failed...
) else (
    echo.
    echo Installation successful !
)