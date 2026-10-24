# Remote game console: talks to CoopMod's console port (127.0.0.1:27780) of a running Spore.
# Usage:
#   .\console.ps1                         interactive: type console commands, see the game's console output; exit or Ctrl+C to quit
#   .\console.ps1 "coop status"           run one or more commands, print what the game answers, exit
#   .\console.ps1 "coop host" -Wait 5     same, but listen 5 seconds for the answer (default 2)
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Command,
    [double]$Wait = 2,
    [int]$Port = 27780,
    [string]$Run  # interactive mode only: commands (separated by ;) sent right after connecting, e.g. -Run "coop host"
)

$ErrorActionPreference = 'Stop'
$client = New-Object Net.Sockets.TcpClient
try { $client.Connect('127.0.0.1', $Port) }
catch { Write-Host "No game console on port $Port. Is Spore running with CoopMod?" -ForegroundColor Red; exit 1 }
$stream = $client.GetStream()
$writer = New-Object IO.StreamWriter($stream); $writer.AutoFlush = $true; $writer.NewLine = "`n"

if ($Command) {
    $reader = New-Object IO.StreamReader($stream)
    $null = $reader.ReadLine()  # greeting
    foreach ($c in $Command) { $writer.WriteLine($c) }
    $until = (Get-Date).AddSeconds($Wait)
    $pending = $reader.ReadLineAsync()
    $got = 0
    while ((Get-Date) -lt $until) {
        if ($pending.Wait(100)) {
            if ($null -eq $pending.Result) { break }
            Write-Host $pending.Result
            $got++
            $pending = $reader.ReadLineAsync()
        }
    }
    $client.Close()
    if (-not $got) {
        # the commands stay queued and run once the game window is restored
        Write-Host 'No answer: a minimized game runs no frames (fullscreen Spore minimizes when it loses focus).' -ForegroundColor Yellow
        Write-Host 'Start it windowed: launch_spore_mod.bat does that (-w).' -ForegroundColor Yellow
    }
    exit 0
}

# interactive: a background runspace prints whatever the game sends while the prompt waits for input
$ps = [PowerShell]::Create()
$null = $ps.AddScript({
    param($s)
    $r = New-Object IO.StreamReader($s)
    while ($null -ne ($line = $r.ReadLine())) { [Console]::WriteLine($line) }
    [Console]::WriteLine('(game closed the console)')
}).AddArgument($stream)
$null = $ps.BeginInvoke()
Write-Host 'Spore console. Commands: coop host | coopjoin <steamid> | coop status | coop off | stage creature | exit' -ForegroundColor Cyan
foreach ($c in ($Run -split ';')) {
    if ($c.Trim()) { $writer.WriteLine($c.Trim()) }
}
try {
    while ($client.Connected) {
        $line = Read-Host 'spore'
        if ($line -eq 'exit') { break }
        $writer.WriteLine($line)
    }
}
finally {
    $client.Close()
    $ps.Dispose()
}
