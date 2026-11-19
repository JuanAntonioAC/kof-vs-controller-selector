$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
& cmd /c "`"$root\build_native.bat`""
if ($LASTEXITCODE -ne 0) { throw "build_native.bat fallo ($LASTEXITCODE)" }
dotnet publish "$root\src\Launcher\KofVsLauncher.csproj" -c Release -o "$root\dist" --nologo -v q
if ($LASTEXITCODE -ne 0) { throw "dotnet publish fallo" }
Copy-Item "$root\config\kofvs.ini" "$root\dist\kofvs.ini" -Force
Copy-Item "$root\config\launcher.cfg" "$root\dist\launcher.cfg" -Force
Copy-Item "$root\config\*.bat" "$root\dist" -Force
Copy-Item "$root\README.md", "$root\PRUEBAS.md", "$root\LEEME_STEAMDECK.md", "$root\LAYOUTS.md" "$root\dist" -Force
Get-ChildItem "$root\dist" | Select-Object Name, Length
