$ErrorActionPreference='Stop'
$backend=Join-Path $PSScriptRoot 'Source\Backend'
$entries=@(Get-ChildItem -LiteralPath $backend -Recurse -File | Where-Object Name -NE 'PayloadHashes.json' | Sort-Object FullName | ForEach-Object {
    @{path=$_.FullName.Substring($backend.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
[IO.File]::WriteAllText((Join-Path $backend 'PayloadHashes.json'),($entries | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
