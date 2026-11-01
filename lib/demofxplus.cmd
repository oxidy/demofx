@echo off
REM Launcher for DemoFX Plus 1.6 (fat jar). cd so KickAss/b2 resolve next to the jar.
cd /d "%~dp0"
java -jar "%~dp0demofxplus-1.6-all.jar" %*
