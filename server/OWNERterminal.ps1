# Geometry Extra Owner Terminal (admin-only)
$ErrorActionPreference = 'Stop'
try { [Console]::Title = "Geometry Extra Owner Terminal" } catch {}

$API = 'http://localhost:3000'
$DEVICE = 'owner-terminal'
$SERVER_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$tokenPath = Join-Path $env:TEMP 'ge_owner_token.txt'

function Get-Tok {
  if (Test-Path $tokenPath) { (Get-Content $tokenPath -Raw).Trim() } else { return $null }
}
function Set-Tok([string]$t) {
  if ($t) { Set-Content -Path $tokenPath -Value $t } else { Remove-Item -Path $tokenPath -Force -ErrorAction SilentlyContinue }
}
$token = Get-Tok

function Invoke-Api {
  param([string]$Path, [string]$Method = 'GET', $Body = $null, [string]$Tok = $null)
  $headers = @{ 'X-Device-ID' = $DEVICE; 'Accept' = 'application/json' }
  if ($Tok) { $headers['Authorization'] = 'Bearer ' + $Tok }
  try {
    $params = @{ Uri = $API + $Path; Method = $Method; Headers = $headers }
    if ($null -ne $Body) { $params.ContentType = 'application/json'; $params.Body = ($Body | ConvertTo-Json -Compress) }
    return Invoke-RestMethod @params
  } catch {
    $resp = $_.Exception.Response
    if ($resp) {
      try {
        $r = [IO.StreamReader]::new($resp.GetResponseStream()); $txt = $r.ReadToEnd()
        Write-Host ('  ' + $txt) -ForegroundColor Red
      } catch {
        Write-Host ('  ' + $_.Exception.Message) -ForegroundColor Red
      }
    } else {
      Write-Host ('  ' + $_.Exception.Message) -ForegroundColor Red
    }
    return $null
  }
}

function Write-Info([string]$text) { Write-Host ('  ' + $text) -ForegroundColor Gray }
function Write-Ok([string]$text) { Write-Host ('  ' + $text) -ForegroundColor Green }
function Write-Err([string]$text) { Write-Host ('  ' + $text) -ForegroundColor Red }
function Write-Warn([string]$text) { Write-Host ('  ' + $text) -ForegroundColor Yellow }

function Test-Owner {
  $token = Get-Tok
  if (-not $token) { Write-Err 'Not logged in'; return $false }
  $me = Invoke-Api -Path '/api/me' -Tok $token
  if (-not $me) { Write-Err 'Session invalid'; return $false }
  if ($me.user.role -ne 'admin') { Write-Err 'Owner access required (admin role)'; return $false }
  return $true
}

function Show-Help {
  Write-Host '  '
  Write-Host '  OWNER Commands:' -ForegroundColor Magenta
  Write-Info '  help                                 show this help'
  Write-Info '  status                               server status + session'
  Write-Info '  whoami                               current logged-in user'
  Write-Info '  login <username>                     log in (asks password)'
  Write-Info '  logout                               clear local session token'
  Write-Host '  '
  Write-Host '  Server:' -ForegroundColor Cyan
  Write-Info '  start                                start the server'
  Write-Info '  server                               show server info'
  Write-Info '  restart                              restart the server (local only)'
  Write-Info '  shutdown                             shutdown the server (local only)'
  Write-Host '  '
  Write-Host '  User Management:' -ForegroundColor Cyan
  Write-Info '  users                                list all saved accounts'
  Write-Info '  promote <username> <role>            set role: player|mod|admin'
  Write-Info '  ban <username> [reason]              ban a user'
  Write-Info '  unban <username>                     unban a user'
  Write-Info '  bans                                 list all bans'
  Write-Info '  kick <username>                      kick a user from WebSocket'
  Write-Host '  '
  Write-Host '  Other:' -ForegroundColor Cyan
  Write-Info '  clear                                clear the screen'
  Write-Info '  exit                                 quit the terminal'
  Write-Host '  '
  Write-Warn '  Owner commands require admin role login'
  Write-Host '  '
}

