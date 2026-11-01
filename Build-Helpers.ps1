param([string]$Python='python.exe', [switch]$SkipNative)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$work=Join-Path $root 'work'
$null=New-Item -ItemType Directory -Force -Path $work
& $Python -m pip install -r (Join-Path $root 'requirements-build.txt')
if ($LASTEXITCODE -ne 0) { throw 'Python build dependencies failed.' }
foreach ($job in @(@('Runtime','Minecraft-AppleTV-Edition','appletv_auth_bridge.py'),@('Reconstructor','Reconstruct','reconstruct.py'))) {
    Push-Location (Join-Path $root ('Source\'+$job[0]))
    try {
        $dest=if ($job[0] -eq 'Runtime') {'..\Backend\Payload'} else {'..\Backend'}
        [string[]]$extra=if ($job[0] -eq 'Runtime') {@('--windowed','--runtime-hook','packaged_launcher_runtime.py')} else {@('--console')}
        & $Python -m PyInstaller --noconfirm --clean --onefile --icon NONE --name $job[1] --distpath $dest --workpath (Join-Path $work ($job[0]+'-build')) @extra $job[2]
        if ($LASTEXITCODE -ne 0) { throw ('Helper build failed: '+$job[0]) }
    } finally { Pop-Location }
}
if (-not $SkipNative) {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Install Visual Studio C++ Build Tools and the Windows SDK.' }
    $vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    $native=Join-Path $root 'Source\Native'
    $dll=Join-Path $root 'Source\Backend\Payload\XINPUTATV.dll'
    $cmd=@"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cd /d "$native"
ml64 /nologo /c /Fostore_ownership.obj store_ownership.asm
if errorlevel 1 exit /b 1
cl /nologo /LD /O2 /EHsc /std:c++17 /await xinput_proxy.cpp presence_filter.cpp store_ownership.obj /link /OUT:"$dll" /DEF:XINPUTUAP.def ws2_32.lib winhttp.lib wininet.lib windowsapp.lib
exit /b %errorlevel%
"@
    $script=Join-Path $work 'build-native.cmd'
    [IO.File]::WriteAllText($script,$cmd,[Text.Encoding]::ASCII)
    & cmd.exe /d /c $script
    if ($LASTEXITCODE -ne 0) { throw 'Native bridge build failed.' }
}
& (Join-Path $root 'Update-Integrity.ps1')
