# Structured events use Console.Out, never the success pipeline: return values
# such as XML manifests must remain intact when invoked from the GUI.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
$ProgressPreference = 'SilentlyContinue'
function Send-GuiEvent([hashtable]$Event) {
    [Console]::Out.WriteLine('@@MCGUI@@' + ($Event | ConvertTo-Json -Compress -Depth 6))
    [Console]::Out.Flush()
}
function Write-Host {
    param([Parameter(Position=0,ValueFromRemainingArguments=$true)][object[]]$Object, $ForegroundColor, $BackgroundColor, [switch]$NoNewline, [object]$Separator=' ')
    Send-GuiEvent @{type='log'; message=($Object -join $Separator)}
}
function Write-Progress {
    param([string]$Activity,[string]$Status,[int]$PercentComplete=0,[switch]$Completed)
    if ($Completed) { return }
    Send-GuiEvent @{type='progress'; message=$Status; percent=$PercentComplete}
}
function Read-Yes([string]$Text) {
    Send-GuiEvent @{type='question'; message=$Text}
    $answer = [Console]::In.ReadLine()
    if ($null -eq $answer) { throw 'The GUI connection closed while waiting for a response.' }
    return $answer -match '^(?i)y(es)?$'
}
