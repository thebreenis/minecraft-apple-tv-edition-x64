param([switch]$CheckOnly)
$ErrorActionPreference='Stop'
if (-not [Environment]::Is64BitOperatingSystem) { throw 'This package requires 64-bit Windows.' }
$root=$PSScriptRoot
$diagnosticFolder=Join-Path $root ('Install-Diagnostics-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
trap {
    $failure=$_
    if(-not $CheckOnly){
        try {
            & (Join-Path $root 'Collect-Diagnostics.ps1') -OutputDirectory $diagnosticFolder
            ('Installer failure: '+$failure.Exception.GetType().Name+'; HRESULT: '+$failure.Exception.HResult) | Set-Content -LiteralPath (Join-Path $diagnosticFolder 'install-error.txt')
        } catch {Write-Warning 'Diagnostic collection also failed.'}
    }
    Write-Error -ErrorRecord $failure -ErrorAction Continue
    exit 1
}
$windowsBuild=[int](Get-ItemPropertyValue -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -Name CurrentBuildNumber)
if($windowsBuild -lt 17134){throw 'Windows build 17134 or newer is required.'}
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
function Invoke-ElevatedSetup([string]$Phase) {
    $logPath=Join-Path $root ('Install-'+$Phase+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.log')
    $setupArguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $root 'Install-Trust.ps1')+'" -Phase '+$Phase+' -LogPath "'+$logPath+'"'
    $process=Start-Process powershell.exe -Verb RunAs -ArgumentList $setupArguments -PassThru -Wait -WindowStyle Hidden
    if($process.ExitCode -ne 0){
        $detail=if(Test-Path -LiteralPath $logPath){Get-Content -LiteralPath $logPath -Raw}else{'No helper log was created.'}
        throw "$Phase setup did not complete (exit $($process.ExitCode)). $detail Log: $logPath"
    }
}
$certificatePath=Join-Path $root 'Minecraft-AppleTV-Edition.cer'
$certificate=New-Object Security.Cryptography.X509Certificates.X509Certificate2($certificatePath)
if($certificate.Thumbprint -ne '{CERT_THUMBPRINT}' -or $certificate.HasPrivateKey){throw 'Unexpected certificate.'}
if(-not (Test-Path -LiteralPath ('Cert:\LocalMachine\TrustedPeople\'+$certificate.Thumbprint))) {
    Write-Host 'Windows will request administrator permission to trust the included community signing certificate.'
    Invoke-ElevatedSetup 'Certificate'
}
$dependencies=@()
foreach($file in Get-ChildItem -LiteralPath (Join-Path $root 'Dependencies') -Filter '*.appx') {
    $name=if($file.Name -like '*VCLibs*'){'Microsoft.VCLibs.140.00'}else{'Microsoft.Services.Store.Engagement'}
    $minimum=if($name -like '*VCLibs*'){[version]'14.0.24123.0'}else{[version]'10.0.1610.0'}
    if(-not (Get-AppxPackage -Name $name | Where-Object {$_.Architecture -eq 'X64' -and [version]$_.Version -ge $minimum})){$dependencies+=$file.FullName}
}
$app=Join-Path $root '{PACKAGE_FILENAME}'
if($dependencies.Count){Add-AppxPackage -Path $app -DependencyPath $dependencies}else{Add-AppxPackage -Path $app}
Write-Host 'Windows will request administrator permission to enable local helper connections.'
Invoke-ElevatedSetup 'Loopback'
& (Join-Path $root 'Collect-Diagnostics.ps1') -OutputDirectory $diagnosticFolder -InstallCheck
Write-Host 'Installed. Open Minecraft: Apple TV Edition from the Start menu.'
Write-Host 'Use the same account in the Xbox app and Microsoft Store before starting.'
