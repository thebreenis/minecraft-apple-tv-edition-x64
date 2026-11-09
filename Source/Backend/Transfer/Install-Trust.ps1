param([ValidateSet('Certificate','Loopback')][string]$Phase='Certificate', [string]$LogPath)
$ErrorActionPreference='Stop'
try {
    if($Phase -eq 'Certificate') {
        $path=Join-Path $PSScriptRoot 'Minecraft-AppleTV-Edition.cer'
        $cert=New-Object Security.Cryptography.X509Certificates.X509Certificate2($path)
        if($cert.Thumbprint -ne '{CERT_THUMBPRINT}' -or $cert.HasPrivateKey){throw 'Unexpected certificate.'}
        Import-Certificate -FilePath $path -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
    } else {
        $output=& "$env:SystemRoot\System32\CheckNetIsolation.exe" LoopbackExempt -a '-n=Microsoft.MinecraftUWP_8wekyb3d8bbwe' 2>&1
        $code=$LASTEXITCODE
        if($code -ne 0){throw "Loopback exemption failed (exit $code): $($output -join [Environment]::NewLine)"}
    }
    if($LogPath){[IO.File]::WriteAllText($LogPath, "$Phase setup completed.")}
    exit 0
} catch {
    $message=$_.ToString()
    if($LogPath){[IO.File]::WriteAllText($LogPath,$message)}
    [Console]::Error.WriteLine($message)
    exit 1
}