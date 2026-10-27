#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $root 'scripts/Release.Common.ps1')
$sourceConfig = [IO.File]::ReadAllText((Join-Path $root 'config.ini'))
$sourceMetadata = [IO.File]::ReadAllText((Join-Path $root 'store/metadata.json'))
$fixture = Join-Path $root ('obj/release-context-test-' + [guid]::NewGuid().ToString('N'))
$fixtureStore = Join-Path $fixture 'store'
$fixtureConfig = Join-Path $fixture 'config.ini'
$fixtureMetadata = Join-Path $fixtureStore 'metadata.json'
[IO.Directory]::CreateDirectory($fixtureStore) | Out-Null

function Write-Fixture([string]$Config = $sourceConfig, [scriptblock]$ChangeMetadata = {}) {
    $metadata = $sourceMetadata | ConvertFrom-Json
    # Exercise different display text even when the repository currently has matching text.
    $metadata.name = 'Independent store title'
    $metadata.description = 'Independent store description'
    $metadata.developer = 'Store publisher display name'
    & $ChangeMetadata $metadata
    [IO.File]::WriteAllText($fixtureConfig, $Config)
    [IO.File]::WriteAllText($fixtureMetadata, ($metadata | ConvertTo-Json -Depth 10))
}

function Assert-Rejected([scriptblock]$Action, [string]$MessagePattern) {
    try { & $Action | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $MessagePattern) { throw }
        return
    }
    throw "Invalid fixture was accepted (expected: $MessagePattern)."
}

try {
    Write-Fixture
    $base = ($sourceMetadata | ConvertFrom-Json).version
    $release = Get-ReleaseContext $fixture
    if ($release.Version -cne $base -or $release.Metadata.name -cne 'Independent store title' -or
        $release.Config -cne $sourceConfig) { throw 'Base build must preserve independent store text and source config.' }

    $release = Get-ReleaseContext $fixture '2.3.4'
    $ini = Read-Ini $release.Config
    if ($release.Version -cne '2.3.4' -or $release.Tag -cne 'v2.3.4' -or
        $ini.General.Version -cne '2.3.4' -or $release.Metadata.version -cne '2.3.4' -or
        $release.Metadata.archive_file_name -cne 'GamepadCustomPlugin-2.3.4.zip' -or
        $release.Metadata.description -cne 'Independent store description') { throw 'Generated release versions or store text differ.' }
    if ([IO.File]::ReadAllText($fixtureConfig) -cne $sourceConfig -or
        (Get-Content -LiteralPath $fixtureMetadata -Raw | ConvertFrom-Json).version -cne $base) {
        throw 'Resolving a release must not write back source versions.'
    }

    # Exercise the exact switch form used by push/PR runs, including empty version input.
    $resolved = & (Join-Path $root 'scripts/Get-ReleaseVersion.ps1') -ProjectRoot $fixture -Version '' -RequireVersion:$false
    if ($resolved.Version -cne $base) { throw 'Push/PR version resolution differs.' }
    Assert-Rejected { & (Join-Path $root 'scripts/Get-ReleaseVersion.ps1') -ProjectRoot $fixture -Version '' -RequireVersion } 'version is required'
    Assert-Rejected { Get-ReleaseContext $fixture '1.2.3.4' } 'major.minor.patch'

    Write-Fixture -ChangeMetadata { param($metadata) $metadata.version = '9.8.7' }
    Assert-Rejected { Get-ReleaseContext $fixture '2.3.4' } 'Config DLL name or version differs'
    Write-Fixture -ChangeMetadata { param($metadata) $metadata.id = 'WrongPlugin' }
    Assert-Rejected { Get-ReleaseContext $fixture } 'plugin ID, DLL filename or base archive filename differs'
    Write-Fixture -ChangeMetadata { param($metadata) $metadata.dll_file_name = 'Other.dll' }
    Assert-Rejected { Get-ReleaseContext $fixture } 'plugin ID, DLL filename or base archive filename differs'
    Write-Fixture -ChangeMetadata { param($metadata) $metadata.archive_file_name = 'Other-1.1.0.zip' }
    Assert-Rejected { Get-ReleaseContext $fixture } 'plugin ID, DLL filename or base archive filename differs'
    Write-Fixture -Config $sourceConfig.Replace('File = GamepadCustomPlugin.dll', 'File = Other.dll')
    Assert-Rejected { Get-ReleaseContext $fixture } 'Config DLL name or version differs'

    Write-Output 'Release context regression checks passed: independent display text, versions, identity and unchanged sources.'
} finally {
    # These are the only files created by this test; no recursive deletion is needed.
    foreach ($file in @($fixtureConfig, $fixtureMetadata)) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
    Remove-Item -LiteralPath $fixtureStore
    Remove-Item -LiteralPath $fixture
    if (Test-Path -LiteralPath $fixture) { throw 'Release context test fixture was not removed.' }
}
