param([string]$Executable='', [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if(-not $Executable){$Executable=Join-Path $repository 'output/guilty-spark-343/windows-x64-v010/GuiltySpark343.exe'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repository ('output/guilty-spark-343/background-smoke-'+[Guid]::NewGuid().ToString('N'))}
$data=Join-Path $OutputDirectory 'Workspace Ω'
New-Item -ItemType Directory -Path $data -Force | Out-Null
$checks=[Collections.Generic.List[string]]::new()
$owned=[Collections.Generic.List[Diagnostics.Process]]::new()
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class SparkBackgroundUi {
  delegate bool EnumCallback(IntPtr window, IntPtr state);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback, IntPtr state);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr WriteText(IntPtr window, uint message, IntPtr w, string text);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string text);
  public static IntPtr Find(int process) { IntPtr found=IntPtr.Zero;EnumWindows((window,state)=>{uint owner;GetWindowThreadProcessId(window,out owner);if(owner!=process)return true;var type=new StringBuilder(128);GetClassName(window,type,128);if(type.ToString()!="GuiltySpark343")return true;found=window;return false;},IntPtr.Zero);return found; }
}
'@
function Check([bool]$condition,[string]$name){if(-not $condition){throw "Background check failed: $name"};$checks.Add($name)}
function Wait-For([scriptblock]$condition,[int]$milliseconds=25000){$timer=[Diagnostics.Stopwatch]::StartNew();while($timer.ElapsedMilliseconds -lt $milliseconds){if(& $condition){return};Start-Sleep -Milliseconds 100};throw 'Background check timed out.'}
function Read-Json($file){try{Get-Content -LiteralPath $file -Raw | ConvertFrom-Json -AsHashtable}catch{return $null}}
function Read-State { Read-Json (Join-Path $data 'workspace.json') }
function Read-Status { Read-Json (Join-Path $data 'background-status.json') }
function Launch([bool]$hidden=$true,[string]$directory=$data){
  $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$Executable;$info.UseShellExecute=$false;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  foreach($argument in @('--config',(Join-Path $OutputDirectory 'config.json'),'--data-dir',$directory)){$info.ArgumentList.Add($argument)}
  if($hidden){$info.ArgumentList.Add('--background')}
  $process=[Diagnostics.Process]::Start($info);$owned.Add($process);return $process
}
function Write-Model($file){$stream=[IO.File]::Open($file,[IO.FileMode]::CreateNew);$writer=[IO.BinaryWriter]::new($stream);try{$writer.Write([Text.Encoding]::ASCII.GetBytes('GGUF'));$writer.Write([uint32]3);$writer.Write([uint64]1);$writer.Write([uint64]1);$key=[Text.Encoding]::UTF8.GetBytes('general.architecture');$value=[Text.Encoding]::UTF8.GetBytes('llama');$writer.Write([uint64]$key.Length);$writer.Write($key);$writer.Write([uint32]8);$writer.Write([uint64]$value.Length);$writer.Write($value)}finally{$writer.Dispose();$stream.Dispose()}}
$source=@{threads=@();telemetry=@()}
function Add-Turn($model,$id,$text){$thread=@($source.threads|Where-Object id -EQ $id);if(-not $thread.Count){$source.threads+=@{id=$id;model=$model;name=$id;messages=@()};$thread=@($source.threads|Where-Object id -EQ $id)};$thread[0].messages+=@{role='user';content=$text},@{role='assistant';content='Synthetic reply.'}}
function Publish-Source { $source|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $OutputDirectory 'source.next') -Encoding utf8;Move-Item -LiteralPath (Join-Path $OutputDirectory 'source.next') -Destination (Join-Path $OutputDirectory 'source.json') -Force }
try {
  $modelA=Join-Path $OutputDirectory 'alpha.gguf';$modelB=Join-Path $OutputDirectory 'beta.gguf';Write-Model $modelA;Write-Model $modelB
  $config=@{mode='local';models=@(@{name='alpha:latest';path=$modelA},@{name='beta:latest';path=$modelB});modelFolders=@();background=$true;web=@{enabled=$false};gaboSync=@{enabled=$true;notifications=$false;origin='https://synthetic.invalid/';file=(Join-Path $OutputDirectory 'source.json');intervalSeconds=15;modelAliases=@{'alpha:latest'='alpha:latest';'beta:latest'='beta:latest'}}}
  $config|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDirectory 'config.json') -Encoding utf8
  @{version=4;selectedModel='alpha:latest';activeThreadId='draft';activeCharacterId='';persona='';settings=@{temperature=0.7;numPredict=128};modelCharacters=@{};modelThreads=@{'alpha:latest'='draft'};modelMemories=@{};characters=@();threads=@(@{id='draft';name='Draft';model='alpha:latest';draft='Original draft';messages=@()})}|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $data 'workspace.json') -Encoding utf8
  Publish-Source;$app=Launch
  Wait-For {$script:window=[SparkBackgroundUi]::Find($app.Id);$script:window -ne [IntPtr]::Zero -and (Read-Status).completedChecks -ge 1}
  Check (-not [SparkBackgroundUi]::IsWindowVisible($window)) 'Background launch keeps the chat window hidden'
  Check ((Read-Status).trayAvailable -and (Read-Status).lastCheckSucceeded) 'Hidden app has a tray icon and a successful source check'
  Add-Turn 'alpha:latest' 'alpha-source' 'I use a telescope.';Add-Turn 'beta:latest' 'beta-source' 'I prefer detailed replies.';Publish-Source
  Wait-For {(Read-State).gaboImports.Count -eq 2}
  $state=Read-State
  Check (@($state.threads|Where-Object {$_.model -eq 'alpha:latest' -and $_.messages.Count -eq 2}).Count -eq 1 -and @($state.threads|Where-Object {$_.model -eq 'beta:latest' -and $_.messages.Count -eq 2}).Count -eq 1) 'Hidden polling imports separate model histories'
  Check ($state.modelMemories['alpha:latest'].notes -contains 'I use a telescope.' -and $state.modelMemories['alpha:latest'].notes -notcontains 'I prefer detailed replies.' -and $state.modelMemories['beta:latest'].notes -contains 'I prefer detailed replies.' -and $state.modelMemories['beta:latest'].notes -notcontains 'I use a telescope.') 'Hidden learning keeps both models memories separate'
  $duplicate=Launch;Check ($duplicate.WaitForExit(5000) -and $duplicate.ExitCode -eq 0) 'Repeated background launch exits successfully without another workspace'
  Check (-not [SparkBackgroundUi]::IsWindowVisible($window) -and -not $app.HasExited) 'Duplicate startup leaves the original app hidden and running'
  $open=Launch $false ($data.ToUpperInvariant()+'\');Check ($open.WaitForExit(5000) -and $open.ExitCode -eq 0) 'Opening the same workspace with normalized casing reaches the existing instance'
  Wait-For {$script:reopenStatus=Read-Status;[SparkBackgroundUi]::IsWindowVisible($window) -and $script:reopenStatus.processId -eq $app.Id}
  Check ($script:reopenStatus.processId -eq $app.Id) 'Reopening shows the original process'
  [void][SparkBackgroundUi]::WriteText([SparkBackgroundUi]::GetDlgItem($window,114),0xC,[IntPtr]::Zero,'Preserved unsent background draft')
  [void][SparkBackgroundUi]::PostMessage($window,0x10,0,0);Wait-For {-not [SparkBackgroundUi]::IsWindowVisible($window)}
  Check (-not $app.HasExited -and @((Read-State).threads|Where-Object id -EQ 'draft')[0].draft -ceq 'Preserved unsent background draft') 'Closing the chat saves its draft and keeps the importer running'
  Add-Turn 'alpha:latest' 'alpha-source' 'I like astronomy.';Add-Turn 'beta:latest' 'beta-source' 'I like jazz.';Publish-Source
  Wait-For {@((Read-State).threads|Where-Object {$_.id -ne 'draft' -and $_.messages.Count -eq 4}).Count -eq 2}
  $state=Read-State
  Check ($state.modelMemories['alpha:latest'].notes -contains 'I like astronomy.' -and $state.modelMemories['alpha:latest'].notes -notcontains 'I like jazz.' -and $state.modelMemories['beta:latest'].notes -contains 'I like jazz.' -and $state.modelMemories['beta:latest'].notes -notcontains 'I like astronomy.') 'Both models learn new facts after the window closes without crossing memories'
  Check (-not [SparkBackgroundUi]::IsWindowVisible($window)) 'Learning does not reopen the chat window'
  $taskbar=[SparkBackgroundUi]::RegisterWindowMessage('TaskbarCreated');[void][SparkBackgroundUi]::SendMessage($window,$taskbar,0,0)
  Check ((Read-Status).trayAvailable) 'Tray registration recovers after a taskbar creation notification'
  Check ([SparkBackgroundUi]::SendMessage($window,0x11,0,0).ToInt64() -eq 1) 'The app permits Windows session shutdown'
  [void][SparkBackgroundUi]::SendMessage($window,0x16,0,0);Check (-not $app.HasExited) 'A cancelled Windows shutdown does not stop importing'
  [void][SparkBackgroundUi]::PostMessage($window,0x8005,0,((343 -shl 16) -bor 0x401));Wait-For {[SparkBackgroundUi]::IsWindowVisible($window)}
  Check ([SparkBackgroundUi]::IsWindowVisible($window)) 'Tray keyboard activation opens the chat window'
  [void][SparkBackgroundUi]::PostMessage($window,0x111,303,0);Wait-For { -not (Read-Status).backgroundEnabled }
  [void][SparkBackgroundUi]::PostMessage($window,0x10,0,0);Check ($app.WaitForExit(10000) -and $app.ExitCode -eq 0) 'Disabling background mode restores normal close behavior'
  $beforeRestart=(Read-State)|ConvertTo-Json -Depth 30 -Compress
  $app=Launch;Wait-For {$script:window=[SparkBackgroundUi]::Find($app.Id);$report=Read-Status;$report.processId -eq $app.Id -and $report.completedChecks -ge 1}
  Check (((Read-State)|ConvertTo-Json -Depth 30 -Compress) -ceq $beforeRestart) 'Background restart does not duplicate chats or memories and preserves the draft'
  [void][SparkBackgroundUi]::PostMessage($window,0x111,305,0);Check ($app.WaitForExit(10000) -and $app.ExitCode -eq 0) 'Tray Exit stops the background process cleanly'
  $app=Launch;Wait-For {$script:window=[SparkBackgroundUi]::Find($app.Id);(Read-Status).processId -eq $app.Id}
  [void][SparkBackgroundUi]::SendMessage($window,0x16,1,0);Check ($app.WaitForExit(10000) -and $app.ExitCode -eq 0) 'Confirmed session end exits instead of hiding in the tray'
  Check (-not (Read-Status).running) 'Final background status reports that the process stopped'
  $receipt=@{passed=$true;checks=$checks;output=$OutputDirectory};$receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8
  @{passed=$true;checks=$checks.Count;output=$OutputDirectory}|ConvertTo-Json
} catch { @{passed=$false;error=$_.Exception.Message;checks=$checks}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.json') -Encoding utf8;throw } finally {
  foreach($process in $owned){if(-not $process.HasExited){$target=[SparkBackgroundUi]::Find($process.Id);if($target -ne [IntPtr]::Zero){[void][SparkBackgroundUi]::PostMessage($target,0x111,305,0)};if(-not $process.WaitForExit(10000)){$process.Kill()}}}
}
