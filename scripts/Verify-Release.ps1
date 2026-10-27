#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$ExpectedVersion,
    [string]$Directory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist')
)
. (Join-Path $PSScriptRoot 'Release.Common.ps1')
$root = Split-Path -Parent $PSScriptRoot
$release = Get-ReleaseContext $root $ExpectedVersion
$directoryPath = (Resolve-Path -LiteralPath $Directory).Path
$metadataPath = Join-Path $directoryPath 'metadata.json'
$hashes = Get-Content -LiteralPath (Join-Path $directoryPath 'package-hashes.json') -Raw | ConvertFrom-Json
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$expectedMetadata = $release.Metadata | ConvertTo-Json -Depth 10 -Compress
if (($metadata | ConvertTo-Json -Depth 10 -Compress) -cne $expectedMetadata -or
    $hashes.id -cne $release.Metadata.id -or $hashes.version -cne $ExpectedVersion -or
    $hashes.archive_file_name -cne $release.Metadata.archive_file_name) { throw 'Package identity, store metadata or versions differ.' }
$expectedFiles = @($release.Metadata.archive_file_name, 'metadata.json', 'install.lua', 'package-hashes.json', 'compression.json')
if (@(Compare-Object $expectedFiles @(Get-ChildItem -LiteralPath $directoryPath -Force | ForEach-Object Name)).Count) {
    throw 'Unexpected release artifact contents.'
}
foreach ($item in @(
    @{ Name = $release.Metadata.archive_file_name; Hash = $hashes.archive_sha256 },
    @{ Name = 'metadata.json'; Hash = $hashes.metadata_sha256 },
    @{ Name = 'install.lua'; Hash = $hashes.installer_sha256 },
    @{ Name = 'compression.json'; Hash = $hashes.compression_sha256 }
)) {
    if ($item.Hash -cnotmatch '\A[0-9a-f]{64}\z' -or
        (Get-FileHash -LiteralPath (Join-Path $directoryPath $item.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -cne $item.Hash) {
        throw "Artifact checksum mismatch: $($item.Name)"
    }
}
$installer = [IO.File]::ReadAllText((Join-Path $directoryPath 'install.lua'))
if ($installer.Trim() -cne 'install.download_plugin("XboxGamepadPlugin")' -or
    (Get-FileHash -LiteralPath (Join-Path $root 'store/install.lua') -Algorithm SHA256).Hash.ToLowerInvariant() -cne $hashes.installer_sha256) {
    throw 'Store installer behavior or identity differs.'
}
$archive = Join-Path $directoryPath $metadata.archive_file_name
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    if ($zip.Entries.Count -ne 2 -or
        @(Compare-Object @('GamepadCustomPlugin.dll', 'config.ini') @($zip.Entries.FullName) -CaseSensitive).Count) {
        throw 'ZIP must contain exactly GamepadCustomPlugin.dll and config.ini at its root.'
    }
    $dllStream = $zip.GetEntry('GamepadCustomPlugin.dll').Open()
    $memory = [IO.MemoryStream]::new()
    try { $dllStream.CopyTo($memory); $dll = $memory.ToArray() } finally { $dllStream.Dispose(); $memory.Dispose() }
    Assert-X64Dll $dll
    if ((Get-BytesSha256 $dll) -cne $hashes.dll_sha256) { throw 'Packaged DLL SHA256 mismatch.' }
    $stream = $zip.GetEntry('config.ini').Open()
    $memory = [IO.MemoryStream]::new()
    try { $stream.CopyTo($memory); $configBytes = $memory.ToArray() } finally { $stream.Dispose(); $memory.Dispose() }
    $config = [Text.UTF8Encoding]::new($false, $true).GetString($configBytes)
    Assert-PluginConfig $config $ExpectedVersion | Out-Null
    if ($config -cne $release.Config -or (Get-BytesSha256 $configBytes) -cne $hashes.config_sha256) {
        throw 'Packaged configuration differs from the versioned repository defaults.'
    }
} finally { $zip.Dispose() }

# FileVersionInfo needs a real file. Use a unique temporary directory and remove only our own file.
$probeRoot = Join-Path ([IO.Path]::GetTempPath()) ("gamepad-release-" + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($probeRoot) | Out-Null
$probeDll = Join-Path $probeRoot 'GamepadCustomPlugin.dll'
try {
    [IO.File]::WriteAllBytes($probeDll, $dll)
    Assert-DllVersion $probeDll $ExpectedVersion
} finally {
    Remove-Item -LiteralPath $probeDll -ErrorAction Stop
    Remove-Item -LiteralPath $probeRoot -ErrorAction Stop
}
$report = Get-Content -LiteralPath (Join-Path $directoryPath 'compression.json') -Raw | ConvertFrom-Json
if ($report.version -cne $ExpectedVersion -or $report.tool -cne 'UPX 5.2.0' -or
    $report.tool_archive_sha256 -cne 'b471ebf1b7f20f4a89150264ed9a008a2a5bfd247f3c6d1184a75bb59ca08f5d' -or
    $report.options -cne '--best --ultra-brute' -or $report.integrity_test_passed -cne $true -or
    $report.decompression_test_passed -cne $true -or $report.load_test_passed -cne $true -or
    $report.compressed_sha256 -cne $hashes.dll_sha256 -or $report.compressed_bytes -ne $dll.Length -or
    $report.compressed_bytes -ge $report.original_bytes -or $report.decompressed_bytes -le 0 -or
    $report.original_sha256 -cnotmatch '\A[0-9a-f]{64}\z' -or $report.decompressed_sha256 -cnotmatch '\A[0-9a-f]{64}\z') {
    throw 'Compression verification report differs.'
}
Write-Output "Verified ${ExpectedVersion}: x64 PE DLL, DLL/config/store versions, exact ZIP contents and SHA256."
