# Geometry Extra backend terminal
$ErrorActionPreference = 'Stop'
try { [Console]::Title = "Geometry Extra Terminal" } catch {}

$API = 'http://localhost:3000'
$DEVICE = 'terminal'
$tokenPath = Join-Path $env:TEMP 'ge_terminal_token.txt'

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

function Show-Help {
  Write-Host '  '
  Write-Host '  Commands:' -ForegroundColor Cyan
  Write-Info '  help                               show this help'
  Write-Info '  status                             server status + session'
  Write-Info '  whoami                             current logged-in user'
  Write-Info '  login <username>                   log in (asks password)'
  Write-Info '  logout                             clear local session token'
  Write-Info '  promote <username> <role>          set role: player|mod|admin'
  Write-Info '  accounts                           list saved accounts (device)'
  Write-Info '  store                              save current session to device list'
  Write-Info '  unstore <id>                       remove saved account by id'
  Write-Info '  clear                              clear the screen'
  Write-Info '  exit                               quit the terminal'
  Write-Host '  '
  Write-Info '  promote requires: admin role OR dev-mode on localhost'
  Write-Host '  '
}

while ($true) {
  $line = Read-Host 'ge> '
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
          if ($me) { Write-Ok ("  session: " + $me.user.name + " (" + $me.user.role + ")") }
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
      if ($me) { Write-Ok ($me.user.name + ' (' + $me.user.role + ')') }
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
    'promote' {
      $token = Get-Tok
      if (-not $token) { Write-Err 'not logged in — use: login <username>'; break }
      if ($args.Count -lt 2) { Show-Help; break }
      $name = $args[0]
      $role = $args[1]
      if ($role -notin @('player', 'mod', 'admin')) { Write-Err 'role must be player|mod|admin'; break }
      $r = Invoke-Api -Path '/api/promote' -Method 'POST' -Body @{ username = $name; role = $role } -Tok $token
      if ($r) { Write-Ok ('promoted ' + $r.user + ' -> ' + $r.role) }
      break
    }
    'accounts' {
      $r = Invoke-Api -Path '/api/accounts'
      if ($r) {
        if (@($r.accounts).Count -eq 0) { Write-Info 'no saved accounts' }
        else {
          foreach ($a in $r.accounts) {
            Write-Info ("  [{0}] {1} ({2})" -f $a.id, $a.username, $a.role)
          }
        }
      }
      break
    }
    'store' {
      $token = Get-Tok
      if (-not $token) { Write-Err 'not logged in — use: login <username>'; break }
      $r = Invoke-Api -Path '/api/accounts' -Method 'POST' -Body @{ token = $token }
      if ($r) { Write-Ok 'saved to device accounts' }
      break
    }
    'unstore' {
      if ($args.Count -lt 1) { Show-Help; break }
      $r = Invoke-Api -Path ('/api/accounts/' + $args[0]) -Method 'DELETE'
      if ($r) { Write-Ok 'account removed' }
      break
    }
    default {
      Write-Err ("unknown command: " + $cmd)
      Write-Info 'type "help" for commands'
    }
  }
}