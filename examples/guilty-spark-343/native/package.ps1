param(
  [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
  [Parameter(Mandatory=$true)][string]$RuntimeLicenseFile,
  [string]$BuildDirectory = '',
  [string]$Destination = ''
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repository 'output/guilty-spark-343/windows-x64' }
if (-not $Destination) { $Destination = Join-Path $repository 'output/guilty-spark-343/Guilty-Spark-343-Windows-x64' }
$Destination = [IO.Path]::GetFullPath($Destination)
$archive = "$Destination.zip"
if ((Test-Path -LiteralPath $Destination) -or (Test-Path -LiteralPath $archive)) { throw 'Choose a new destination; existing packages are preserved.' }
$runtimeFiles = @('llama-server.exe','llama-server-impl.dll','llama-common.dll','llama.dll','mtmd.dll','ggml.dll','ggml-base.dll','libomp.dll','LICENSE-LLVM-OpenMP')
$runtimeFiles += Get-ChildItem -LiteralPath $RuntimeDirectory -Filter 'ggml-cpu-*.dll' -File | Select-Object -ExpandProperty Name
$required = @((Join-Path $BuildDirectory 'GuiltySpark343.exe'), (Join-Path $BuildDirectory 'web/chat.html'), (Join-Path $BuildDirectory 'web/LICENSE'), $RuntimeLicenseFile)
$required += $runtimeFiles | ForEach-Object { Join-Path $RuntimeDirectory $_ }
foreach ($file in $required) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required distribution file is missing: $file" } }
if (-not ($runtimeFiles -contains 'ggml-cpu-x64.dll')) { throw 'This package expects a Windows x64 CPU llama.cpp runtime.' }
New-Item -ItemType Directory -Path (Join-Path $Destination 'runtime'),(Join-Path $Destination 'licenses') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'GuiltySpark343.exe') -Destination $Destination
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'web') -Destination $Destination -Recurse
foreach ($name in $runtimeFiles) { Copy-Item -LiteralPath (Join-Path $RuntimeDirectory $name) -Destination (Join-Path $Destination 'runtime') }
Copy-Item -LiteralPath $RuntimeLicenseFile -Destination (Join-Path $Destination 'licenses/LICENSE.llama.cpp.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'vendor/LICENSE.cJSON.txt') -Destination (Join-Path $Destination 'licenses')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../THIRD_PARTY_NOTICES.md') -Destination (Join-Path $Destination 'licenses/GobboNet-and-platform-notices.md')
foreach ($name in @('README.md','MIGRATION.md','guilty-spark.example.json')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $Destination }
$manifest = Get-ChildItem -LiteralPath $Destination -File -Recurse | Sort-Object FullName | ForEach-Object {
  [ordered]@{path=$_.FullName.Substring($Destination.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $Destination 'SHA256SUMS.json') -Encoding utf8
Compress-Archive -LiteralPath $Destination -DestinationPath $archive -CompressionLevel Optimal
[pscustomobject]@{Directory=$Destination;Archive=$archive;Bytes=(Get-Item -LiteralPath $archive).Length;SHA256=(Get-FileHash -LiteralPath $archive).Hash}
