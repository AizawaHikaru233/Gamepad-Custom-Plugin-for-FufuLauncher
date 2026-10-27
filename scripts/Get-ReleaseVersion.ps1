param([string]$Version = '', [switch]$RequireVersion, [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'Release.Common.ps1')
if ($RequireVersion -and $Version -ceq '') { throw 'A version is required for manual releases.' }
$release = Get-ReleaseContext $ProjectRoot $Version
[pscustomobject]@{ Version = $release.Version; Tag = $release.Tag }
