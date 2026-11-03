param(
  [Parameter(Mandatory=$true)][string]$Config,
  [string]$Executable = '',
  [string]$OutputDirectory = ''
)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (-not $Executable) { $Executable=Join-Path $repository 'output/guilty-spark-343/windows-x64/GuiltySpark343.exe' }
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $repository ('output/guilty-spark-343/smoke-'+[Guid]::NewGuid().ToString('N')) }
$Config=[IO.Path]::GetFullPath($Config)
$Executable=[IO.Path]::GetFullPath($Executable)
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$records=@()
foreach ($scenario in @('generation','stop','close')) {
  $flag=if ($scenario -eq 'generation') {'--smoke'} else {"--smoke-$scenario"}
  $receipt=Join-Path $OutputDirectory "$scenario.json"
  $data=Join-Path $OutputDirectory "$scenario-data"
  $info=New-Object System.Diagnostics.ProcessStartInfo
  $info.FileName=$Executable
  $info.UseShellExecute=$false
  $info.CreateNoWindow=$true
  # ArgumentList avoids shell parsing and handles spaces and Unicode correctly.
  # This script requires PowerShell 7; the desktop executable does not.
  foreach ($value in @('--config',$Config,'--data-dir',$data,$flag,'--receipt',$receipt)) { $info.ArgumentList.Add($value) }
  $process=[System.Diagnostics.Process]::Start($info)
  try {
    if (-not $process.WaitForExit(180000)) { $process.Kill(); throw "Native smoke timed out: $scenario" }
    $result=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    if ($process.ExitCode -ne 0 -or -not $result.passed) { throw "Native smoke failed: $scenario ($($result.error))" }
    $records += [ordered]@{scenario=$scenario;passed=$result.passed;exitCode=$process.ExitCode;receipt=$receipt}
  } finally { $process.Dispose() }
}
$records | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
$records | ForEach-Object { [pscustomobject]$_ }
