# Local Ubuntu credentials

For authorized sudo work on `jt@192.168.168.114`, use `Invoke-UbuntuSudo.ps1 -RemoteScript /home/jt/task-script.sh` from Windows PowerShell. Upload and inspect the task script first. The runner decrypts `ubuntu-sudo.clixml` in memory and sends the password through SSH standard input; the password is never a command argument.

Run `Set-UbuntuCredential.ps1` interactively to create or replace the password. Storage uses Windows DPAPI through Export-Clixml and is restricted to this Windows user and SYSTEM. It works only under the same Windows account on this computer. Check successful remote exit status before claiming authentication succeeded.

Keep the encrypted file and any decrypted value out of Git, logs and chat. This is only the Ubuntu sudo credential, not an AI-market, database, GitHub or device credential. Continue using those systems' existing private configuration. Credential availability does not authorize deployments, destructive operations or unrelated actions.
