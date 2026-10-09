# OpenBV @VERSION@ on gasm-run @GASM_VERSION@ for Windows (started by OpenBV.cmd). Runs openbv.wasm with
# the game's content from data\ next to it. Options:
#   --help       this
#   --dry-run    print the gasm-run command instead of running it (also OPENBV_DRY_RUN=1)
# Anything else goes to gasm-run (e.g. --window 1920x1080, --filter fsr, --param master=host:port).
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dry = $env:OPENBV_DRY_RUN -eq '1'
$pass = @()
foreach ($a in $args) {
  switch -Exact ($a) {
    { $_ -in '--help', '-h' } { Get-Content $MyInvocation.MyCommand.Path -TotalCount 5 | ForEach-Object { $_ -replace '^# ?', '' }; exit 0 }
    '--dry-run' { $dry = $true }
    default { $pass += $a }
  }
}
$run = Join-Path $here 'gasm-run.exe'
$cmd = @((Join-Path $here 'openbv.wasm'), '--asset-dir', (Join-Path $here 'data'), '--storage-id', 'openbv',
  '--allow-net=127.0.0.1,localhost', '--window', '1600x900', '--icon', (Join-Path $here 'openbv.png')) + $pass
if ($dry) {
  Write-Output ("`"$run`" " + (($cmd | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '))
  exit 0
}
& $run @cmd
exit $LASTEXITCODE
