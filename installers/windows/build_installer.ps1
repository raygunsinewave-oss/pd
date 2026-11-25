# Builds PureDevotion_Setup.exe with Inno Setup and signs it when a certificate is supplied.
#   WIN_CERT_PFX_BASE64 / WIN_CERT_PASSWORD  (Authenticode certificate, optional)
param(
    [Parameter(Mandatory = $true)][string]$ArtefactDir,
    [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$art = Resolve-Path $ArtefactDir

if (-not (Get-Command iscc -ErrorAction SilentlyContinue)) {
    choco install innosetup -y --no-progress | Out-Null
    $env:Path += ";C:\Program Files (x86)\Inno Setup 6"
}

$sign = $false
if ($env:WIN_CERT_PFX_BASE64) {
    $pfx = Join-Path $env:RUNNER_TEMP "cert.pfx"
    [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:WIN_CERT_PFX_BASE64))
    $signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" | Select-Object -Last 1
    $sign = $true
    function Sign-File($f) { & $signtool.FullName sign /f $pfx /p $env:WIN_CERT_PASSWORD /fd sha256 /tr http://timestamp.digicert.com /td sha256 $f }
    Get-ChildItem "$art\VST3" -Recurse -Filter *.vst3 -File | ForEach-Object { Sign-File $_.FullName }
    Sign-File "$art\Standalone\Pure Devotion.exe"
} else {
    Write-Host "(unsigned: WIN_CERT_PFX_BASE64 not set)"
}

iscc "/DArtefactDir=$art" "/DContentDir=$root\content" "/O$OutDir" "$PSScriptRoot\PureDevotion.iss"
if ($sign) { Sign-File (Join-Path $OutDir "PureDevotion_Setup.exe") }
Write-Host "Built $OutDir\PureDevotion_Setup.exe"
