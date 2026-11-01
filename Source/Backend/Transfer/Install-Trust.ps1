$ErrorActionPreference='Stop'
try {
    $path=Join-Path $PSScriptRoot 'Minecraft-AppleTV-Edition.cer'
    $cert=New-Object Security.Cryptography.X509Certificates.X509Certificate2($path)
    if($cert.Thumbprint -ne '{CERT_THUMBPRINT}' -or $cert.HasPrivateKey){throw 'Unexpected certificate.'}
    Import-Certificate -FilePath $path -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
    & "$env:SystemRoot\System32\CheckNetIsolation.exe" LoopbackExempt -a -n=Microsoft.MinecraftUWP_8wekyb3d8bbwe
    if($LASTEXITCODE -ne 0){throw 'Loopback exemption failed.'}
    exit 0
} catch {Write-Error $_;exit 1}
