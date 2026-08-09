[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$matchPattern = '(?i)VEEPEAK|OBDCheck|ELM327|OBDII'
$serialPorts = @(
    [System.IO.Ports.SerialPort]::GetPortNames() |
        Sort-Object -Unique
)

$pnpQuerySucceeded = $true
$pnpQueryError = $null
$pnpCandidates = @()
try {
    $pnpCandidates = @(
        Get-PnpDevice -PresentOnly -ErrorAction Stop |
            Where-Object { $_.FriendlyName -match $matchPattern } |
            ForEach-Object {
                [pscustomobject]@{
                    FriendlyName = $_.FriendlyName
                    Class        = $_.Class
                    Status       = $_.Status
                }
            }
    )
}
catch {
    $pnpQuerySucceeded = $false
    $pnpQueryError = $_.Exception.Message
}

$registryQuerySucceeded = $true
$registryQueryErrors = [System.Collections.Generic.List[string]]::new()
$registryNames = [System.Collections.Generic.List[string]]::new()
$registryRoots = @(
    'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\BTHENUM',
    'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\BTHLEDEVICE'
)

foreach ($root in $registryRoots) {
    try {
        if (-not (Test-Path -LiteralPath $root)) {
            continue
        }
        foreach ($deviceType in Get-ChildItem -LiteralPath $root) {
            foreach ($instance in Get-ChildItem -LiteralPath $deviceType.PSPath) {
                $properties = Get-ItemProperty -LiteralPath $instance.PSPath
                $friendlyProperty =
                    $properties.PSObject.Properties['FriendlyName']
                $descriptionProperty =
                    $properties.PSObject.Properties['DeviceDesc']
                $name = if ($null -ne $friendlyProperty) {
                    $friendlyProperty.Value
                }
                elseif ($null -ne $descriptionProperty) {
                    $descriptionProperty.Value
                }
                else {
                    $null
                }
                if ($name -match $matchPattern) {
                    # Do not output registry paths or instance IDs: they may
                    # contain a Bluetooth address or device serial.
                    $registryNames.Add(($name -replace '^.*;', ''))
                }
            }
        }
    }
    catch {
        $registryQuerySucceeded = $false
        $registryQueryErrors.Add($_.Exception.Message)
    }
}

$registryCandidates = @($registryNames | Sort-Object -Unique)
$registryErrorText = if ($registryQueryErrors.Count -eq 0) {
    $null
}
else {
    ($registryQueryErrors | Sort-Object -Unique) -join ' | '
}
$candidateCount = $pnpCandidates.Count + $registryCandidates.Count

[pscustomobject]@{
    CheckedAtUtc            = [DateTime]::UtcNow.ToString('o')
    SerialPorts             = $serialPorts
    PnpQuerySucceeded       = $pnpQuerySucceeded
    PnpCandidates           = $pnpCandidates
    PnpQueryError           = $pnpQueryError
    RegistryQuerySucceeded  = $registryQuerySucceeded
    RegistryCandidates      = $registryCandidates
    RegistryQueryError      = $registryErrorText
    StateChangesMade        = $false
    Interpretation          = if ($candidateCount -gt 0) {
        'Windows has a matching saved or present adapter name. This does not prove the adapter is powered, connected, or vehicle-compatible.'
    }
    else {
        'No matching public-safe adapter name was found. A blocked PnP query, unpaired adapter, or unpowered vehicle port makes this inconclusive.'
    }
}
