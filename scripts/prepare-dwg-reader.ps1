param([string]$Destination = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Destination) { $Destination = Join-Path $root 'build\native\bin\tools\libredwg' }
$cache = Join-Path $root 'build\dependencies\libredwg-0.14'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$assets = @(
    @{ Name = 'libredwg-0.14-win64.zip'; Hash = '1ad7e15344d20b3426c3435b078d82fb84b35062815946b2cca9c5fc9810fea8' },
    @{ Name = 'libredwg-0.14.tar.xz'; Hash = '62ebb73b984f865960f20ed26619ea5f8789d5e3fd088fa40a2598384da81275' }
)
foreach ($asset in $assets) {
    $file = Join-Path $cache $asset.Name
    if (-not (Test-Path -LiteralPath $file)) {
        Invoke-WebRequest -Uri "https://github.com/LibreDWG/libredwg/releases/download/0.14/$($asset.Name)" -OutFile $file
    }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $asset.Hash) {
        throw "LibreDWG checksum mismatch: $file. No unverified executable will be deployed."
    }
}
$unpacked = Join-Path $cache 'win64'
Expand-Archive -LiteralPath (Join-Path $cache $assets[0].Name) -DestinationPath $unpacked -Force
$source = Join-Path $cache 'source'
New-Item -ItemType Directory -Path $source -Force | Out-Null
& tar -xf (Join-Path $cache $assets[1].Name) -C $source
if ($LASTEXITCODE -ne 0) { throw 'LibreDWG source extraction failed.' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
# Separate process runtime: never place its DLLs beside Qt's MinGW runtime.
foreach ($name in @('dwg2dxf.exe', 'libredwg-0.dll')) {
    Copy-Item -LiteralPath (Join-Path $unpacked $name) -Destination $Destination -Force
}
Copy-Item -LiteralPath (Join-Path $unpacked 'README.txt') -Destination $Destination -Force
Copy-Item -LiteralPath (Join-Path $source 'libredwg-0.14\COPYING') -Destination $Destination -Force
# Accompany the GPLv3 binary with its exact complete corresponding source.
Copy-Item -LiteralPath (Join-Path $cache $assets[1].Name) -Destination $Destination -Force
Copy-Item -LiteralPath (Join-Path $root 'resources\third-party\LibreDWG-NOTICE.txt') -Destination $Destination -Force
Write-Host "Verified LibreDWG 0.14 reader and corresponding source: $Destination"
