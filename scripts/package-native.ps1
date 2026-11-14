param(
    [string]$QtRoot = '',
    [string]$CompilerRoot = '',
    [string]$NsisRoot = '',
    [switch]$SkipBuild,
    [switch]$PortableOnly,
    [switch]$SingleExe
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $QtRoot) { $QtRoot = Join-Path $root '.toolchain\6.8.3\mingw_64' }
if (-not $CompilerRoot) { $CompilerRoot = Join-Path $root '.toolchain\Tools\mingw1310_64' }
if (-not $NsisRoot) { $NsisRoot = Join-Path $root '.toolchain\nsis\nsis-3.11' }

$cmake = Join-Path $root '.venv\Lib\site-packages\cmake\data\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$needsNsis = -not $PortableOnly
if ($needsNsis -and -not (Test-Path -LiteralPath "$NsisRoot\makensis.exe")) {
    throw 'NSIS missing. Install NSIS 3 and pass -NsisRoot, or place it under .toolchain\nsis.'
}
if (-not $SkipBuild) {
    & "$PSScriptRoot\build-native.ps1" -QtRoot $QtRoot -CompilerRoot $CompilerRoot
}
$env:PATH = "$CompilerRoot\bin;$QtRoot\bin;$env:PATH"
$build = Join-Path $root 'build\native'
if (-not (Test-Path -LiteralPath "$build\bin\CAD.exe")) { throw 'CAD.exe is missing. Build first.' }
$cache = Get-Content -LiteralPath "$build\CMakeCache.txt"
$versionLine = $cache | Where-Object { $_ -match '^CMAKE_PROJECT_VERSION:STATIC=' }
if (-not $versionLine) { throw 'Could not determine project version from CMake.' }
$version = ($versionLine -split '=', 2)[1]

# Create a fresh generated staging directory. Never package tests or stale build executables.
if (-not (Test-Path -LiteralPath $build)) { throw 'Invalid build directory.' }
$stage = Join-Path $build ('package-' + [guid]::NewGuid().ToString('N'))
$stageRoot = Join-Path $stage 'CAD'
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
& $cmake --install $build --prefix $stageRoot --config Release
if ($LASTEXITCODE -ne 0) { throw 'CMake install failed.' }
& "$QtRoot\bin\windeployqt.exe" --release --compiler-runtime --no-translations --no-opengl-sw "$stageRoot\CAD.exe"
if ($LASTEXITCODE -ne 0) { throw 'Runtime deployment failed.' }
if (-not (Test-Path -LiteralPath $root)) { throw 'Invalid project root.' }
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$archive = Join-Path $dist "CAD-$version-Windows-x64.zip"
Compress-Archive -LiteralPath $stageRoot -DestinationPath $archive -Force
$single = $null
if ($needsNsis) {
    $single = Join-Path $dist "CAD-$version.exe"
    & "$NsisRoot\makensis.exe" /V3 /WX /INPUTCHARSET UTF8 "/DINPUT_DIR=$stageRoot" "/DOUTPUT_FILE=$single" "/DVERSION=$version" "/DICON_FILE=$build\generated\branding\qiao.ico" "$root\resources\windows\portable.nsi"
    if ($LASTEXITCODE -ne 0) { throw 'Single-file portable compilation failed.' }
}
if ($needsNsis -and -not $SingleExe) {
    $setup = Join-Path $dist "CAD-$version-Setup.exe"
    & "$NsisRoot\makensis.exe" /V3 /WX /INPUTCHARSET UTF8 "/DINPUT_DIR=$stageRoot" "/DOUTPUT_FILE=$setup" "/DVERSION=$version" "/DICON_FILE=$build\generated\branding\qiao.ico" "$root\resources\windows\installer.nsi"
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
}

# Execute the exact staged application with deployed libraries, not an SDK copy.
$process = Start-Process -FilePath "$stageRoot\CAD.exe" -ArgumentList '--smoke-test' -PassThru -Wait
if ($process.ExitCode -ne 0) { throw 'Packaged application startup failed.' }
Write-Host "Portable package: $archive"
if ($single) { Write-Host "Single-file portable executable: $single" }
if ($needsNsis -and -not $SingleExe) { Write-Host "Setup executable: $setup" }
Write-Host "Staged application: $stageRoot\CAD.exe"
