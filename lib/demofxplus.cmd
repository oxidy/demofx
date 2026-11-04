@echo off
REM Launcher for DemoFX Plus 1.8 (fat jar). cd so KickAss/b2 resolve next to the jar.
cd /d "%~dp0"
java -jar "%~dp0demofxplus-1.8-all.jar" %*
