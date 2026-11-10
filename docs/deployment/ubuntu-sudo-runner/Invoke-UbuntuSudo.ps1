param(
    [Parameter(Mandatory=$true)]
    [ValidatePattern('^/home/jt/[A-Za-z0-9_./-]+[.]sh$')]
    [string]$RemoteScript
)
$ErrorActionPreference = 'Stop'
$credentialPath = Join-Path $PSScriptRoot 'ubuntu-sudo.clixml'
if (!(Test-Path -LiteralPath $credentialPath)) { throw 'Run Set-UbuntuCredential.ps1 to save the credential first.' }
$credential = Import-Clixml -LiteralPath $credentialPath
if ($credential.UserName -ne 'jt') { throw 'Unexpected credential username.' }
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = (Get-Command ssh.exe).Source
$startInfo.Arguments = "-T -o BatchMode=yes -o ConnectTimeout=10 jt@192.168.168.114 sudo -S -k -p '' -- bash $RemoteScript"
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardInput = $true
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo
try {
    if (!$process.Start()) { throw 'Could not start SSH.' }
    $process.StandardInput.WriteLine($credential.GetNetworkCredential().Password)
    $process.StandardInput.Close()
    $process.WaitForExit()
    $result = $process.ExitCode
} finally {
    $credential.Password.Dispose()
    $process.Dispose()
}
exit $result
