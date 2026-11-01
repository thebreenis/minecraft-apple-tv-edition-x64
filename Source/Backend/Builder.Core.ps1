Set-StrictMode -Version 2
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.Security

function Write-Step([string]$Text) { Write-Host ('  > ' + $Text) -ForegroundColor Cyan }
function Write-Good([string]$Text) { Write-Host ('  [OK] ' + $Text) -ForegroundColor Green }
function Read-Yes([string]$Text) { return (Read-Host ('  ' + $Text + ' [y/N]')) -match '^(?i)y(es)?$' }
function Test-Administrator {
    return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Get-SdkTool([string]$Name) {
    foreach ($root in @(${env:ProgramFiles(x86)}, $env:ProgramFiles) | Where-Object { $_ }) {
        $bin = Join-Path $root 'Windows Kits\10\bin'
        if (-not (Test-Path -LiteralPath $bin)) { continue }
        foreach ($v in @(Get-ChildItem -LiteralPath $bin -Directory | Where-Object Name -Match '^10\.0\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending)) {
            $path = Join-Path $v.FullName ('x64\' + $Name)
            if (Test-Path -LiteralPath $path) { return $path }
        }
    }
    return $null
}
function Get-OfflineHelp {
    return @'
Manual setup (download on another PC if offline):
1. Install Microsoft's Windows SDK: https://developer.microsoft.com/windows/downloads/windows-sdk/
   Include the signing/packaging tools and UWP tools: makeappx.exe,
   signtool.exe and makepri.exe (all x64). Restart this builder afterwards.
2. For automatic SDK installation, install Microsoft's App Installer/WinGet:
   https://aka.ms/getwinget
3. Keep the two Microsoft-signed framework APPXs in Dependencies with this
   builder. Desktop VC++ redistributable EXEs or UWPDesktop VCLibs cannot
   replace Microsoft.VCLibs.140.00 UWP. See README.md for exact filenames.
4. Supply the supported Windows APPX and decrypted Apple TV TIPA locally. This tool does not
   download games. Building works offline once the SDK is installed.
'@
}
function Ensure-Sdk {
    $missing = @('makeappx.exe','signtool.exe','makepri.exe' | Where-Object { -not (Get-SdkTool $_) })
    if (-not $missing.Count) { return }
    Write-Host ('  Required SDK tools missing: ' + ($missing -join ', ')) -ForegroundColor Yellow
    if (-not (Read-Yes 'Automatically obtain dependencies using WinGet and accept default installation agreements?')) { throw (Get-OfflineHelp) }
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $null = Invoke-WebRequest -UseBasicParsing -Uri 'https://www.microsoft.com' -Method Head -TimeoutSec 15
    } catch { throw ('Unable to reach the internet. ' + [Environment]::NewLine + (Get-OfflineHelp)) }
    if (-not (Test-Administrator)) { throw 'ELEVATION_REQUIRED: installing the Windows SDK. Restart elevated to start over.' }
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $winget) {
        Write-Step 'Obtaining Microsoft WinGet...'
        $null = Install-PackageProvider -Name NuGet -MinimumVersion 2.8.5.201 -Force -Scope CurrentUser
        Install-Module Microsoft.WinGet.Client -Scope CurrentUser -Repository PSGallery -Force -AllowClobber -Confirm:$false
        Import-Module Microsoft.WinGet.Client
        Repair-WinGetPackageManager -Latest | Out-Host
        $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
        if (-not $winget) { throw (Get-OfflineHelp) }
    }
    Write-Step 'Installing Windows SDK with default options. This can take several minutes...'
    & $winget.Source install --id Microsoft.WindowsSDK.10.0.26100 --exact --source winget --silent --accept-source-agreements --accept-package-agreements --disable-interactivity | Out-Host
    if ($LASTEXITCODE -ne 0) { throw ('SDK installation failed. ' + (Get-OfflineHelp)) }
    foreach ($name in 'makeappx.exe','signtool.exe','makepri.exe') {
        if (-not (Get-SdkTool $name)) { throw (Get-OfflineHelp) }
    }
}
function Invoke-Sdk([string]$Name, [string[]]$Arguments, [string]$LogRoot) {
    $tool = Get-SdkTool $Name
    if (-not $tool) { throw ('Missing SDK tool: ' + $Name) }
    $null = New-Item -ItemType Directory -Path $LogRoot -Force
    $log = Join-Path $LogRoot ($Name + '-' + [guid]::NewGuid().ToString('N') + '.log')
    # SDK output can exceed 10,000 lines; keep it in a log, show concise stages.
    & $tool @Arguments > $log 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($Name + ' failed. Details: ' + $log) }
}
function Get-ZipBytes([string]$Path, [string]$EntryName) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entry = $zip.GetEntry($EntryName)
        if (-not $entry) { throw ('Package entry missing: ' + $EntryName) }
        $stream = $entry.Open(); $memory = [IO.MemoryStream]::new()
        try { $stream.CopyTo($memory); return ,$memory.ToArray() }
        finally { $stream.Dispose(); $memory.Dispose() }
    } finally { $zip.Dispose() }
}
function Get-PackageManifest([string]$Path) {
    $bytes = Get-ZipBytes $Path 'AppxManifest.xml'
    $reader = [IO.StringReader]::new([Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF))
    $settings = [Xml.XmlReaderSettings]::new(); $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit; $settings.XmlResolver = $null
    $xmlReader = [Xml.XmlReader]::Create($reader, $settings)
    try { $doc = [xml]::new(); $doc.XmlResolver = $null; $doc.Load($xmlReader); return ,$doc }
    finally { $xmlReader.Dispose(); $reader.Dispose() }
}
function Get-SHA256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-','') }
    finally { $sha.Dispose() }
}
function Expand-SafeAppx([string]$Path, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { throw 'Extraction destination already exists.' }
    $null = New-Item -ItemType Directory -Path $Destination
    $prefix = [IO.Path]::GetFullPath($Destination).TrimEnd('\') + '\'
    $skip = @('AppxSignature.p7x','AppxBlockMap.xml','[Content_Types].xml','AppxMetadata/CodeIntegrity.cat')
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\','/')
            if ($name -in $skip -or $name.EndsWith('/')) { continue }
            if ($name.Contains(':') -or $name.StartsWith('/') -or -not $seen.Add($name)) { throw 'Unsafe or duplicate APPX entry.' }
            $target = [IO.Path]::GetFullPath((Join-Path $Destination $name))
            if (-not $target.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'APPX entry escapes its output directory.' }
            $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
        }
    } finally { $zip.Dispose() }
}
function Get-BuildCertificate($Config, [string]$Thumbprint) {
    $friendly = 'Minecraft Apple TV Builder - self-signed'
    if ($Thumbprint) { $cert = Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$Thumbprint) -ErrorAction Stop }
    else { $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.FriendlyName -ceq $friendly -and $_.Subject -ceq $Config.Publisher -and $_.HasPrivateKey -and $_.NotBefore -le (Get-Date) -and $_.NotAfter -gt (Get-Date).AddDays(7) } | Sort-Object NotAfter -Descending | Select-Object -First 1 }
    if (-not $cert) {
        Write-Step 'Creating a 10-year local signing certificate with a non-exportable key...'
        $cert = New-SelfSignedCertificate -Type Custom -Subject $Config.Publisher -FriendlyName $friendly -CertStoreLocation Cert:\CurrentUser\My -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -KeyExportPolicy NonExportable -KeyUsage DigitalSignature -NotBefore (Get-Date).AddMinutes(-10) -NotAfter (Get-Date).AddYears(10) -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3','2.5.29.19={text}CA=false')
    }
    $hasCodeSigning = @($cert.EnhancedKeyUsageList | Where-Object { $_.ObjectId -eq '1.3.6.1.5.5.7.3.3' }).Count -gt 0
    if ($cert.Subject -cne $Config.Publisher -or -not $cert.HasPrivateKey -or -not $hasCodeSigning -or $cert.NotBefore -gt (Get-Date) -or $cert.NotAfter -le (Get-Date)) { throw 'Signing certificate is invalid, has no code-signing key, or has a different publisher.' }
    return $cert
}
function Test-PackageSignature([string]$Path, [string]$Thumbprint) {
    $raw = Get-ZipBytes $Path 'AppxSignature.p7x'
    if ([Text.Encoding]::ASCII.GetString($raw,0,4) -cne 'PKCX') { throw 'Invalid APPX signature header.' }
    $cms = [Security.Cryptography.Pkcs.SignedCms]::new(); $cms.Decode([byte[]]$raw[4..($raw.Length-1)]); $cms.CheckSignature($true)
    if ($cms.SignerInfos.Count -ne 1 -or $cms.SignerInfos[0].Certificate.Thumbprint -cne $Thumbprint) { throw 'Unexpected package signer.' }
}
function Get-NextPackageVersion([string]$BaseVersion) {
    $version = [version]$BaseVersion
    $existing = Get-AppxPackage -Name Microsoft.MinecraftUWP | Select-Object -First 1
    if ($existing -and [version]$existing.Version -ge $version) {
        $old = [version]$existing.Version
        if ($old.Major -ne $version.Major -or $old.Minor -ne $version.Minor -or $old.Build -ne $version.Build) { throw 'A newer Mobile game is installed. Downgrading it automatically is not supported.' }
        if ($old.Revision -ge 65535) { throw 'Package revision limit reached.' }
        return ('{0}.{1}.{2}.{3}' -f $old.Major,$old.Minor,$old.Build,($old.Revision+1))
    }
    return $version.ToString()
}
function Get-FrameworkPaths($Config, [string]$BaseDirectory) {
    $paths = @()
    foreach ($fw in $Config.Frameworks) {
        $installed = @(Get-AppxPackage -Name $fw.Name | Where-Object { $_.Publisher -ceq $Config.Publisher -and $_.Architecture.ToString() -in @('X64','Neutral') -and [version]$_.Version -ge [version]$fw.Minimum })
        if ($installed.Count) { Write-Good ('Framework available: ' + $fw.Name); continue }
        $path = Join-Path $BaseDirectory ('Dependencies\'+$fw.File)
        if (-not (Test-Path -LiteralPath $path)) { throw ('Missing framework: ' + $fw.File + '. Restore the complete Dependencies folder. ' + (Get-OfflineHelp)) }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne $fw.SHA256) { throw ('Framework checksum mismatch: ' + $fw.File) }
        $paths += $path
    }
    return $paths
}
function Export-BuildOnlyFiles([string]$OutputDirectory, [string]$PackagePath, $Config, [string]$BaseDirectory) {
    $dependencies = Join-Path $OutputDirectory 'Dependencies'
    $null = New-Item -ItemType Directory -Path $dependencies -Force
    foreach ($framework in $Config.Frameworks) {
        $source = Join-Path $BaseDirectory ('Dependencies\'+$framework.File)
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -cne $framework.SHA256) { throw ('Framework checksum mismatch: '+$framework.File) }
        Copy-Item -LiteralPath $source -Destination (Join-Path $dependencies $framework.File)
    }
    $instructions = @'
Minecraft: Apple TV Edition x64 Edition

Keep these files together:
- {APPX}
- Minecraft-AppleTV-Edition.cer (public signing certificate)
- Dependencies folder (Microsoft-signed x64 runtime frameworks)

To install on another Windows x64 PC, run Install.cmd and accept UAC.
It verifies the files, installs the certificate/frameworks, enables loopback,
and installs the game. No worlds are deleted.

Manual alternative:
1. Install Minecraft-AppleTV-Edition.cer into Local Computer / Trusted People.
   Administrator approval is required. Do not use Trusted Root.
   If this exact certificate is already trusted, it need not be imported again.
2. Install any missing framework APPXs from Dependencies. Existing compatible
   x64 frameworks can be reused.
3. Open the main APPX with Windows App Installer and choose Install.
4. In an administrator terminal run:
   CheckNetIsolation.exe LoopbackExempt -a -n=Microsoft.MinecraftUWP_8wekyb3d8bbwe
5. Launch Minecraft: Apple TV Edition from Start. Sign into Xbox and Store
   with the same account, then complete the bridge sign-in when prompted.

The public CER is safe to share with the APPX. No PFX, password or private key
is required on the destination computer. This is a self-signed modified
package, not a Microsoft-signed release. The certificate does not grant
Store ownership or Xbox service entitlements.

If Windows cannot open the package (0x80073CF0 / 0x80070003), copy the APPX
to a simple temporary folder, rename that copy game.appx and retry there.
The GUI's Build & Install operation uses verified temporary copies itself.

SHA256.json contains transfer checksums. Build-Report.json records version
and public certificate details. The resource index is already embedded in the APPX;
no separate resource-index file is needed for installation.
'@
    $instructions.Replace('{APPX}',[IO.Path]::GetFileName($PackagePath)) | Set-Content -LiteralPath (Join-Path $OutputDirectory 'README.txt') -Encoding UTF8
    Write-Good 'Build-only files ready: APPX, public CER, runtime dependencies and installation instructions.'
}
function New-DeploymentStage([string]$PackagePath, [string[]]$DependencyPaths) {
    # The Windows deployment service can reject packages in the generated Builds
    # directory with MsixvcStagingSession/GetManifestReader 0x80070003, even though
    # the file exists and SignTool verifies it. A separate temporary source works.
    $stage = Join-Path ([IO.Path]::GetTempPath()) ('ATVDeploy-'+[guid]::NewGuid().ToString('N').Substring(0,8))
    $null = New-Item -ItemType Directory -Path $stage
    $sourceHash = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash
    $game = Join-Path $stage 'game.appx'
    Copy-Item -LiteralPath $PackagePath -Destination $game
    if ((Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash -cne $sourceHash) { throw 'Deployment staging copy failed integrity verification.' }
    $dependencies = @()
    for ($i=0; $i -lt $DependencyPaths.Count; $i++) {
        $source = $DependencyPaths[$i]
        $target = Join-Path $stage ('framework'+$i+'.appx')
        Copy-Item -LiteralPath $source -Destination $target
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -cne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) { throw 'Framework staging copy failed integrity verification.' }
        $dependencies += $target
    }
    return [pscustomobject]@{ PackagePath=$game; DependencyPaths=$dependencies; Directory=$stage }
}
function Install-BuiltGame([string]$Package, [string]$CertificatePath, $Certificate, $Config, [string]$BaseDirectory, [string]$LogRoot) {
    $existing = Get-AppxPackage -Name Microsoft.MinecraftUWP | Select-Object -First 1
    if ($existing -and -not (Test-Path -LiteralPath (Join-Path $existing.InstallLocation 'XINPUTATV.dll'))) { throw 'A different Windows Minecraft installation uses this package identity. Back up your worlds and resolve it manually; nothing was removed.' }
    if (-not (Test-Administrator)) { throw 'ELEVATION_REQUIRED: enabling the game loopback exemption.' }
    $trustPath = 'Cert:\LocalMachine\TrustedPeople\'+$Certificate.Thumbprint
    if (-not (Test-Path -LiteralPath $trustPath)) {
        if (-not (Test-Administrator)) { throw 'ELEVATION_REQUIRED: trusting the public signing certificate. Restart elevated to start over.' }
        Write-Step 'Installing the public certificate in Local Computer / Trusted People...'
        Import-Certificate -FilePath $CertificatePath -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
        (Get-Item -LiteralPath $trustPath).FriendlyName = 'Minecraft Apple TV Builder - self-signed'
        Write-Good 'Certificate installed! Existing certificates were kept.'
    } else { Write-Good 'Certificate already installed! Exact fingerprint matches.' }
    Invoke-Sdk 'signtool.exe' @('verify','/pa',$Package) $LogRoot
    $dependencies = @(Get-FrameworkPaths $Config $BaseDirectory)
    Write-Step 'Preparing a verified temporary deployment copy...'
    $deployment = New-DeploymentStage $Package $dependencies
    Write-Step ('Deployment source: ' + $deployment.Directory)
    Write-Step 'Installing Minecraft: Apple TV Edition. Windows reports deployment progress...'
    if ($deployment.DependencyPaths.Count) { Add-AppxPackage -Path $deployment.PackagePath -DependencyPath $deployment.DependencyPaths }
    else { Add-AppxPackage -Path $deployment.PackagePath }
    & "$env:SystemRoot\System32\CheckNetIsolation.exe" LoopbackExempt -a -n=Microsoft.MinecraftUWP_8wekyb3d8bbwe | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Loopback setup failed. Re-run installation elevated.' }
    $expected = (Get-PackageManifest $Package).Package.Identity.Version
    $installed = Get-AppxPackage -Name Microsoft.MinecraftUWP | Where-Object Version -EQ $expected | Select-Object -First 1
    if (-not $installed -or $installed.Status.ToString() -ne 'Ok') { throw 'The expected package was not found in a healthy installed state.' }
    Write-Good 'Minecraft: Apple TV Edition installed!'
    if (Read-Yes 'Launch now?') { Start-Process explorer.exe -ArgumentList ('shell:AppsFolder\'+$installed.PackageFamilyName+'!Launcher') }
}