while ($true) {
  $line = Read-Host 'owner> '
  $line = $line.Trim()
  if (-not $line) { continue }
  $parts = $line -split '\s+'
  $cmd = $parts[0].ToLower()
  $args = @(($parts | Select-Object -Skip 1))

  switch ($cmd) {
    'help'  { Show-Help; break }
    'clear' { Clear-Host; break }
    'exit'  { Write-Ok 'bye'; exit }
    'status' {
      $r = Invoke-Api -Path '/api/accounts'
      if ($r) {
        Write-Ok 'server: RUNNING'
        Write-Info ("  device: " + $DEVICE)
        $token = Get-Tok
        if ($token) {
          $me = Invoke-Api -Path '/api/me' -Tok $token
          if ($me) {
            Write-Ok ("  session: " + $me.user.name + " (" + $me.user.role + ")")
            if ($me.user.role -eq 'admin') { Write-Ok '  access: OWNER' }
            else { Write-Warn '  access: LIMITED (admin required for owner commands)' }
          }
          else { Write-Err '  session: invalid/expired' }
        } else { Write-Info '  session: not logged in' }
      } else {
        Write-Err 'server: OFFLINE'
      }
      break
    }
    'whoami' {
      $token = Get-Tok
      if (-not $token) { Write-Info 'not logged in'; break }
      $me = Invoke-Api -Path '/api/me' -Tok $token
      if ($me) {
        Write-Ok ($me.user.name + ' (' + $me.user.role + ')')
        if ($me.user.role -eq 'admin') { Write-Ok 'Owner access: YES' }
        else { Write-Warn 'Owner access: NO' }
      }
      break
    }
    'login' {
      if ($args.Count -lt 1) { Show-Help; break }
      $name = $args[0]
      $sec = Read-Host '  password' -AsSecureString
      $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
      $pass = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
      [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
      $r = Invoke-Api -Path '/api/login' -Method 'POST' -Body @{ username = $name; password = $pass }
      if ($r) {
        Set-Tok $r.token
        Write-Ok ("logged in as " + $r.user.name + " (" + $r.user.role + ")")
        if ($r.user.role -eq 'admin') { Write-Ok 'Owner access: GRANTED' }
        else { Write-Warn 'Owner access: DENIED (admin role required)' }
      }
      break
    }
    'logout' {
      $token = Get-Tok
      if ($token) { Invoke-Api -Path '/api/logout' -Method 'POST' -Tok $token | Out-Null }
      Set-Tok $null
      Write-Ok 'logged out'
      break
    }
    'start' {
      $batPath = Join-Path $SERVER_DIR 'start.bat'
      if (-not (Test-Path $batPath)) {
        Write-Err ('start.bat not found at ' + $batPath)
        break
      }
      $proc = Get-NetTCPConnection -LocalPort 3000 -ErrorAction SilentlyContinue
      if ($proc) {
        Write-Warn 'Server is already running on port 3000'
        Write-Info 'Opening game in browser...'
        Start-Process 'http://localhost:3000/geometry-dash.html'
        break
      }
      Write-Ok 'Starting server...'
      Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', $batPath -WorkingDirectory $SERVER_DIR
      Write-Info 'Waiting for server to start...'
      Start-Sleep -Seconds 3
      $check = Invoke-Api -Path '/api/accounts'
      if ($check) {
        Write-Ok 'Server started successfully!'
        Write-Info 'Opening game in browser...'
        Start-Process 'http://localhost:3000/geometry-dash.html'
      } else {
        Write-Warn 'Server may still be starting, check manually'
      }
      break
    }
    'users' {
      if (-not (Test-Owner)) { break }
      $r = Invoke-Api -Path '/api/accounts' -Tok $token
      if ($r) {
        if (@($r.accounts).Count -eq 0) { Write-Info 'no saved accounts' }
        else {
          foreach ($a in $r.accounts) {
            $roleColor = switch ($a.role) {
              'admin' { 'Magenta' }
              'mod' { 'Yellow' }
              default { 'Gray' }
            }
            Write-Host ("  [{0}] {1} ({2})" -f $a.id, $a.username, $a.role) -ForegroundColor $roleColor
          }
        }
      }
      break
    }
    'promote' {
      if (-not (Test-Owner)) { break }
      $token = Get-Tok
      if ($args.Count -lt 2) { Show-Help; break }
      $name = $args[0]
      $role = $args[1]
      if ($role -notin @('player', 'mod', 'admin')) { Write-Err 'role must be player|mod|admin'; break }
      $r = Invoke-Api -Path '/api/promote' -Method 'POST' -Body @{ username = $name; role = $role } -Tok $token
      if ($r) { Write-Ok ('promoted ' + $r.user + ' -> ' + $r.role) }
      break
    }
    'ban' {
      if (-not (Test-Owner)) { break }
      $token = Get-Tok
      if ($args.Count -lt 1) { Show-Help; break }
      $name = $args[0]
      $reason = if ($args.Count -ge 2) { $args[1..($args.Count-1)] -join ' ' } else { '' }
      $body = @{ username = $name }
      if ($reason) { $body.reason = $reason }
      $r = Invoke-Api -Path '/api/ban' -Method 'POST' -Body $body -Tok $token
      if ($r) {
        Write-Ok ('banned ' + $r.user)
        if ($reason) { Write-Info ('  reason: ' + $reason) }
      }
      break
    }
    'unban' {
      if (-not (Test-Owner)) { break }
      $token = Get-Tok
      if ($args.Count -lt 1) { Show-Help; break }
      $name = $args[0]
      $r = Invoke-Api -Path '/api/unban' -Method 'POST' -Body @{ username = $name } -Tok $token
      if ($r) { Write-Ok ('unbanned ' + $r.user) }
      break
    }
    'bans' {
      if (-not (Test-Owner)) { break }
      $token = Get-Tok
      $r = Invoke-Api -Path '/api/bans' -Tok $token
      if ($r) {
        if (@($r.bans).Count -eq 0) { Write-Info 'no bans' }
        else {
          foreach ($b in $r.bans) {
            Write-Host ('  [{0}] {1} - banned by {2}' -f $b.id, $b.username, $b.bannedBy) -ForegroundColor Red
            if ($b.reason) { Write-Info ('    reason: ' + $b.reason) }
          }
        }
      }
      break
    }
    'kick' {
      if (-not (Test-Owner)) { break }
      $token = Get-Tok
      if ($args.Count -lt 1) { Show-Help; break }
      $name = $args[0]
      $r = Invoke-Api -Path '/api/kick' -Method 'POST' -Body @{ username = $name } -Tok $token
      if ($r) {
        Write-Ok ('kicked ' + $r.user)
        Write-Info ('  connections closed: ' + $r.kicked)
      }
      break
    }
    'server' {
      if (-not (Test-Owner)) { break }
      Write-Ok 'Server Information'
      Write-Info ('  API: ' + $API)
      Write-Info ('  Device: ' + $DEVICE)
      $token = Get-Tok
      if ($token) {
        $me = Invoke-Api -Path '/api/me' -Tok $token
        if ($me) {
          Write-Info ('  Logged in as: ' + $me.user.name + ' (' + $me.user.role + ')')
        }
      }
      $r = Invoke-Api -Path '/api/accounts' -Tok $token
      if ($r) {
        Write-Info ('  Saved accounts: ' + @($r.accounts).Count)
      }
      $b = Invoke-Api -Path '/api/bans' -Tok $token
      if ($b) {
        Write-Info ('  Banned users: ' + @($b.bans).Count)
      }
      break
    }
    'restart' {
      if (-not (Test-Owner)) { break }
      Write-Warn 'Restarting server...'
      $r = Invoke-Api -Path '/api/power-action' -Method 'POST' -Body @{ action = 'restart' } -Tok $token
      if ($r) { Write-Ok 'Server restart initiated' }
      break
    }
    'shutdown' {
      if (-not (Test-Owner)) { break }
      Write-Warn 'Shutting down server...'
      $r = Invoke-Api -Path '/api/power-action' -Method 'POST' -Body @{ action = 'shutdown' } -Tok $token
      if ($r) { Write-Ok 'Server shutdown initiated' }
      break
    }
    default {
      Write-Err ("unknown command: " + $cmd)
      Write-Info 'type "help" for commands'
    }
  }
}
