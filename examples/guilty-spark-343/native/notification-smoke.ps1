param([string]$Executable='', [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if(-not $Executable){$Executable=Join-Path $repository 'output/guilty-spark-343/windows-x64-v012/GuiltySpark343.exe'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repository ('output/guilty-spark-343/notification-smoke-'+[Guid]::NewGuid().ToString('N'))}
$data=Join-Path $OutputDirectory 'Workspace Ω'
New-Item -ItemType Directory -Path $data -Force | Out-Null
$checks=[Collections.Generic.List[string]]::new();$owned=[Collections.Generic.List[Diagnostics.Process]]::new()
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class SparkNotificationUi {
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
function Check([bool]$condition,[string]$name){if(-not $condition){throw "Notification check failed: $name"};$checks.Add($name)}
function Wait-For([scriptblock]$condition,[int]$milliseconds=20000){$timer=[Diagnostics.Stopwatch]::StartNew();while($timer.ElapsedMilliseconds -lt $milliseconds){if(& $condition){return};Start-Sleep -Milliseconds 100};throw 'Notification check timed out.'}
function Read-Json($file){try{Get-Content -LiteralPath $file -Raw|ConvertFrom-Json -AsHashtable}catch{return $null}}
function State {Read-Json (Join-Path $data 'workspace.json')}
function Status {Read-Json (Join-Path $data 'background-status.json')}
function Body {[SparkNotificationUi]::Text([SparkNotificationUi]::GetDlgItem($script:activity,401))}
function Launch {
  $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$Executable;$info.UseShellExecute=$false;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  foreach($argument in @('--background','--config',(Join-Path $data 'settings.json'),'--data-dir',$data)){$info.ArgumentList.Add($argument)}
  $process=[Diagnostics.Process]::Start($info);$owned.Add($process);return $process
}
function Open-Activity($command=124){[void][SparkNotificationUi]::PostMessage($script:window,0x111,$command,0);Wait-For {$script:activity=[SparkNotificationUi]::Find($app.Id,'SparkActivity');$script:activity -ne [IntPtr]::Zero -and [SparkNotificationUi]::IsWindowVisible($script:activity)}}
function Choose($model){$combo=[SparkNotificationUi]::GetDlgItem($script:activity,400);$index=[SparkNotificationUi]::StringSend($combo,0x158,-1,$model);if($index.ToInt32() -lt 0){throw "Missing activity model: $model"};[void][SparkNotificationUi]::Send($combo,0x14E,$index,0);[void][SparkNotificationUi]::Send($script:activity,0x111,(400 -bor (1 -shl 16)),$combo)}
function Sync {Wait-For {[SparkNotificationUi]::IsWindowEnabled([SparkNotificationUi]::GetDlgItem($script:activity,402))};$before=(Status).completedChecks;[void][SparkNotificationUi]::PostMessage($script:activity,0x111,402,0);Wait-For {(Status).completedChecks -gt $before};Wait-For {[SparkNotificationUi]::IsWindowEnabled([SparkNotificationUi]::GetDlgItem($script:activity,402))}}
function Write-Model($file){$stream=[IO.File]::Open($file,[IO.FileMode]::CreateNew);$writer=[IO.BinaryWriter]::new($stream);try{$writer.Write([Text.Encoding]::ASCII.GetBytes('GGUF'));$writer.Write([uint32]3);$writer.Write([uint64]1);$writer.Write([uint64]1);$key=[Text.Encoding]::UTF8.GetBytes('general.architecture');$value=[Text.Encoding]::UTF8.GetBytes('llama');$writer.Write([uint64]$key.Length);$writer.Write($key);$writer.Write([uint32]8);$writer.Write([uint64]$value.Length);$writer.Write($value)}finally{$writer.Dispose();$stream.Dispose()}}
$source=@{threads=@();telemetry=@()}
function Add-Turn($model,$id,$text){$thread=@($source.threads|Where-Object id -EQ $id);if(-not $thread.Count){$source.threads+=@{id=$id;model=$model;name=$id;messages=@()};$thread=@($source.threads|Where-Object id -EQ $id)};$thread[0].messages+=@{role='user';content=$text},@{role='assistant';content='Synthetic reply.'}}
function Publish {$source|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $OutputDirectory 'source.next') -Encoding utf8;Move-Item -LiteralPath (Join-Path $OutputDirectory 'source.next') -Destination (Join-Path $OutputDirectory 'source.json') -Force}
function Check-Source {$before=(Status).completedChecks;[void][SparkNotificationUi]::PostMessage($script:window,0x111,301,0);Wait-For {(Status).completedChecks -gt $before}}
function Wait-NoticeEnd {Wait-For {(Status).lastNotification.status -in @('dismissed','suppressed','clicked','display unconfirmed')} 45000}
try {
  $alpha='343 test Alpha:latest';$beta='343 test Beta:latest'
  $modelA=Join-Path $OutputDirectory 'alpha.gguf';$modelB=Join-Path $OutputDirectory 'beta.gguf';Write-Model $modelA;Write-Model $modelB
  $config=@{mode='local';models=@(@{name=$alpha;path=$modelA},@{name=$beta;path=$modelB});modelFolders=@();background=$true;web=@{enabled=$false};gaboSync=@{enabled=$true;origin='https://synthetic.invalid/';file=(Join-Path $OutputDirectory 'source.json');intervalSeconds=15;modelAliases=@{$alpha=$alpha;$beta=$beta}}}
  $config|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $data 'settings.json') -Encoding utf8
  @{version=4;selectedModel=$alpha;activeThreadId='draft';activeCharacterId='';persona='';settings=@{temperature=0.7;numPredict=128};modelCharacters=@{};modelThreads=@{$alpha='draft'};modelMemories=@{};characters=@();threads=@(@{id='draft';name='Draft';model=$alpha;draft='Original draft';messages=@()})}|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $data 'workspace.json') -Encoding utf8
  Publish;$app=Launch;Wait-For {$script:window=[SparkNotificationUi]::Find($app.Id,'GuiltySpark343');(Status).processId -eq $app.Id -and (Status).completedChecks -ge 1}
  Check ((Status).importNotificationsEnabled -and (Status).notificationsSubmitted -eq 0) 'Notifications default to enabled without announcing old or empty imports'
  Open-Activity;Check ([SparkNotificationUi]::Send([SparkNotificationUi]::GetDlgItem($script:activity,403),0xF0,0,0).ToInt32() -eq 1) 'Activity checkbox reflects enabled notifications'
  Add-Turn $alpha 'a' 'I use a telescope.';Add-Turn $beta 'b' 'I prefer detailed replies.';Publish
  [void][SparkNotificationUi]::PostMessage($script:window,0x10,0,0);Wait-For {-not [SparkNotificationUi]::IsWindowVisible($script:window)}
  Wait-For {(Status).notificationsSubmitted -ge 1} 25000;$first=Status
  Check ($first.lastNotification.model -ceq $alpha -and $first.lastNotification.chats -eq 1 -and $first.lastNotification.memories -eq 1) 'First committed import submits model A with its own chat and memory counts'
  Check ($first.notificationsPending -eq 1 -and -not [SparkNotificationUi]::IsWindowVisible($script:window)) 'Model B is queued and notifications do not open the hidden chat window'
  # Real Windows SHOW/HIDE/TIMEOUT callbacks drive these checks; no callbacks
  # have been synthesized at this point. Windows may suppress display by policy.
  Wait-For {(Status).notificationsSubmitted -eq 2} 45000;$second=Status
  Check ($second.lastNotification.model -ceq $beta -and $second.lastNotification.chats -eq 1 -and $second.lastNotification.memories -eq 1) 'Next Windows notification belongs to model B with no mixed counts'
  Wait-NoticeEnd;$actualShown=(Status).notificationsShown
  Check ((Status).notificationsPending -eq 0) 'Windows completion releases the notification queue'
  $stateBefore=(State)|ConvertTo-Json -Depth 30 -Compress
  # Exercise the documented click callback on the owned app after the real
  # banners finish; never click or move the user's desktop pointer.
  [void][SparkNotificationUi]::PostMessage($script:window,0x8005,0,((343 -shl 16) -bor 0x405))
  Wait-For {$script:activity=[SparkNotificationUi]::Find($app.Id,'SparkActivity');[SparkNotificationUi]::IsWindowVisible($script:activity)}
  Check ((Body).Contains('I use a telescope.') -and (Body).Contains('I prefer detailed replies.')) 'Notification click opens grouped Import activity with both model owners'
  Check (((State)|ConvertTo-Json -Depth 30 -Compress) -ceq $stateBefore) 'Notification activation changes neither chat selection nor workspace data'
  Check-Source;Check ((Status).notificationsSubmitted -eq 2) 'Unchanged polls do not repeat notifications'
  Add-Turn $alpha 'a' 'Thanks for your help.';Publish;Check-Source
  Check ((Status).notificationsSubmitted -eq 2 -and (State).gaboActivity.Count -eq 2) 'Message-only updates without a new memory remain quiet'
  New-Item -ItemType Directory -Path (Join-Path $data 'workspace.json.tmp')|Out-Null
  Add-Turn $alpha 'a' 'I like astronomy.';Publish;Check-Source
  Check (-not (Status).lastCheckSucceeded -and (Status).notificationsSubmitted -eq 2 -and (Status).notificationsPending -eq 0) 'Failed workspace commits cannot emit a new-memory notification'
  Remove-Item -LiteralPath (Join-Path $data 'workspace.json.tmp');Check-Source
  Wait-For {(Status).notificationsSubmitted -eq 3};$learned=Status
  Check ($learned.lastNotification.model -ceq $alpha -and $learned.lastNotification.chats -eq 0 -and $learned.lastNotification.memories -eq 1) 'Retry announces a newly learned memory even when no new chat was added'
  # Turning off notifications cancels a currently submitted banner and clears
  # pending notices, while the importer and learning continue.
  [void][SparkNotificationUi]::PostMessage($script:activity,0x111,403,0);Wait-For {-not (Status).importNotificationsEnabled}
  Check (-not (Read-Json (Join-Path $data 'settings.json')).gaboSync.notifications -and (Status).notificationsPending -eq 0) 'Activity switch saves the preference and clears pending notifications'
  Add-Turn $beta 'b' 'I like jazz.';Add-Turn $alpha 'a-new' 'No fact to learn here.';Publish;Check-Source
  Check ((State).modelMemories[$beta].notes -contains 'I like jazz.' -and @((State).threads|Where-Object name -EQ 'a-new').Count -eq 1 -and (Status).notificationsSubmitted -eq 3) 'Muted imports still save new chats and model-specific memories'
  $history=(State).gaboActivity|ConvertTo-Json -Depth 30 -Compress;$actualShown=(Status).notificationsShown
  [void][SparkNotificationUi]::PostMessage($script:window,0x111,305,0);Check ($app.WaitForExit(10000) -and $app.ExitCode -eq 0) 'Notification-enabled app exits normally'
  $app=Launch;Wait-For {$script:window=[SparkNotificationUi]::Find($app.Id,'GuiltySpark343');(Status).processId -eq $app.Id -and (Status).completedChecks -ge 1}
  Check (-not (Status).importNotificationsEnabled -and (Status).notificationsSubmitted -eq 0 -and ((State).gaboActivity|ConvertTo-Json -Depth 30 -Compress) -ceq $history) 'Restart preserves mute and does not replay earlier import receipts'
  [void][SparkNotificationUi]::PostMessage($script:window,0x111,307,0);Wait-For {(Status).importNotificationsEnabled};Open-Activity
  Check ([SparkNotificationUi]::Send([SparkNotificationUi]::GetDlgItem($script:activity,403),0xF0,0,0).ToInt32() -eq 1) 'Tray switch enables notifications and updates the activity checkbox'
  Check-Source;Check ((Status).notificationsSubmitted -eq 0) 'Re-enabling does not replay imports made while muted'
  Add-Turn $beta 'b' 'I use a microscope.';Publish;Check-Source;Wait-For {(Status).notificationsSubmitted -eq 1};$resumed=Status
  Check ($resumed.lastNotification.model -ceq $beta -and $resumed.lastNotification.memories -eq 1 -and $resumed.lastNotification.chats -eq 0) 'New post-enable facts notify under their owning model only'
  Wait-NoticeEnd;$beforeTest=(State)|ConvertTo-Json -Depth 30 -Compress
  [void][SparkNotificationUi]::PostMessage($script:activity,0x111,404,0);Wait-For {(Status).notificationsSubmitted -eq 2}
  Check ((Status).lastNotification.test -and ((State)|ConvertTo-Json -Depth 30 -Compress) -ceq $beforeTest) 'Test notification is labeled as a test and does not invent a chat or memory'
  Wait-NoticeEnd;$actualShown+=(Status).notificationsShown
  Check (@((State).threads|Where-Object id -EQ 'draft')[0].draft -eq 'Original draft') 'Notification use preserves the unsent draft'
  Check ((State).modelMemories[$alpha].notes -notcontains 'I like jazz.' -and (State).modelMemories[$beta].notes -notcontains 'I like astronomy.') 'Model memories remain separate throughout notification delivery'
  @{passed=$true;checks=$checks;actualWindowsShowCallbacks=$actualShown;activationTest='Documented click callback simulated after real Windows notification completion';output=$OutputDirectory}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8
  @{passed=$true;checks=$checks.Count;actualWindowsShowCallbacks=$actualShown;output=$OutputDirectory}|ConvertTo-Json
}catch{@{passed=$false;error=$_.Exception.Message;checks=$checks;status=(Status)}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.json') -Encoding utf8;throw}finally{
  foreach($process in $owned){if(-not $process.HasExited){$target=[SparkNotificationUi]::Find($process.Id,'GuiltySpark343');if($target -ne [IntPtr]::Zero){[void][SparkNotificationUi]::PostMessage($target,0x111,305,0)};if(-not $process.WaitForExit(10000)){$process.Kill()}}}
}
