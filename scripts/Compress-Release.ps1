#requires -Version 7.0
param(
    [string]$DllPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'bin/x64/Release/GamepadCustomPlugin.dll'),
    [string]$WorkDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'obj/release'),
    [string]$Version = ''
)
. (Join-Path $PSScriptRoot 'Release.Common.ps1')
$release = Get-ReleaseContext (Split-Path -Parent $PSScriptRoot) $Version
$source = (Resolve-Path -LiteralPath $DllPath).Path
Assert-DllVersion $source $release.Version
$work = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $work) { throw "Compression directory already exists: $work" }
[IO.Directory]::CreateDirectory($work) | Out-Null

# Official release: https://github.com/upx/upx/releases/tag/v5.2.0
# Pin both the version and the official attachment's SHA256.
$upxVersion = '5.2.0'
$upxHash = 'b471ebf1b7f20f4a89150264ed9a008a2a5bfd247f3c6d1184a75bb59ca08f5d'
$upxUrl = "https://github.com/upx/upx/releases/download/v$upxVersion/upx-$upxVersion-win64.zip"
$archive = Join-Path $work 'upx.zip'
Invoke-WebRequest -Uri $upxUrl -OutFile $archive
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -cne $upxHash) {
    throw 'Official UPX attachment SHA256 mismatch.'
}
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $work 'upx')
$upx = Join-Path $work "upx/upx-$upxVersion-win64/upx.exe"
$packed = Join-Path $work 'GamepadCustomPlugin.dll'
$unpacked = Join-Path $work 'GamepadCustomPlugin.unpacked.dll'
$originalSize = (Get-Item -LiteralPath $source).Length
& $upx --best --ultra-brute -o $packed $source
if ($LASTEXITCODE -ne 0) { throw 'UPX compression failed.' }
& $upx --test $packed
if ($LASTEXITCODE -ne 0) { throw 'UPX integrity test failed.' }
& $upx --decompress -o $unpacked $packed
if ($LASTEXITCODE -ne 0) { throw 'UPX decompression failed.' }
# UPX may rebuild PE/debug/import data: equal SHA256 or file length is not required.
Assert-DllVersion $unpacked $release.Version
Assert-DllVersion $packed $release.Version
$packedSize = (Get-Item -LiteralPath $packed).Length
if ($packedSize -ge $originalSize) { throw 'UPX did not reduce DLL size.' }

$start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
foreach ($arg in @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'Test-DllLoad.ps1'), '-DllPath', $packed)) {
    $start.ArgumentList.Add($arg)
}
$probe = [Diagnostics.Process]::Start($start)
try {
    if (-not $probe.WaitForExit(30000)) { $probe.Kill($true); throw 'Isolated DLL loader probe timed out.' }
    if ($probe.ExitCode -ne 0) { throw "Isolated DLL loader probe failed: $($probe.ExitCode)" }
} finally { $probe.Dispose() }
[ordered]@{
    version = $release.Version
    tool = "UPX $upxVersion"
    tool_archive_sha256 = $upxHash
    options = '--best --ultra-brute'
    original_bytes = $originalSize
    compressed_bytes = $packedSize
    decompressed_bytes = (Get-Item -LiteralPath $unpacked).Length
    original_sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    compressed_sha256 = (Get-FileHash -LiteralPath $packed -Algorithm SHA256).Hash.ToLowerInvariant()
    decompressed_sha256 = (Get-FileHash -LiteralPath $unpacked -Algorithm SHA256).Hash.ToLowerInvariant()
    integrity_test_passed = $true
    decompression_test_passed = $true
    load_test_passed = $true
    load_test_scope = 'isolated non-game x64 PowerShell process; no live FreeLibrary'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'compression.json') -Encoding utf8
Write-Output "UPX verified: $originalSize -> $packedSize bytes."
