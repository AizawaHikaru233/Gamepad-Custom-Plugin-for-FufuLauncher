#requires -Version 7.0
param([Parameter(Mandatory)][string]$DllPath)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess -or (Get-Process -Id $PID).ProcessName -in @('YuanShen', 'GenshinImpact')) {
    throw 'The loader probe requires a separate x64 PowerShell process outside the game.'
}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GamepadDllProbe {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryW(string path);
}
'@
$module = [GamepadDllProbe]::LoadLibraryW((Resolve-Path -LiteralPath $DllPath).Path)
if ($module -eq [IntPtr]::Zero) { throw "LoadLibrary failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
# Worker performs configuration setup, then returns before MinHook initialization
# for hosts other than YuanShen.exe/GenshinImpact.exe (src/Plugin.cpp).
# ConfigWatcher's destructor cancels its pending directory notification on return.
# Live FreeLibrary is unsupported: keep the module loaded until this process exits.
Start-Sleep -Milliseconds 750
Write-Output 'Packed DLL loaded in an isolated non-game host; exiting the probe process.'
