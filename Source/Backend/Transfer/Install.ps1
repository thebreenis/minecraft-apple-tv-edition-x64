param([switch]$CheckOnly)
$ErrorActionPreference='Stop'
if (-not [Environment]::Is64BitOperatingSystem) { throw 'This package requires 64-bit Windows.' }
$root=$PSScriptRoot
$hashes=Get-Content -LiteralPath (Join-Path $root 'SHA256.json') -Raw | ConvertFrom-Json
foreach($entry in $hashes.PSObject.Properties) {
    $path=Join-Path $root $entry.Name
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Value){throw "File verification failed: $($entry.Name)"}
}
Write-Host 'Package files verified.'
if($CheckOnly){return}
$existing=Get-AppxPackage -Name Microsoft.MinecraftUWP
if($existing -and -not (Test-Path -LiteralPath (Join-Path $existing.InstallLocation 'XINPUTATV.dll'))) {
    throw 'Another Minecraft for Windows installation uses this package identity. Back up its worlds and uninstall it yourself before installing this edition. Nothing was removed.'
}
Write-Host 'Setup will trust the included community signing certificate and enable this game to contact its local helpers.'
Write-Host 'Windows will request administrator permission for those two settings.'
$args='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $root 'Install-Trust.ps1')+'"'
$process=Start-Process powershell.exe -Verb RunAs -ArgumentList $args -PassThru -Wait -WindowStyle Hidden
if($process.ExitCode -ne 0){throw 'Certificate/loopback setup did not complete.'}
$dependencies=@()
foreach($file in Get-ChildItem -LiteralPath (Join-Path $root 'Dependencies') -Filter '*.appx') {
    $name=if($file.Name -like '*VCLibs*'){'Microsoft.VCLibs.140.00'}else{'Microsoft.Services.Store.Engagement'}
    $minimum=if($name -like '*VCLibs*'){[version]'14.0.24123.0'}else{[version]'10.0.1610.0'}
    if(-not (Get-AppxPackage -Name $name | Where-Object {$_.Architecture -eq 'X64' -and [version]$_.Version -ge $minimum})){$dependencies+=$file.FullName}
}
$app=Join-Path $root '{PACKAGE_FILENAME}'
if($dependencies.Count){Add-AppxPackage -Path $app -DependencyPath $dependencies}else{Add-AppxPackage -Path $app}
Write-Host 'Installed. Open Minecraft: Apple TV Edition from the Start menu.'
Write-Host 'Use the same account in the Xbox app and Microsoft Store before starting.'
