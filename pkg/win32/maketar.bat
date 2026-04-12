@echo off
rem $Id: maketar.bat,v 1.3 2021-12-08 20:48:46 phil Exp $
rem Windows 10 comes with tar utility, but not zip?!

setlocal
set /P VERS=<version

if exist SNOBOL4 rmdir SNOBOL4 /Q /S
mkdir SNOBOL4
call pkg\win32\install.bat %CD%\SNOBOL4

tar -c -z -f windows-%VERS%.tar.gz SNOBOL4
rmdir SNOBOL4 /Q /S
