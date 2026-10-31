# Remote game console: talks to CoopMod's console port (127.0.0.1:27780) of a running Spore.
# Usage:
#   .\console.ps1                         interactive: type console commands (Tab completes, Up/Down: history),
#                                         see the game's console output; exit or Ctrl+C to quit
#   .\console.ps1 "coop status"           run one or more commands, print what the game answers, exit
#   .\console.ps1 "coop dump; coop status" several commands in one line are separated by ; (also at the prompt)
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

# The game runs one command per line; "coop dump; coop status" is split here into two.
function Send-Commands([string]$text) {
    foreach ($c in ($text -split ';')) {
        if ($c.Trim()) { $writer.WriteLine($c.Trim()) }
    }
}

if ($Command) {
    $reader = New-Object IO.StreamReader($stream)
    $null = $reader.ReadLine()  # greeting
    foreach ($c in $Command) { Send-Commands $c }
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

# interactive: a line editor with Tab completion and history; a background runspace prints whatever the game sends and
# redraws the line being typed under it
$state = [hashtable]::Synchronized(@{ Prompt = 'spore> '; Buffer = ''; Cursor = 0; Lock = New-Object Object; Editing = $false })

# Draws "prompt + buffer" on the current console line and puts the cursor where it belongs. Call with $state.Lock held.
function Show-Line {
    $text = $state.Prompt + $state.Buffer
    $width = [Math]::Max(1, [Console]::BufferWidth - 1)
    if ($text.Length -gt $width) { $text = $text.Substring($text.Length - $width) }
    [Console]::Write("`r" + $text + (' ' * [Math]::Max(0, $width - $text.Length)))
    $col = [Math]::Min($width, $state.Prompt.Length + $state.Cursor)
    try { [Console]::CursorLeft = $col } catch { }
}

$ps = [PowerShell]::Create()
$null = $ps.AddScript({
    param($s, $state)
    $r = New-Object IO.StreamReader($s)
    while ($null -ne ($line = $r.ReadLine())) {
        [Threading.Monitor]::Enter($state.Lock)
        try {
            if ($state.Editing) {
                $width = [Math]::Max(1, [Console]::BufferWidth - 1)
                [Console]::Write("`r" + (' ' * $width) + "`r")
                [Console]::WriteLine($line)
                $text = $state.Prompt + $state.Buffer
                if ($text.Length -gt $width) { $text = $text.Substring($text.Length - $width) }
                [Console]::Write($text)
                try { [Console]::CursorLeft = [Math]::Min($width, $state.Prompt.Length + $state.Cursor) } catch { }
            }
            else { [Console]::WriteLine($line) }
        }
        finally { [Threading.Monitor]::Exit($state.Lock) }
    }
    [Console]::WriteLine('')
    [Console]::WriteLine('(the game closed, closing this window)')
    Start-Sleep -Seconds 1
    [Environment]::Exit(0)  # the prompt is waiting for a key: end the whole window together with the game
}).AddArgument($stream).AddArgument($state)
$null = $ps.BeginInvoke()

# what Tab completes: every console command of the mod, with a short help text
$commands = @(
    @('coop host', 'host a session over Steam, prints your Steam ID'),
    @('coopjoin ', '<steamid> | last | <number> | <name>: join a host'),
    @('coop leave', 'co-op launch: leave the friend''s world, the game restarts in your own world'),
    @('coop friends', 'the hosts you joined before (for coopjoin last / <number> / <name>)'),
    @('coop status', 'connection, packets, puppet, shared world'),
    @('coop off', 'leave the session'),
    @('coop sendworld', 'host: send the world to the guest again'),
    @('coop noworld', 'host: do not send the world automatically'),
    @('coop restart', 'restart the game (tests the automatic restart)'),
    @('coop udphost', 'host over plain UDP, port 27777'),
    @('coopudpjoin ', '<ip>: join a UDP host'),
    @('coop move', 'puppet follows with the game''s walking (default)'),
    @('coop walk', 'puppet follows with WalkTo'),
    @('coop teleport', 'puppet is teleported (old way, no leg animation)'),
    @('coop echo', 'one-PC test: a copy of your creature follows you'),
    @('coop die', 'one-PC test: kill your creature, check the respawn at your nest and the echo puppet'),
    @('coop motionlog','log the puppet''s motion to CoopMod.log'),
    @('coop abs', 'absolute / relative puppet positions'),
    @('coop dump', 'list every entity (and CoopDump.txt)'),
    @('coop modeltest', 'check the creature model transfer on one PC'),
    @('coop killcheck', 'one PC: a mirror-like herd gets a herd type the game knows (lethal-bite crash fix)'),
    @('coop fps', 'log frames per second and window focus'),
    @('coop menu', 'open the CO-OP window'),
    @('stage cell', 'switch to the cell stage'),
    @('stage creature', 'switch to the creature stage'),
    @('stage tribe', 'switch to the tribe stage'),
    @('stage civ', 'switch to the civilization stage'),
    @('stage space', 'switch to the space stage'),
    @('coophello', 'check that the mod answers'),
    @('exit', 'close this window (the game keeps running)')
)

# coopjoin targets from the list of remembered hosts (written by the mod)
function Get-HostCandidates {
    $list = Join-Path $env:APPDATA 'Spore\Games\CoopHosts.txt'
    $result = @()
    if (Test-Path $list) {
        $n = 0
        foreach ($line in Get-Content $list) {
            $parts = $line -split "`t"
            if ($parts.Count -lt 1 -or -not $parts[0]) { continue }
            $n++
            $name = if ($parts.Count -ge 3 -and $parts[2]) { $parts[2] } else { $parts[0] }
            if ($n -eq 1) { $result += , @('coopjoin last', "join $name (the last host)") }
            $result += , @("coopjoin $n", "join $name")
            if ($parts.Count -ge 3 -and $parts[2] -and $parts[2] -notmatch '\s') { $result += , @("coopjoin $($parts[2])", "join $name") }
        }
    }
    return $result
}

# Tab: complete the command after the last ';'; with several matches complete their common start, and when that does
# not add anything, list them with their help text.
function Complete-Line {
    $buf = $state.Buffer.Substring(0, $state.Cursor)
    $rest = $state.Buffer.Substring($state.Cursor)
    $cut = $buf.LastIndexOf(';') + 1
    $head = $buf.Substring(0, $cut)
    $segment = $buf.Substring($cut)
    $lead = $segment.Length - $segment.TrimStart().Length
    $typed = $segment.TrimStart()
    $all = @(Get-HostCandidates) + $commands
    $found = @($all | Where-Object { $_[0].StartsWith($typed, [StringComparison]::OrdinalIgnoreCase) })
    if ($found.Count -eq 0) { return }
    if ($found.Count -eq 1) { $completion = $found[0][0] }
    else {
        $completion = $found[0][0]
        foreach ($m in $found) {
            $k = 0
            while ($k -lt $completion.Length -and $k -lt $m[0].Length -and [char]::ToLower($completion[$k]) -eq [char]::ToLower($m[0][$k])) { $k++ }
            $completion = $completion.Substring(0, $k)
        }
    }
    if ($completion.Length -gt $typed.Length) {
        $newHead = $head + (' ' * $lead) + $completion
        $state.Buffer = $newHead + $rest
        $state.Cursor = $newHead.Length
        Show-Line
        return
    }
    if ($found.Count -gt 1) {
        [Console]::WriteLine('')
        $w = ($found | ForEach-Object { $_[0].Length } | Measure-Object -Maximum).Maximum + 2
        foreach ($m in $found) { Write-Host ('  ' + $m[0].PadRight($w)) -NoNewline -ForegroundColor White; Write-Host $m[1] -ForegroundColor DarkGray }
        Show-Line
    }
}

function Read-CommandLine {
    $state.Buffer = ''
    $state.Cursor = 0
    $historyIndex = $script:history.Count
    [Threading.Monitor]::Enter($state.Lock)
    try { $state.Editing = $true; Show-Line } finally { [Threading.Monitor]::Exit($state.Lock) }
    while ($true) {
        $key = [Console]::ReadKey($true)
        [Threading.Monitor]::Enter($state.Lock)
        try {
            switch ($key.Key) {
                'Enter' {
                    $state.Editing = $false
                    [Console]::WriteLine('')
                    return $state.Buffer
                }
                'Tab' { Complete-Line }
                'Backspace' {
                    if ($state.Cursor -gt 0) {
                        $state.Buffer = $state.Buffer.Remove($state.Cursor - 1, 1); $state.Cursor--; Show-Line
                    }
                }
                'Delete' {
                    if ($state.Cursor -lt $state.Buffer.Length) { $state.Buffer = $state.Buffer.Remove($state.Cursor, 1); Show-Line }
                }
                'LeftArrow' { if ($state.Cursor -gt 0) { $state.Cursor--; Show-Line } }
                'RightArrow' { if ($state.Cursor -lt $state.Buffer.Length) { $state.Cursor++; Show-Line } }
                'Home' { $state.Cursor = 0; Show-Line }
                'End' { $state.Cursor = $state.Buffer.Length; Show-Line }
                'Escape' { $state.Buffer = ''; $state.Cursor = 0; Show-Line }
                'UpArrow' {
                    if ($historyIndex -gt 0) {
                        $historyIndex--; $state.Buffer = $script:history[$historyIndex]; $state.Cursor = $state.Buffer.Length; Show-Line
                    }
                }
                'DownArrow' {
                    if ($historyIndex -lt $script:history.Count - 1) {
                        $historyIndex++; $state.Buffer = $script:history[$historyIndex]
                    }
                    else { $historyIndex = $script:history.Count; $state.Buffer = '' }
                    $state.Cursor = $state.Buffer.Length; Show-Line
                }
                default {
                    if ($key.KeyChar -and -not [char]::IsControl($key.KeyChar)) {
                        $state.Buffer = $state.Buffer.Insert($state.Cursor, [string]$key.KeyChar); $state.Cursor++; Show-Line
                    }
                }
            }
        }
        finally { [Threading.Monitor]::Exit($state.Lock) }
    }
}

Write-Host 'Spore console. Tab completes commands (twice: lists them), Up/Down: history, ; separates several commands.' -ForegroundColor Cyan
Send-Commands $Run
$script:history = New-Object Collections.Generic.List[string]
try {
    while ($client.Connected) {
        if ([Console]::IsInputRedirected) { $line = Read-Host 'spore' }  # input from a pipe: no line editor
        else { $line = Read-CommandLine }
        if ($line.Trim() -eq 'exit') { break }
        if ($line.Trim()) {
            if ($script:history.Count -eq 0 -or $script:history[$script:history.Count - 1] -ne $line) { $script:history.Add($line) }
            Send-Commands $line
        }
    }
}
finally {
    $client.Close()
    $ps.Dispose()
}
