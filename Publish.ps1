[CmdletBinding()]
param([string]$Destination)
$ErrorActionPreference='Stop'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1'
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
if (-not (Get-Command dotnet.exe -ErrorAction SilentlyContinue)) { throw 'Install a compatible .NET SDK on this development machine before rebuilding. End users do not need it.' }
if (-not $Destination) { $Destination=Join-Path $PSScriptRoot 'Release' }
foreach ($required in 'Source\Backend\Reconstruct.exe','Source\Backend\Payload\Minecraft-AppleTV-Edition.exe','Source\Backend\Payload\XINPUTATV.dll') {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $required))) { throw 'Run Build-Helpers.ps1 before publishing.' }
}
& (Join-Path $PSScriptRoot 'Update-Integrity.ps1')
& dotnet publish (Join-Path $PSScriptRoot 'Source\Frontend\MinecraftAppleTVBuilder.csproj') -c Release -r win-x64 --self-contained true -o $Destination
if ($LASTEXITCODE -ne 0) { throw 'Publishing failed.' }
Write-Host ('Ready: '+(Join-Path $Destination 'MinecraftAppleTVBuilder.exe')) -ForegroundColor Green
