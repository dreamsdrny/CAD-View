param([string]$QtRoot = '', [string]$CompilerRoot = '', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $QtRoot) { $QtRoot = Join-Path $root '.toolchain\6.8.3\mingw_64' }
if (-not $CompilerRoot) { $CompilerRoot = Join-Path $root '.toolchain\Tools\mingw1310_64' }
if (-not (Test-Path -LiteralPath "$QtRoot\lib\cmake\Qt6\Qt6Config.cmake")) { throw 'Qt SDK missing. Set -QtRoot to a Qt 6 MinGW kit.' }
if (-not (Test-Path -LiteralPath "$CompilerRoot\bin\g++.exe")) { throw 'MinGW missing. Set -CompilerRoot to the matching compiler.' }
$cmake = Join-Path $root '.venv\Lib\site-packages\cmake\data\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$env:PATH = "$CompilerRoot\bin;$QtRoot\bin;$root\.venv\Scripts;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot\plugins\platforms"
$build = Join-Path $root 'build\native'
if (-not (Test-Path -LiteralPath $root)) { throw 'Invalid project root.' }
& $cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$CompilerRoot/bin/g++.exe"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
if (-not $SkipTests) {
    & $ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
}
$bin = Join-Path $build 'bin'
& "$QtRoot\bin\windeployqt.exe" --release --compiler-runtime --no-translations --no-opengl-sw "$bin\CADWorkbench.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
Write-Host "Ready: $bin\CADWorkbench.exe"
