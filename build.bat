@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
MSBuild.exe MapEditor.sln /p:Configuration=Release;Platform=x64