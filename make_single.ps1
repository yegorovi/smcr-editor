param(
	[switch]$SkipDeploy
)

# stderr от windeployqt/enigmavbconsole нам не мешает, ловим только коды выхода
$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $here "dist"
$evb = "P:\Pickle_temp\enigma\pack.evb"
$console = "V:\pickle dayz\tech\enigmavb\enigmavbconsole.exe"
$out = Join-Path $here "smcr_editor_single.exe"

if (-not $SkipDeploy) {
	$env:PATH = "P:\Qt\Tools\mingw1310_64\bin;P:\Qt\6.11.1\mingw_64\bin;" + $env:PATH
	New-Item -ItemType Directory -Force -Path $dist | Out-Null
	Copy-Item (Join-Path $here "smcr_editor.exe") (Join-Path $dist "smcr_editor.exe") -Force
	& "P:\Qt\6.11.1\mingw_64\bin\windeployqt.exe" (Join-Path $dist "smcr_editor.exe") | Out-Null
	if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
}

& (Join-Path $here "pack_single.ps1") -Dist $dist -InputExe (Join-Path $dist "smcr_editor.exe") -OutputExe $out -OutEvb $evb
& $console $evb
if ($LASTEXITCODE -ne 0) { throw "enigmavbconsole failed" }
"OK: $out ($((Get-Item $out).Length) bytes)"
