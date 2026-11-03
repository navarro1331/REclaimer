param([string]$OutputDirectory = '', [switch]$TestsOnly)
$ErrorActionPreference = 'Stop'
$nativeSource = $PSScriptRoot
$repository = [IO.Path]::GetFullPath((Join-Path $nativeSource '../../..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository 'output/guilty-spark-343/windows-x64' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Install Visual Studio Build Tools with the C++ x64 tools and Windows 10/11 SDK.' }
$vc = Get-ChildItem -LiteralPath (Join-Path $installation 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um/Windows.h') } | Sort-Object Name -Descending | Select-Object -First 1
if (-not $vc -or -not $sdk) { throw 'MSVC and a Windows SDK are required.' }
$compiler = Join-Path $vc.FullName 'bin/Hostx64/x64/cl.exe'
$savedInclude = $env:INCLUDE; $savedLib = $env:LIB; $savedPath = $env:PATH
try {
  $env:INCLUDE = @((Join-Path $vc.FullName 'include'), (Join-Path $sdk.FullName 'ucrt'), (Join-Path $sdk.FullName 'shared'), (Join-Path $sdk.FullName 'um')) -join ';'
  $env:LIB = @((Join-Path $vc.FullName 'lib/x64'), (Join-Path $sdkRoot "Lib/$($sdk.Name)/ucrt/x64"), (Join-Path $sdkRoot "Lib/$($sdk.Name)/um/x64")) -join ';'
  $env:PATH = (Split-Path $compiler) + ';' + (Join-Path $sdkRoot "bin/$($sdk.Name)/x64") + ';' + $env:PATH
  Push-Location $OutputDirectory
  try {
    $flags = @('/nologo','/std:c11','/utf-8','/W4','/O2','/MT','/wd4996','/DUNICODE','/D_UNICODE','/DCJSON_HIDE_SYMBOLS','/DCJSON_NESTING_LIMIT=64')
    $common = @((Join-Path $nativeSource 'state.c'),(Join-Path $nativeSource 'memory.c'),(Join-Path $nativeSource 'gabo.c'),(Join-Path $nativeSource 'gabo-browser.c'),(Join-Path $nativeSource 'background.c'),(Join-Path $nativeSource 'activity.c'),(Join-Path $nativeSource 'notifications.c'),(Join-Path $nativeSource 'web-state.c'),(Join-Path $nativeSource 'web-server.c'),(Join-Path $nativeSource 'runtime.c'),(Join-Path $nativeSource 'catalog.c'),(Join-Path $nativeSource 'templates.c'),(Join-Path $nativeSource 'vendor/cJSON.c'))
    & $compiler @flags /c @common
    if ($LASTEXITCODE) { throw 'C core compilation failed.' }
    $libraries = @('user32.lib','gdi32.lib','comctl32.lib','comdlg32.lib','shell32.lib','ole32.lib','uuid.lib','winhttp.lib','ws2_32.lib','bcrypt.lib','crypt32.lib','dwmapi.lib','uxtheme.lib')
    & $compiler @flags (Join-Path $nativeSource 'tests.c') (Join-Path $nativeSource 'catalog_tests.c') (Join-Path $nativeSource 'personality_tests.c') (Join-Path $nativeSource 'history_tests.c') (Join-Path $nativeSource 'memory_tests.c') (Join-Path $nativeSource 'backfill_tests.c') (Join-Path $nativeSource 'gabo_tests.c') (Join-Path $nativeSource 'background_tests.c') (Join-Path $nativeSource 'activity_tests.c') (Join-Path $nativeSource 'notification_tests.c') (Join-Path $nativeSource 'web_tests.c') state.obj memory.obj gabo.obj gabo-browser.obj background.obj activity.obj notifications.obj web-state.obj web-server.obj runtime.obj catalog.obj templates.obj cJSON.obj '/Fe:SparkTests.exe' /link @libraries /STACK:8388608 /DYNAMICBASE /NXCOMPAT /INCREMENTAL:NO
    if ($LASTEXITCODE) { throw 'C test compilation failed.' }
    & (Join-Path $OutputDirectory 'SparkTests.exe')
    if ($LASTEXITCODE) { throw 'Native C tests failed.' }
    if (-not $TestsOnly) {
      & $compiler @flags (Join-Path $nativeSource 'main.c') state.obj memory.obj gabo.obj gabo-browser.obj background.obj activity.obj notifications.obj web-state.obj web-server.obj runtime.obj catalog.obj templates.obj cJSON.obj '/Fe:GuiltySpark343.exe' /link @libraries /SUBSYSTEM:WINDOWS /STACK:8388608 /DYNAMICBASE /NXCOMPAT /INCREMENTAL:NO /MANIFEST:EMBED "/MANIFESTINPUT:$(Join-Path $nativeSource 'app.manifest')"
      if ($LASTEXITCODE) { throw 'Windows application compilation failed.' }
      & $compiler @flags /DSPARK_TEST_HARNESS (Join-Path $nativeSource 'main.c') state.obj memory.obj gabo.obj gabo-browser.obj background.obj activity.obj notifications.obj web-state.obj web-server.obj runtime.obj catalog.obj templates.obj cJSON.obj '/Fe:GuiltySpark343Test.exe' /link @libraries /SUBSYSTEM:WINDOWS /STACK:8388608 /DYNAMICBASE /NXCOMPAT /INCREMENTAL:NO /MANIFEST:EMBED "/MANIFESTINPUT:$(Join-Path $nativeSource 'app.manifest')"
      if ($LASTEXITCODE) { throw 'Native web test harness compilation failed.' }
    }
  } finally { Pop-Location }
} finally { $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath }
Copy-Item -LiteralPath (Join-Path $nativeSource 'web') -Destination $OutputDirectory -Recurse -Force
Write-Output "Built native Windows x64 output: $OutputDirectory"
