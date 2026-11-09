param([string]$OutputDirectory, [switch]$InstallCheck)
$ErrorActionPreference='Stop'
if(-not $OutputDirectory){$OutputDirectory=Join-Path ([Environment]::GetFolderPath('Desktop')) ('Minecraft-AppleTV-Diagnostics-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$report=[ordered]@{SchemaVersion=1;CollectedUtc=[DateTime]::UtcNow.ToString('o');Checks=@();Errors=@()}
function Add-Check($name,$ok,$detail){$script:report.Checks+=@{Name=$name;Passed=[bool]$ok;Detail=$detail}}
try {
    $os=Get-CimInstance Win32_OperatingSystem
    $report.Windows=@{Caption=$os.Caption;Version=$os.Version;Build=$os.BuildNumber;Architecture=$os.OSArchitecture}
    Add-Check 'Windows minimum' ([Environment]::Is64BitOperatingSystem -and [int]$os.BuildNumber -ge 17134) 'Windows x64, build 17134 or newer required.'
} catch {$report.Errors+=('Windows inventory: '+$_.Exception.GetType().Name)}
$package=Get-AppxPackage -Name Microsoft.MinecraftUWP | Select-Object -First 1
Add-Check 'Game registered' ($null -ne $package) 'Current Windows user registration.'
if($package){
    $report.Package=@{Version=[string]$package.Version;Status=[string]$package.Status;Family=$package.PackageFamilyName}
    Add-Check 'Package healthy' ([string]$package.Status -eq 'Ok') ([string]$package.Status)
    $report.Files=@()
    foreach($name in @('Minecraft.Windows.exe','Minecraft-AppleTV-Edition.exe','XINPUTATV.dll','AppxManifest.xml')){
        $path=Join-Path $package.InstallLocation $name
        $exists=Test-Path -LiteralPath $path -PathType Leaf
        Add-Check $name $exists 'Required installed file.'
        $report.Files+=@{Name=$name;Present=$exists;SHA256=$(if($exists){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}else{''})}
    }
    try {
        $manifest=Get-AppxPackageManifest -Package $package
        $apps=@($manifest.Package.Applications.Application | ForEach-Object {@{Id=[string]$_.Id;Executable=[string]$_.Executable}})
        $report.Applications=$apps
        Add-Check 'Bridge launcher registered' (@($apps | Where-Object {$_.Id -eq 'Launcher' -and $_.Executable -eq 'Minecraft-AppleTV-Edition.exe'}).Count -gt 0) 'Start menu must invoke the Launcher entry.'
    } catch {$report.Errors+=('Manifest inventory: '+$_.Exception.GetType().Name)}
}
$report.Dependencies=@()
foreach($dep in @(@('Microsoft.VCLibs.140.00','14.0.24123.0'),@('Microsoft.Services.Store.Engagement','10.0.1610.0'))){
    $found=@(Get-AppxPackage -Name $dep[0] | Where-Object {$_.Architecture -eq 'X64'})
    $ok=@($found|Where-Object {[version]$_.Version -ge [version]$dep[1]}).Count -gt 0
    $report.Dependencies+=@{Name=$dep[0];Minimum=$dep[1];Versions=@($found | ForEach-Object {[string]$_.Version});Satisfied=$ok}
    Add-Check $dep[0] $ok 'Required x64 framework.'
}
$report.IdentityMetadata=@()
foreach($view in @([Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32)){
    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,$view)
    $values=@{}; $statuses=@{}
    try {
        foreach($spec in @(@('XboxAccountId','Software\Microsoft\XboxLive','AccountId'),@('XboxXuid','Software\Microsoft\XboxLive','Xuid'),@('XboxGamertag','Software\Microsoft\XboxLive','Gamertag'),@('StoreAccountId','Software\Microsoft\Windows\CurrentVersion\Store','PrimaryWebAccountId'))){
            $key=$null
            try {$key=$base.OpenSubKey($spec[1]); if($null -eq $key){$statuses[$spec[0]]='key_missing';continue};$v=$key.GetValue($spec[2],$null);if($null -eq $v){$statuses[$spec[0]]='value_missing'}elseif([string]::IsNullOrWhiteSpace([string]$v)){$statuses[$spec[0]]='empty'}else{$statuses[$spec[0]]='present';$values[$spec[0]]=[string]$v}}
            catch {$statuses[$spec[0]]='read_error:'+ $_.Exception.GetType().Name}
            finally {if($key){$key.Dispose()}}
        }
        $report.IdentityMetadata+=@{View=[string]$view;Fields=$statuses;XboxGamertag=$values.XboxGamertag;WindowsAccountsMatch=$(if($values.XboxAccountId -and $values.StoreAccountId){$values.XboxAccountId.Trim().ToLowerInvariant() -eq $values.StoreAccountId.Trim().ToLowerInvariant()}else{$null})}
    } finally {$base.Dispose()}
}
try {
    $loopback=& "$env:SystemRoot\System32\CheckNetIsolation.exe" LoopbackExempt -s 2>&1
    $report.Loopback=@{QueryExitCode=$LASTEXITCODE;GameListed=[bool]($loopback -match 'microsoft.minecraftuwp_8wekyb3d8bbwe')}
    Add-Check 'Loopback exemption' $report.Loopback.GameListed 'Read-only CheckNetIsolation inventory.'
} catch {$report.Errors+=('Loopback inventory: '+$_.Exception.GetType().Name)}
$report.Processes=@();$report.Ports=@()
$report.SupportingApps=@()
foreach($appName in @('Microsoft.GamingApp','Microsoft.WindowsStore','Microsoft.GamingServices')){
    $report.SupportingApps+=@{Name=$appName;Versions=@(Get-AppxPackage -Name $appName | ForEach-Object {[string]$_.Version})}
}
$report.Services=@(Get-Service -Name XblAuthManager,XblGameSave,XboxNetApiSvc,TokenBroker -ErrorAction SilentlyContinue | ForEach-Object {@{Name=$_.Name;Status=[string]$_.Status;StartType=[string]$_.StartType}})
foreach($name in @('Minecraft.Windows','Minecraft-AppleTV-Edition')){
    foreach($process in @(Get-Process -Name $name -ErrorAction SilentlyContinue)){
        $nativeLoaded=$null
        if($name -eq 'Minecraft.Windows'){try{$nativeLoaded=@($process.Modules|Where-Object ModuleName -eq 'XINPUTATV.dll').Count -gt 0}catch{}}
        $report.Processes+=@{Name=$name;PID=$process.Id;NativeBridgeLoaded=$nativeLoaded}
    }
}
try {
    $connections=@(Get-NetTCPConnection -State Listen -ErrorAction Stop)
    foreach($port in @(32115,32116,32117,32118,32121,32122,32123)){
        $listeners=@($connections|Where-Object LocalPort -eq $port)
        $report.Ports+=@{Port=$port;Listening=($listeners.Count -gt 0);Owners=@($listeners|ForEach-Object {$owner=Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue;@{PID=$_.OwningProcess;Name=$owner.ProcessName}})}
    }
    $gamePids=@($report.Processes|Where-Object Name -eq 'Minecraft.Windows'|ForEach-Object {$_.PID})
    $report.GameBridgeConnections=@(Get-NetTCPConnection -ErrorAction Stop | Where-Object {$_.OwningProcess -in $gamePids -and $_.RemoteAddress -in @('127.0.0.1','::1') -and $_.RemotePort -in @(32115,32116,32117,32118,32121,32122,32123)} | ForEach-Object {@{Port=$_.RemotePort;State=[string]$_.State;PID=$_.OwningProcess}})
} catch {$report.Errors+=('Listener inventory: '+$_.Exception.GetType().Name)}
$report.RuntimeLogs=@()
$roots=@((Join-Path $env:LOCALAPPDATA 'MinecraftAppleTV'),(Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\LocalCache\Local\MinecraftAppleTV'))
foreach($root in $roots){
    $kind=if($root -like '*\Packages\*'){'Packaged'}else{'Unpackaged'}
    $requestPath=Join-Path $root 'requests.log'
    $requestPresent=Test-Path -LiteralPath $requestPath
    $signCount=0
    if($requestPresent){$signCount=@(Get-Content -LiteralPath $requestPath -Tail 2000|Where-Object {$_ -match '\bSIGN '}).Count}
    $report.RuntimeLogs+=@{Location=$kind;RequestLogPresent=$requestPresent;RecentSigningRequests=$signCount;LastWriteUtc=$(if($requestPresent){(Get-Item -LiteralPath $requestPath).LastWriteTimeUtc.ToString('o')}else{$null});Note='Signing includes launcher probes; alone it does not prove game traffic.'}
    foreach($name in @('startup.jsonl','startup.previous.jsonl')){
        $source=Join-Path $root ('Diagnostics\'+$name)
        if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $OutputDirectory ($kind+'-'+$name))}
    }
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'diagnostics.json') -Encoding UTF8
'Read-only report. Includes Windows/package versions, installed file hashes, legacy Xbox Gamertag, account metadata availability/equality (not raw IDs), processes/listeners, and structured startup events. No tokens, passwords, email addresses, sign-in codes, saves, purchase records, raw request logs, or authentication caches. Missing metadata does not prove an app is signed out. Ports may be closed before authentication or when the game is closed.' | Set-Content -LiteralPath (Join-Path $OutputDirectory 'README.txt') -Encoding UTF8
Write-Host ('Diagnostics saved to: '+$OutputDirectory)
if($InstallCheck -and @($report.Checks | Where-Object {-not $_.Passed}).Count){throw 'Installation verification failed. See diagnostics.json in the diagnostic folder.'}
