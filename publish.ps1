#requires -Version 7.0
param(
    [string]$MSBuildPath = '',
    [string]$UnlockerIslandRoot = (Join-Path $PSScriptRoot '../FufuLauncher.UnlockerIsland'),
    [string]$Version = '',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'),
    [string]$DllPath = '',
    [string]$CompressionDirectory = (Join-Path $PSScriptRoot 'obj/release')
)
. (Join-Path $PSScriptRoot 'scripts/Release.Common.ps1')
$release = Get-ReleaseContext $PSScriptRoot $Version
$output = [IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $output) -and @(Get-ChildItem -LiteralPath $output -Force).Count) {
    throw "Output directory must be empty; existing artifacts will not be overwritten: $output"
}
if (-not $DllPath) {
    & (Join-Path $PSScriptRoot 'build.ps1') -MSBuildPath $MSBuildPath -UnlockerIslandRoot $UnlockerIslandRoot -Version $release.Version | Out-Host
    & (Join-Path $PSScriptRoot 'scripts/Compress-Release.ps1') -Version $release.Version -WorkDirectory $CompressionDirectory | Out-Host
    $DllPath = Join-Path $CompressionDirectory 'GamepadCustomPlugin.dll'
}
$dll = (Resolve-Path -LiteralPath $DllPath).Path
Assert-DllVersion $dll $release.Version
$compression = Join-Path (Split-Path -Parent $dll) 'compression.json'
if (-not (Test-Path -LiteralPath $compression -PathType Leaf)) { throw 'A verified compression report beside the DLL is required.' }
[IO.Directory]::CreateDirectory($output) | Out-Null
$archive = Join-Path $output $release.Metadata.archive_file_name
$zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
try {
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $dll, 'GamepadCustomPlugin.dll') | Out-Null
    $writer = [IO.StreamWriter]::new($zip.CreateEntry('config.ini').Open(), [Text.UTF8Encoding]::new($false))
    try { $writer.Write($release.Config) } finally { $writer.Dispose() }
} finally { $zip.Dispose() }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'store/install.lua') -Destination $output
$release.Metadata | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'metadata.json') -Encoding utf8
Copy-Item -LiteralPath $compression -Destination $output
[ordered]@{
    id = $release.Metadata.id
    version = $release.Version
    archive_file_name = $release.Metadata.archive_file_name
    archive_sha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    dll_sha256 = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
    config_sha256 = Get-BytesSha256 ([Text.UTF8Encoding]::new($false).GetBytes($release.Config))
    metadata_sha256 = (Get-FileHash -LiteralPath (Join-Path $output 'metadata.json') -Algorithm SHA256).Hash.ToLowerInvariant()
    installer_sha256 = (Get-FileHash -LiteralPath (Join-Path $output 'install.lua') -Algorithm SHA256).Hash.ToLowerInvariant()
    compression_sha256 = (Get-FileHash -LiteralPath (Join-Path $output 'compression.json') -Algorithm SHA256).Hash.ToLowerInvariant()
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'package-hashes.json') -Encoding utf8
& (Join-Path $PSScriptRoot 'scripts/Verify-Release.ps1') -ExpectedVersion $release.Version -Directory $output
Get-Item -LiteralPath $archive
