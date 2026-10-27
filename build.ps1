#requires -Version 7.0
param(
    [string]$MSBuildPath = '',
    [string]$UnlockerIslandRoot = (Join-Path $PSScriptRoot '../FufuLauncher.UnlockerIsland'),
    [string]$Version = '',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'bin/x64/Release'),
    [string]$IntermediateDirectory = (Join-Path $PSScriptRoot 'obj/x64/Release')
)
. (Join-Path $PSScriptRoot 'scripts/Release.Common.ps1')
$release = Get-ReleaseContext $PSScriptRoot $Version
if (-not $MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $MSBuildPath = & $vswhere -latest -version '[18.0,19.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    }
}
if (-not $MSBuildPath -or -not (Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) {
    throw 'MSBuild not found. Install Visual Studio 2026 C++ Build Tools, or specify -MSBuildPath.'
}
$UnlockerIslandRoot = (Resolve-Path -LiteralPath $UnlockerIslandRoot).Path
foreach ($dependency in @('Patterns/Patterns.h', 'MinHook/MinHook.h', 'MinHook/libMinHook.x64.lib')) {
    $path = Join-Path $UnlockerIslandRoot "FufuLauncher.UnlockerIsland/$dependency"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required build dependency not found: $path" }
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
$intermediate = [IO.Path]::GetFullPath($IntermediateDirectory)
[IO.Directory]::CreateDirectory($output) | Out-Null
[IO.Directory]::CreateDirectory($intermediate) | Out-Null
$resource = Join-Path $intermediate 'GamepadCustomPlugin.version.rc'
$config = Join-Path $intermediate 'build-config.ini'
$parts = $release.Version.Split('.')
# PE fixed version records require four WORDs; the reserved final WORD stays zero.
# User-visible FileVersion/ProductVersion strings contain exactly the requested three parts.
$resourceText = @"
#include <winver.h>
1 VERSIONINFO
FILEVERSION $($parts -join ','),0
PRODUCTVERSION $($parts -join ','),0
FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
FILEFLAGS 0
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_DLL
FILESUBTYPE 0
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "FileDescription", "GamepadCustomPlugin"
      VALUE "FileVersion", "$($release.Version)"
      VALUE "InternalName", "GamepadCustomPlugin"
      VALUE "OriginalFilename", "GamepadCustomPlugin.dll"
      VALUE "ProductName", "GamepadCustomPlugin"
      VALUE "ProductVersion", "$($release.Version)"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x0409, 1200
  END
END
"@
# Do not disturb incremental builds when the requested version has not changed.
foreach ($generated in @(@{ Path = $resource; Text = $resourceText }, @{ Path = $config; Text = $release.Config })) {
    if (-not (Test-Path -LiteralPath $generated.Path) -or [IO.File]::ReadAllText($generated.Path) -cne $generated.Text) {
        [IO.File]::WriteAllText($generated.Path, $generated.Text, [Text.UTF8Encoding]::new($false))
    }
}
& $MSBuildPath (Join-Path $PSScriptRoot 'GamepadCustomPlugin.vcxproj') /m /t:Build /p:Configuration=Release /p:Platform=x64 "/p:UnlockerIslandRoot=$UnlockerIslandRoot" "/p:OutDir=$output/" "/p:IntDir=$intermediate/" "/p:PluginVersionResource=$resource" "/p:PluginBuildConfig=$config" /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "Plugin build failed: $LASTEXITCODE" }
$dll = Join-Path $output 'GamepadCustomPlugin.dll'
Assert-DllVersion $dll $release.Version
Get-Item -LiteralPath $dll
