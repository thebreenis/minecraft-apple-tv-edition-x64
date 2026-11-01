[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$AppleTVTipa,
    [Parameter(Mandatory=$true)][string]$DesktopAppx,
    [switch]$BuildOnly,
    [switch]$GuiWorker,
    [string]$OutputDirectory,
    [string]$CertificateThumbprint,
    [string]$OriginalUserSid
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Builder.Core.ps1')
if ($GuiWorker) { . (Join-Path $PSScriptRoot 'GuiBridge.ps1') }
$stage=$null
function Request-Elevation([string]$Reason) {
    if ($GuiWorker) { Send-GuiEvent @{type='elevation';message=$Reason}; return }
    throw ('Run the builder as administrator: '+$Reason)
}
try {
    if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Windows x64 and 64-bit PowerShell are required.' }
    if ([Environment]::OSVersion.Version.Build -lt 17134) { throw 'Windows 10 build 17134 or newer is required.' }
    $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    if ($OriginalUserSid -and $OriginalUserSid -ne $sid) { throw 'Elevation changed the Windows account. Restart with the original account.' }
    $config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Build.json') -Raw | ConvertFrom-Json
    if (-not $BuildOnly -and -not (Test-Administrator)) { Request-Elevation 'certificate trust, loopback access and installation'; exit 0 }
    Write-Progress -Activity 'Apple TV Builder' -Status 'Verifying original input packages' -PercentComplete 5
    $DesktopAppx=(Resolve-Path -LiteralPath $DesktopAppx).Path
    $AppleTVTipa=(Resolve-Path -LiteralPath $AppleTVTipa).Path
    foreach ($inputPair in @(@($DesktopAppx,$config.DesktopSHA256),@($AppleTVTipa,$config.AppleTVSHA256))) {
        if ((Get-FileHash -LiteralPath $inputPair[0] -Algorithm SHA256).Hash -cne $inputPair[1]) { throw ('Unsupported input checksum: '+[IO.Path]::GetFileName($inputPair[0])+'. Supply the exact supported original; see README.') }
    }
    $manifest=Get-PackageManifest $DesktopAppx
    if ($manifest.Package.Identity.Name -cne 'Microsoft.MinecraftUWP' -or $manifest.Package.Identity.ProcessorArchitecture -cne 'x64' -or $manifest.Package.Identity.Version -cne $config.InputVersion -or $manifest.Package.Identity.Publisher -cne $config.Publisher) { throw 'Desktop APPX identity mismatch.' }
    Ensure-Sdk
    $root=Join-Path $env:LOCALAPPDATA 'MinecraftAppleTVBuilder'
    $logs=Join-Path $root 'Logs'
    Invoke-Sdk 'signtool.exe' @('verify','/pa',$DesktopAppx) $logs
    # Verify the bundled authored helpers and recipes before using them.
    $integrity=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'PayloadHashes.json') -Raw | ConvertFrom-Json
    foreach ($entry in $integrity) {
        $path=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $entry.path))
        if (-not $path.StartsWith([IO.Path]::GetFullPath($PSScriptRoot)+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid embedded payload path.' }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $entry.sha256) { throw ('Builder payload integrity failure: '+$entry.path) }
    }
    if (-not $BuildOnly) {
        $existing=Get-AppxPackage -Name Microsoft.MinecraftUWP | Select-Object -First 1
        if ($existing -and -not (Test-Path -LiteralPath (Join-Path $existing.InstallLocation 'XINPUTATV.dll'))) { throw 'An unrelated Windows Minecraft installation shares this identity. Back up your worlds and resolve that installation manually. Nothing was removed.' }
    }
    $version=if ($BuildOnly) { $config.OutputVersion } else { Get-NextPackageVersion $config.OutputVersion }
    if (-not $OutputDirectory) { $OutputDirectory=Join-Path $root ('Builds\'+[guid]::NewGuid().ToString('N')) }
    $OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output folder; existing files will not be overwritten.' }
    $null=New-Item -ItemType Directory -Path $OutputDirectory
    $stage=Join-Path $root ('Work\'+[guid]::NewGuid().ToString('N'))
    Write-Progress -Activity 'Apple TV Builder' -Status 'Reconstructing game, UI and Apple TV artwork' -PercentComplete 20
    & (Join-Path $PSScriptRoot 'Reconstruct.exe') --desktop $DesktopAppx --apple $AppleTVTipa --backend $PSScriptRoot --stage $stage | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw 'Reconstruction failed. See the preceding message; original inputs are unchanged.' }
    if ($version -ne $config.OutputVersion) {
        $path=Join-Path $stage 'AppxManifest.xml'
        $doc=[xml]::new();$doc.XmlResolver=$null;$doc.Load($path);$doc.Package.Identity.SetAttribute('Version',$version);$doc.Save($path)
    }
    $cert=Get-BuildCertificate $config $CertificateThumbprint
    $cer=Join-Path $OutputDirectory 'Minecraft-AppleTV-Edition.cer'
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null
    $appx=Join-Path $OutputDirectory ('Minecraft-AppleTV-Edition_'+$version+'_x64.appx')
    Write-Progress -Activity 'Apple TV Builder' -Status 'Packaging reconstructed files' -PercentComplete 50
    Invoke-Sdk 'makeappx.exe' @('pack','/d',$stage,'/p',$appx,'/o') $logs
    Write-Progress -Activity 'Apple TV Builder' -Status 'Signing with your local certificate' -PercentComplete 75
    Invoke-Sdk 'signtool.exe' @('sign','/fd','SHA256','/sha1',$cert.Thumbprint,'/s','My',$appx) $logs
    Test-PackageSignature $appx $cert.Thumbprint
    Export-BuildOnlyFiles $OutputDirectory $appx $config $PSScriptRoot
    [ordered]@{BuilderVersion=$config.BuilderVersion;PackageVersion=$version;DesktopSHA256=$config.DesktopSHA256;AppleTVSHA256=$config.AppleTVSHA256;PackageSHA256=(Get-FileHash -LiteralPath $appx -Algorithm SHA256).Hash;CertificateThumbprint=$cert.Thumbprint;CertificateExpires=$cert.NotAfter.ToString('o');SelfSigned=$true;CreatedUTC=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'Build-Report.json') -Encoding UTF8
    foreach ($name in 'Install.cmd','Install.ps1','Install-Trust.ps1') {
        $text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot ('Transfer\'+$name)))
        $text=$text.Replace('{CERT_THUMBPRINT}',$cert.Thumbprint).Replace('{PACKAGE_FILENAME}',[IO.Path]::GetFileName($appx))
        [IO.File]::WriteAllText((Join-Path $OutputDirectory $name),$text)
    }
    $hashes=[ordered]@{}
    foreach ($file in Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File) {
        $hashes[$file.FullName.Substring($OutputDirectory.Length+1)]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    $hashes | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256.json') -Encoding UTF8
    if ($GuiWorker) { Send-GuiEvent @{type='result';folder=$OutputDirectory;package=$appx;version=$version} }
    if (-not $BuildOnly) {
        Write-Progress -Activity 'Apple TV Builder' -Status 'Installing certificate, game and loopback exemption' -PercentComplete 85
        Install-BuiltGame $appx $cer $cert $config $PSScriptRoot $logs
    }
    Write-Good ('Finished: '+$OutputDirectory)
    exit 0
} catch {
    if ($_.Exception.Message -like 'ELEVATION_REQUIRED:*' -and -not (Test-Administrator)) { Request-Elevation $_.Exception.Message; exit 0 }
    Write-Host ('Stopped: '+$_.Exception.Message)
    exit 1
} finally {
    # Only our freshly generated, exact GUID staging directory may be removed.
    if ($stage -and (Test-Path -LiteralPath $stage)) {
        $workRoot=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'MinecraftAppleTVBuilder\Work'))+'\'
        $resolved=[IO.Path]::GetFullPath($stage)
        if ($resolved.StartsWith($workRoot,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved) -match '^[0-9a-f]{32}$') { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }
}
