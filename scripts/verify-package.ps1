param(
    [string]$Setup,
    [Parameter(Mandatory=$true)][string]$Archive,
    [string]$SingleExe,
    [Parameter(Mandatory=$true)][string]$TemporaryRoot
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $TemporaryRoot -PathType Container)) { throw 'TemporaryRoot must already exist.' }
$archivePath = (Resolve-Path -LiteralPath $Archive).Path
$version = $null
if ($Setup) {
    $setupPath = (Resolve-Path -LiteralPath $Setup).Path
    $installDir = Join-Path $TemporaryRoot ('qiao-cad-test-' + [guid]::NewGuid().ToString('N'))
    if (Test-Path -LiteralPath $installDir) { throw 'Test installation must use a fresh directory.' }

    # TESTMODE leaves user shortcuts and installed-application registry entries untouched.
    $installer = Start-Process -FilePath $setupPath -ArgumentList @('/S', '/TESTMODE', "/D=$installDir") -Wait -PassThru
    if ($installer.ExitCode -ne 0) { throw 'Silent test install failed.' }
    $exe = Join-Path $installDir 'CAD.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Installer did not create CAD.exe.' }
    $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
    if ($version.ProductName -ne 'CAD' -or $version.OriginalFilename -ne 'CAD.exe') { throw 'EXE branding metadata is incorrect.' }
    Add-Type -AssemblyName System.Drawing
    $icon = [System.Drawing.Icon]::ExtractAssociatedIcon($exe)
    if (-not $icon) { throw 'EXE has no Windows icon.' }
    $iconBitmap = $icon.ToBitmap()
    $iconBitmap.Save((Join-Path $TemporaryRoot 'cad-exe-icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    $iconBitmap.Dispose()
    $icon.Dispose()
    if (-not (Test-Path -LiteralPath "$installDir\platforms\qwindows.dll")) { throw 'Windows Qt plugin missing.' }
    $process = Start-Process -FilePath $exe -ArgumentList '--smoke-test' -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw 'Installed CAD startup failed.' }
    $uninstaller = Start-Process -FilePath "$installDir\Uninstall.exe" -ArgumentList @('/S', '/TESTMODE') -Wait -PassThru
    if ($uninstaller.ExitCode -ne 0) { throw 'Test uninstall failed.' }
    for ($attempt=0; $attempt -lt 40 -and (Test-Path -LiteralPath $exe); $attempt++) { Start-Sleep -Milliseconds 250 }
    if (Test-Path -LiteralPath $exe) { throw 'Uninstaller did not remove the application.' }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $names = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\','/') })
    if ('CAD/CAD.exe' -notin $names -or 'CAD/platforms/qwindows.dll' -notin $names) { throw 'Portable archive is incomplete.' }
    foreach ($name in $names) {
        if ($name -match '(cad_.*\.exe|CADWorkbench\.exe|\.py$|\.cpp$|\.venv/)') { throw "Unexpected development file in archive: $name" }
    }
} finally { $zip.Dispose() }
$portableDir = Join-Path $TemporaryRoot ('qiao-cad-portable-' + [guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $archivePath -DestinationPath $portableDir
$oldPath = $env:PATH
$oldPluginPath = $env:QT_PLUGIN_PATH
$oldPlatformPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    # Prove the extracted application works without Qt/MinGW SDK directories.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = $null
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
    $portableExe = Join-Path $portableDir 'CAD\CAD.exe'
    $process = Start-Process -FilePath $portableExe -ArgumentList '--smoke-test' -WorkingDirectory $TemporaryRoot -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw 'Extracted portable CAD startup failed.' }
} finally {
    $env:PATH = $oldPath
    $env:QT_PLUGIN_PATH = $oldPluginPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldPlatformPath
}
Write-Host "Portable archive contents and extracted startup verified: $portableExe"

if ($SingleExe) {
    $singlePath = (Resolve-Path -LiteralPath $SingleExe).Path
    $singleVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($singlePath)
    if ($singleVersion.ProductName -ne 'CAD') { throw 'Single-file EXE branding metadata is incorrect.' }
    $launcher = Start-Process -FilePath $singlePath -ArgumentList '--smoke-test' -PassThru -Wait
    if ($launcher.ExitCode -ne 0) { throw 'Single-file EXE startup failed.' }
    $cache = Join-Path $env:LOCALAPPDATA "Qiao\CAD-Portable\$($singleVersion.ProductVersion)"
    for ($attempt=0; $attempt -lt 120 -and -not (Test-Path -LiteralPath "$cache\CAD.exe"); $attempt++) { Start-Sleep -Milliseconds 250 }
    if (-not (Test-Path -LiteralPath "$cache\CAD.exe")) { throw 'Single-file EXE did not extract CAD.exe.' }
    if (-not (Test-Path -LiteralPath "$cache\platforms\qwindows.dll")) { throw 'Single-file EXE did not extract Qt platform plugin.' }
    $cached = Start-Process -FilePath "$cache\CAD.exe" -ArgumentList '--smoke-test' -WorkingDirectory $cache -Wait -PassThru
    if ($cached.ExitCode -ne 0) { throw 'Extracted single-file CAD startup failed.' }
    Write-Host "Single-file portable verified: $singlePath -> $cache\CAD.exe"
}

if ($Setup) { Write-Host "Installer install/start/uninstall verified. CAD version $($version.ProductVersion)." }
