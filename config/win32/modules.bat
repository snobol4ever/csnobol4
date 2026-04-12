@echo off
rem $Id: modules.bat,v 1.13 2020-10-20 18:28:18 phil Exp $
rem invoked from ntmsvc.mak

setlocal
set CMD=%1
if "%CMD%" == "" set CMD=build

set TOP=%CD%
set SNOPATH=%TOP%;%TOP%\snolib;%TOP%\config\win32
rem set V=-v
set SETUP=..\..\snobol4.exe -N setup.sno %V%

cd modules\base64
%SETUP% %CMD%

cd ..\com
%SETUP% %CMD%

cd ..\dirs
%SETUP% %CMD%

cd ..\logic
%SETUP% %CMD%

cd ..\ndbm
%SETUP% %CMD%

cd ..\random
%SETUP% %CMD%

cd ..\readline
%SETUP% %CMD%

cd  ..\sprintf
%SETUP% %CMD%

cd ..\sqlite3
if EXIST sqlite3.c %SETUP% %CMD%

cd ..\stat
%SETUP% %CMD%

cd ..\time
%SETUP% %CMD%

cd ..\..

