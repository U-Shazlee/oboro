# Let Oboro's relay reach Sunshine and Oboro Host on this PC, and check the
# tunnel. See docs/remote-play.md. Run in PowerShell as Administrator, after
# importing oboro-relay.conf in WireGuard and activating it:
#
#     powershell -ExecutionPolicy Bypass -File relay\setup-pc.ps1
#
# Running it again replaces its own two firewall rules and nothing else.

$ErrorActionPreference = 'Stop'
$Relay = '10.66.66.1'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'Run this from PowerShell opened with "Run as administrator".'
    exit 1
}

# Windows treats the tunnel as a public network, where Sunshine's own
# firewall rule may not apply. These two allow the relay, and only the relay.
Get-NetFirewallRule -DisplayName 'Oboro relay*' -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName 'Oboro relay (TCP)' -Direction Inbound -Action Allow -Profile Any `
    -Protocol TCP -LocalPort 47984, 47989, 48010, 48100 -RemoteAddress $Relay | Out-Null
New-NetFirewallRule -DisplayName 'Oboro relay (UDP)' -Direction Inbound -Action Allow -Profile Any `
    -Protocol UDP -LocalPort '47998-48000' -RemoteAddress $Relay | Out-Null
Write-Host 'Firewall: the relay may reach Sunshine and Oboro Host.'

if (-not (Get-NetIPAddress -IPAddress '10.66.66.2' -ErrorAction SilentlyContinue)) {
    Write-Host 'The tunnel is not up: open WireGuard, import oboro-relay.conf and press Activate, then run this again.'
    exit 1
}
if (Test-Connection -ComputerName $Relay -Count 3 -Quiet) {
    $ms = (Test-Connection -ComputerName $Relay -Count 10 | Measure-Object -Property ResponseTime -Average).Average
    Write-Host ("Tunnel: connected, {0:N0} ms to the relay." -f $ms)
} else {
    Write-Host 'Tunnel: the relay does not answer. Check the Endpoint line in oboro-relay.conf and that the server is running.'
    exit 1
}

foreach ($port in 47989, 48100) {
    $name = if ($port -eq 48100) { 'Oboro Host' } else { 'Sunshine' }
    if (Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue) {
        Write-Host "${name}: running."
    } else {
        Write-Host "${name}: NOT running on this PC (port $port). Start it before playing."
    }
}
