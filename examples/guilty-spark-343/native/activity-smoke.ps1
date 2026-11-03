param([string]$Executable='', [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if(-not $Executable){$Executable=Join-Path $repository 'output/guilty-spark-343/windows-x64-v011/GuiltySpark343.exe'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repository ('output/guilty-spark-343/activity-smoke-'+[Guid]::NewGuid().ToString('N'))}
$data=Join-Path $OutputDirectory 'Workspace Ω'
New-Item -ItemType Directory -Path $data -Force | Out-Null
$checks=[Collections.Generic.List[string]]::new();$owned=[Collections.Generic.List[Diagnostics.Process]]::new()
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class SparkActivityUi {
  delegate bool EnumCallback(IntPtr window, IntPtr state);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback, IntPtr state);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int size);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window,int id);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window,uint msg,IntPtr w,IntPtr l);
  [DllImport("user32.dll",EntryPoint="SendMessageW")] public static extern IntPtr Send(IntPtr window,uint msg,IntPtr w,IntPtr l);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] public static extern IntPtr StringSend(IntPtr window,uint msg,IntPtr w,string text);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr Read(IntPtr window,uint msg,IntPtr w,StringBuilder text);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;}
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
  public static string Text(IntPtr window){int n=Send(window,14,IntPtr.Zero,IntPtr.Zero).ToInt32();var text=new StringBuilder(n+1);Read(window,13,(IntPtr)(n+1),text);return text.ToString();}
  public static IntPtr Find(int process,string kind){IntPtr found=IntPtr.Zero;EnumWindows((window,state)=>{uint owner;GetWindowThreadProcessId(window,out owner);if(owner!=process)return true;var type=new StringBuilder(128);GetClassName(window,type,128);if(type.ToString()!=kind)return true;found=window;return false;},IntPtr.Zero);return found;}
}
'@
function Check([bool]$condition,[string]$name){if(-not $condition){throw "Activity check failed: $name"};$checks.Add($name)}
function Wait-For([scriptblock]$condition,[int]$milliseconds=20000){$timer=[Diagnostics.Stopwatch]::StartNew();while($timer.ElapsedMilliseconds -lt $milliseconds){if(& $condition){return};Start-Sleep -Milliseconds 100};throw 'Activity check timed out.'}
function Read-Json($file){try{Get-Content -LiteralPath $file -Raw|ConvertFrom-Json -AsHashtable}catch{return $null}}
function State {Read-Json (Join-Path $data 'workspace.json')}
function Status {Read-Json (Join-Path $data 'background-status.json')}
function Body {[SparkActivityUi]::Text([SparkActivityUi]::GetDlgItem($script:activity,401))}
function Launch {
  $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$Executable;$info.UseShellExecute=$false;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  foreach($argument in @('--background','--config',(Join-Path $OutputDirectory 'config.json'),'--data-dir',$data)){$info.ArgumentList.Add($argument)}
  $process=[Diagnostics.Process]::Start($info);$owned.Add($process);return $process
}
function Open-Activity($command=124){[void][SparkActivityUi]::PostMessage($script:window,0x111,$command,0);Wait-For {$script:activity=[SparkActivityUi]::Find($app.Id,'SparkActivity');$script:activity -ne [IntPtr]::Zero -and [SparkActivityUi]::IsWindowVisible($script:activity)}}
function Choose($model){$combo=[SparkActivityUi]::GetDlgItem($script:activity,400);$index=[SparkActivityUi]::StringSend($combo,0x158,-1,$model);if($index.ToInt32() -lt 0){throw "Missing activity model: $model"};[void][SparkActivityUi]::Send($combo,0x14E,$index,0);[void][SparkActivityUi]::Send($script:activity,0x111,(400 -bor (1 -shl 16)),$combo)}
function Sync {Wait-For {[SparkActivityUi]::IsWindowEnabled([SparkActivityUi]::GetDlgItem($script:activity,402))};$before=(Status).completedChecks;[void][SparkActivityUi]::PostMessage($script:activity,0x111,402,0);Wait-For {(Status).completedChecks -gt $before};Wait-For {[SparkActivityUi]::IsWindowEnabled([SparkActivityUi]::GetDlgItem($script:activity,402))}}
function Write-Model($file){$stream=[IO.File]::Open($file,[IO.FileMode]::CreateNew);$writer=[IO.BinaryWriter]::new($stream);try{$writer.Write([Text.Encoding]::ASCII.GetBytes('GGUF'));$writer.Write([uint32]3);$writer.Write([uint64]1);$writer.Write([uint64]1);$key=[Text.Encoding]::UTF8.GetBytes('general.architecture');$value=[Text.Encoding]::UTF8.GetBytes('llama');$writer.Write([uint64]$key.Length);$writer.Write($key);$writer.Write([uint32]8);$writer.Write([uint64]$value.Length);$writer.Write($value)}finally{$writer.Dispose();$stream.Dispose()}}
$source=@{threads=@();telemetry=@()}
function Add-Turn($model,$id,$text){$thread=@($source.threads|Where-Object id -EQ $id);if(-not $thread.Count){$source.threads+=@{id=$id;model=$model;name=$id;messages=@()};$thread=@($source.threads|Where-Object id -EQ $id)};$thread[0].messages+=@{role='user';content=$text},@{role='assistant';content='Synthetic reply.'}}
function Publish {$source|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $OutputDirectory 'source.next') -Encoding utf8;Move-Item -LiteralPath (Join-Path $OutputDirectory 'source.next') -Destination (Join-Path $OutputDirectory 'source.json') -Force}
try {
  $modelA=Join-Path $OutputDirectory 'alpha.gguf';$modelB=Join-Path $OutputDirectory 'beta.gguf';Write-Model $modelA;Write-Model $modelB
  $config=@{mode='local';models=@(@{name='alpha:latest';path=$modelA},@{name='beta:latest';path=$modelB});modelFolders=@();background=$true;web=@{enabled=$false};gaboSync=@{enabled=$true;notifications=$false;origin='https://synthetic.invalid/';file=(Join-Path $OutputDirectory 'source.json');intervalSeconds=15;modelAliases=@{'alpha:latest'='alpha:latest';'beta:latest'='beta:latest'}}}
  $config|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDirectory 'config.json') -Encoding utf8
  @{version=4;selectedModel='alpha:latest';activeThreadId='draft';activeCharacterId='';persona='';settings=@{temperature=0.7;numPredict=128};modelCharacters=@{};modelThreads=@{'alpha:latest'='draft'};modelMemories=@{};characters=@();threads=@(@{id='draft';name='Draft';model='alpha:latest';draft='Original draft';messages=@()})}|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $data 'workspace.json') -Encoding utf8
  Publish;$app=Launch;Wait-For {$script:window=[SparkActivityUi]::Find($app.Id,'GuiltySpark343');(Status).processId -eq $app.Id -and (Status).completedChecks -ge 1}
  Open-Activity;Check ([SparkActivityUi]::IsWindowEnabled($window)) 'Activity is modeless and leaves importing enabled'
  Check ((Body).Contains('No new content imported yet') -and (Body).Contains('Latest check succeeded.')) 'Initial empty check is distinguished from an import'
  Add-Turn 'alpha:latest' 'a' 'I use a telescope.';Add-Turn 'beta:latest' 'b' 'I prefer detailed replies.';Publish
  # Let a scheduled check run with the activity window open.
  Wait-For {(State).gaboActivity.Count -eq 1} 25000;Wait-For {(Body).Contains('I use a telescope.')}
  Check ((Body).Contains('New memories learned: 1') -and -not (Body).Contains('I prefer detailed replies.')) 'Model A view shows its newly learned fact only'
  Choose 'beta:latest';Check ((Body).Contains('I prefer detailed replies.') -and -not (Body).Contains('I use a telescope.')) 'Model B view shows its newly learned fact only'
  Choose 'All models';Check ((Body).Contains('I use a telescope.') -and (Body).Contains('I prefer detailed replies.')) 'All models groups both owners results'
  $history=(State).gaboActivity|ConvertTo-Json -Depth 20 -Compress;$importTime=(Status).lastImportUtc
  Check (-not $history.Contains('telescope') -and -not $history.Contains('detailed replies')) 'Persisted receipts contain memory fingerprints rather than extra fact copies'
  Sync;Check (((State).gaboActivity|ConvertTo-Json -Depth 20 -Compress) -ceq $history -and (Status).lastImportUtc -eq $importTime) 'A repeat check updates check status without duplicating learned memories or import time'
  $lastSuccess=(Status).lastSuccessfulCheckUtc
  New-Item -ItemType Directory -Path (Join-Path $data 'workspace.json.tmp')|Out-Null
  Add-Turn 'alpha:latest' 'a' 'I like astronomy.';Publish;Sync
  Check (-not (Status).lastCheckSucceeded -and (Body).Contains('Latest check failed.')) 'Failed atomic save is visible in the activity window'
  Check (((State).gaboActivity|ConvertTo-Json -Depth 20 -Compress) -ceq $history -and -not (Body).Contains('I like astronomy.')) 'An unsaved import cannot publish a learned memory receipt'
  Check ((Status).lastSuccessfulCheckUtc -eq $lastSuccess -and (Status).lastImportUtc -eq $importTime) 'Failure preserves the last successful check and actual import times'
  # The fixture path is an explicitly created empty directory, never recursive.
  Remove-Item -LiteralPath (Join-Path $data 'workspace.json.tmp');Sync
  Check ((State).gaboActivity.Count -eq 2 -and (Body).Contains('I like astronomy.')) 'Retry commits one receipt and refreshes the still-open activity window'
  [void][SparkActivityUi]::PostMessage($script:activity,0x10,0,0);Wait-For {[SparkActivityUi]::Find($app.Id,'SparkActivity') -eq [IntPtr]::Zero}
  Check (-not $app.HasExited) 'Closing activity leaves the background importer running'
  Open-Activity 306;Check ([SparkActivityUi]::IsWindowVisible($script:activity)) 'Tray Import activity opens the same review window'
  Choose 'All models'
  Add-Type -AssemblyName System.Drawing
  $rect=[SparkActivityUi+Rect]::new();[void][SparkActivityUi]::GetWindowRect($script:activity,[ref]$rect)
  $bitmap=[Drawing.Bitmap]::new($rect.Right-$rect.Left,$rect.Bottom-$rect.Top);$graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
  try{[void][SparkActivityUi]::PrintWindow($script:activity,$dc,0)}finally{$graphics.ReleaseHdc($dc);$graphics.Dispose()}
  $bitmap.Save((Join-Path $OutputDirectory 'activity.png'));$bitmap.Dispose()
  [void][SparkActivityUi]::PostMessage($window,0x111,305,0);Check ($app.WaitForExit(10000) -and $app.ExitCode -eq 0) 'Exit saves and closes activity cleanly'
  $history=(State).gaboActivity|ConvertTo-Json -Depth 20 -Compress;$lastSuccess=(Status).lastSuccessfulCheckUtc
  # Simulate normal saved memory deletion before a fresh launch; the receipt
  # must not preserve readable deleted text or relearn a consumed source fact.
  $saved=State;$saved.modelMemories['alpha:latest'].notes=@('I like astronomy.');$saved|ConvertTo-Json -Depth 30|Set-Content -LiteralPath (Join-Path $data 'workspace.json') -Encoding utf8
  '{"broken":'|Set-Content -LiteralPath (Join-Path $OutputDirectory 'source.json') -Encoding utf8
  $app=Launch;Wait-For {$script:window=[SparkActivityUi]::Find($app.Id,'GuiltySpark343');(Status).processId -eq $app.Id -and (Status).completedChecks -ge 1};Open-Activity
  Check (-not (Status).lastCheckSucceeded -and (Status).lastSuccessfulCheckUtc -eq $lastSuccess) 'Restart retains the prior successful check even when the source is unavailable'
  Check ((Body).Contains('I like astronomy.') -and (Body).Contains('later removed or edited') -and -not (Body).Contains('telescope')) 'Restart shows saved activity and respects deleted memories'
  Check (((State).gaboActivity|ConvertTo-Json -Depth 20 -Compress) -ceq $history) 'Import receipt survives native workspace reload'
  Publish;Sync;Check ((State).modelMemories['alpha:latest'].notes.Count -eq 1 -and -not (Body).Contains('telescope')) 'Polling does not relearn or redisplay the deleted source fact'
  Check (@((State).threads|Where-Object id -EQ 'draft')[0].draft -eq 'Original draft') 'Activity use preserves the unsent draft'
  @{passed=$true;checks=$checks;output=$OutputDirectory}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8
  @{passed=$true;checks=$checks.Count;output=$OutputDirectory}|ConvertTo-Json
}catch{@{passed=$false;error=$_.Exception.Message;checks=$checks}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.json') -Encoding utf8;throw}finally{
  foreach($process in $owned){if(-not $process.HasExited){$target=[SparkActivityUi]::Find($process.Id,'GuiltySpark343');if($target -ne [IntPtr]::Zero){[void][SparkActivityUi]::PostMessage($target,0x111,305,0)};if(-not $process.WaitForExit(10000)){$process.Kill()}}}
}
