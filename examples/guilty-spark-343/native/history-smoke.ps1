param([string]$Executable = '', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (-not $Executable) { $Executable = Join-Path $repository 'output/guilty-spark-343/windows-x64/GuiltySpark343.exe' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository ('output/guilty-spark-343/history-smoke-' + [Guid]::NewGuid().ToString('N')) }
$Executable = [IO.Path]::GetFullPath($Executable)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh test directory; existing workspaces are preserved.' }
$data = Join-Path $OutputDirectory 'workspace'
New-Item -ItemType Directory -Path $data -Force | Out-Null

# Operate only on windows belonging to the test process started below. The
# user's open application and account workspace are never selected or edited.
if (-not ('SparkHistoryUi' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class SparkHistoryUi {
  delegate bool WindowCallback(IntPtr window, IntPtr context);
  [DllImport("user32.dll")] static extern bool EnumWindows(WindowCallback callback, IntPtr context);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int length);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string type, string name);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr window, uint message, IntPtr length, StringBuilder text);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr WriteText(IntPtr window, uint message, IntPtr w, string text);
  public static bool SetWindowText(IntPtr window, string text) { return WriteText(window, 0xC, IntPtr.Zero, text) != IntPtr.Zero; }
  public static IntPtr Find(uint process, string type) {
    IntPtr result = IntPtr.Zero;
    EnumWindows((window, context) => { uint owner; GetWindowThreadProcessId(window, out owner); if(owner != process) return true;
      var name = new StringBuilder(128); GetClassName(window, name, name.Capacity);
      if(name.ToString() != type) return true; result = window; return false; }, IntPtr.Zero);
    return result;
  }
  public static string Text(IntPtr window) { var text = new StringBuilder(2048); ReadText(window, 0xD, (IntPtr)text.Capacity, text); return text.ToString(); }
}
'@
}
function Assert-Check([bool]$Condition, [string]$Name) {
  if (-not $Condition) { throw "History UI check failed: $Name" }
  $script:checks.Add($Name)
}
function Write-GgufFixture([string]$Path) {
  $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew)
  $writer = [IO.BinaryWriter]::new($stream)
  try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes('GGUF')); $writer.Write([uint32]3); $writer.Write([uint64]1); $writer.Write([uint64]1)
    $key = [Text.Encoding]::UTF8.GetBytes('general.architecture'); $value = [Text.Encoding]::UTF8.GetBytes('llama')
    $writer.Write([uint64]$key.Length); $writer.Write($key); $writer.Write([uint32]8); $writer.Write([uint64]$value.Length); $writer.Write($value)
  } finally { $writer.Dispose(); $stream.Dispose() }
}
function Start-TestApp([string]$Model = '') {
  $info = [Diagnostics.ProcessStartInfo]::new()
  $info.FileName = $Executable; $info.UseShellExecute = $false; $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
  foreach ($value in @('--config', (Join-Path $OutputDirectory 'config.json'), '--data-dir', $data)) { $info.ArgumentList.Add($value) }
  if ($Model) { $info.ArgumentList.Add('--model'); $info.ArgumentList.Add($Model) }
  $script:testProcess = [Diagnostics.Process]::Start($info)
  $script:window = [IntPtr]::Zero
  for ($i = 0; $i -lt 200; $i++) {
    if ($testProcess.HasExited) { throw 'The test app exited before discovery completed.' }
    $script:window = [SparkHistoryUi]::Find($testProcess.Id, 'GuiltySpark343')
    if ($window -ne [IntPtr]::Zero) {
      $combo = [SparkHistoryUi]::GetDlgItem($window,107)
      if ([SparkHistoryUi]::SendMessage($combo,0x146,0,0).ToInt64() -eq 2 -and [SparkHistoryUi]::IsWindowEnabled($combo)) { return }
    }
    Start-Sleep -Milliseconds 50
  }
  throw 'The test app did not finish model discovery.'
}
function Stop-TestApp {
  if ($script:testProcess -and -not $testProcess.HasExited) {
    [SparkHistoryUi]::PostMessage($window,0x10,0,0) | Out-Null
    if (-not $testProcess.WaitForExit(5000)) { $testProcess.Kill(); $testProcess.WaitForExit(); throw 'The owned test application did not close normally.' }
  }
  if ($script:testProcess) { $testProcess.Dispose(); $script:testProcess = $null }
}
function Select-Item([int]$Control, [int]$Index) {
  $combo = [SparkHistoryUi]::GetDlgItem($window,$Control)
  [SparkHistoryUi]::SendMessage($combo,0x14E,$Index,0) | Out-Null
  [SparkHistoryUi]::SendMessage($window,0x111,(0x10000 -bor $Control),$combo) | Out-Null
}
function Selected-Character { [SparkHistoryUi]::Text([SparkHistoryUi]::GetDlgItem($window,110)) }
function Read-State {
  for($attempt=0;$attempt -lt 30;$attempt++) {
    try { return (Get-Content -LiteralPath (Join-Path $data 'workspace.json') -Raw | ConvertFrom-Json) }
    catch { if($attempt -eq 29){throw};Start-Sleep -Milliseconds 25 }
  }
}

# A deterministic loopback inference fixture lets the actual desktop send and
# resume chats. Only synthetic text enters it; no model or user data is read.
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Collections.Concurrent;
public sealed class SparkHistoryServer : IDisposable {
  readonly TcpListener listener = new TcpListener(IPAddress.Loopback, 0);
  readonly Thread worker;
  volatile bool stopped;
  public volatile bool FailTags, EmptyCatalog;
  public readonly ConcurrentQueue<string> Requests = new ConcurrentQueue<string>();
  public string Address { get; private set; }
  public SparkHistoryServer() {
    listener.Start(); Address = "http://127.0.0.1:" + ((IPEndPoint)listener.LocalEndpoint).Port;
    worker = new Thread(Serve); worker.IsBackground = true; worker.Start();
  }
  void Serve() {
    while(!stopped) {
      try { using(var client = listener.AcceptTcpClient()) {
        client.ReceiveTimeout=5000; client.SendTimeout=5000;
        using(var stream=client.GetStream()) {
          var bytes=new MemoryStream(); int b; string headers="";
          while(bytes.Length<16384 && (b=stream.ReadByte())>=0) {
            bytes.WriteByte((byte)b);
            if(b==10) { headers=Encoding.ASCII.GetString(bytes.ToArray()); if(headers.EndsWith("\r\n\r\n")) break; }
          }
          int length=0; foreach(var line in headers.Split('\n')) if(line.StartsWith("Content-Length:",StringComparison.OrdinalIgnoreCase)) length=int.Parse(line.Substring(15).Trim());
          if(length<0 || length>4*1024*1024) continue;
          var payload=new byte[length]; int read=0;
          while(read<length) { int got=stream.Read(payload,read,length-read); if(got<=0) throw new IOException(); read+=got; }
          string response; int status=200;
          if(headers.StartsWith("GET /api/tags ")) {
            if(FailTags) { response="{}";status=500; }
            else response=EmptyCatalog?"{\"models\":[]}":"{\"models\":[{\"name\":\"model-a:latest\"},{\"name\":\"model-b:latest\"}]}";
          } else if(headers.StartsWith("POST /api/chat ")) {
            var request=Encoding.UTF8.GetString(payload); Requests.Enqueue(request);
            response="{\"message\":{\"content\":\""+(request.Contains("model-a:latest")?"MODEL_A_REPLY":"MODEL_B_REPLY")+"\"},\"done\":true}\n";
          } else { response="{}";status=404; }
          var body=Encoding.UTF8.GetBytes(response);
          var head=Encoding.ASCII.GetBytes("HTTP/1.1 "+status+" Test\r\nContent-Type: application/x-ndjson\r\nContent-Length: "+body.Length+"\r\nConnection: close\r\n\r\n");
          stream.Write(head,0,head.Length);stream.Write(body,0,body.Length);
        }
      }} catch(Exception) { if(stopped) return; }
    }
  }
  public void Dispose() { stopped=true;listener.Stop();worker.Join(6000); }
}
'@
function Control-Text([int]$Id) { [SparkHistoryUi]::Text([SparkHistoryUi]::GetDlgItem($window,$Id)) }
function Set-Prompt([string]$Text) { [SparkHistoryUi]::SetWindowText([SparkHistoryUi]::GetDlgItem($window,114),$Text) | Out-Null }
function Command([int]$Id) { [SparkHistoryUi]::SendMessage($window,0x111,$Id,0) | Out-Null }
function Chat-Count { [SparkHistoryUi]::SendMessage([SparkHistoryUi]::GetDlgItem($window,100),0x18B,0,0).ToInt64() }
function Wait-Idle {
  for($i=0;$i -lt 200;$i++) {
    if($testProcess.HasExited) { throw 'The test app exited unexpectedly.' }
    if([SparkHistoryUi]::IsWindowEnabled([SparkHistoryUi]::GetDlgItem($window,107))) { [SparkHistoryUi]::SendMessage($window,0,0,0) | Out-Null; return }
    Start-Sleep -Milliseconds 25
  }
  throw 'The test application did not finish its request.'
}
function Send-Prompt([string]$Text) { Set-Prompt $Text; Command 115; Wait-Idle }
function Select-Chat([int]$Index) {
  $list=[SparkHistoryUi]::GetDlgItem($window,100)
  [SparkHistoryUi]::SendMessage($list,0x186,$Index,0) | Out-Null
  [SparkHistoryUi]::SendMessage($window,0x111,(0x10000 -bor 100),$list) | Out-Null
}
function Latest-Request { $server.Requests.ToArray()[-1] | ConvertFrom-Json }

$script:checks=[Collections.Generic.List[string]]::new();$script:testProcess=$null;$script:window=[IntPtr]::Zero
$server=[SparkHistoryServer]::new()
@{mode='ollama';api=$server.Address;modelFolders=@();models=@()} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'config.json') -Encoding utf8
@{
  version=2;selectedModel='model-a:latest';activeCharacterId='guide';activeThreadId='a-main'
  modelCharacters=@{'model-a:latest'='guide';'model-b:latest'='coach'}
  characters=@(@{id='guide';name='Patient guide';systemPrompt='A_PERSONALITY_ONLY'},@{id='coach';name='Code coach';systemPrompt='B_PERSONALITY_ONLY'})
  threads=@(
    @{id='a-main';name='Existing A chat';messages=@(@{role='user';content='A_HISTORY_ONLY'},@{role='assistant';content='A_PRIOR_REPLY'})},
    @{id='a-older';name='Earlier A chat';pinned=$true;messages=@(@{role='user';content='A_OLDER_ONLY'})}
  )
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $data 'workspace.json') -Encoding utf8
try {
  Start-TestApp
  Assert-Check ((Read-State).version -eq 5) 'Legacy workspace upgrades to format 5'
  Assert-Check ((Read-State).activeThreadId -eq 'a-main' -and (Chat-Count) -eq 2) 'All legacy chats remain with the formerly selected model'
  Assert-Check ((Control-Text 113).Contains('A_PRIOR_REPLY')) 'Existing transcript is displayed'
  Set-Prompt 'A unsent draft Ω'
  Select-Item 107 1
  Assert-Check ((Chat-Count) -eq 1 -and -not (Control-Text 113).Contains('A_HISTORY_ONLY')) 'Model B starts with an independent chat list and transcript'
  Assert-Check ((Control-Text 114) -eq '') 'Model B does not inherit model A draft'
  Assert-Check ((Selected-Character) -eq 'Code coach') 'Personality switches together with history'
  Send-Prompt 'B_HISTORY_ONLY'
  Assert-Check ((Control-Text 113).Contains('MODEL_B_REPLY')) 'Actual Send handler displays model B reply'
  $request=Latest-Request; $contents=$request.messages.content -join '|'
  Assert-Check ($request.model -eq 'model-b:latest' -and $contents.Contains('B_HISTORY_ONLY') -and -not $contents.Contains('A_HISTORY_ONLY')) 'Actual HTTP request contains only model B history'
  Assert-Check ($contents.Contains('B_PERSONALITY_ONLY') -and -not $contents.Contains('A_PERSONALITY_ONLY')) 'Actual HTTP request contains model B personality'
  Set-Prompt 'B unsent draft'
  Select-Item 107 0
  Assert-Check ((Read-State).activeThreadId -eq 'a-main' -and (Control-Text 113).Contains('A_PRIOR_REPLY')) 'Switching back restores model A active chat and transcript'
  Assert-Check ((Control-Text 114) -eq 'A unsent draft Ω') 'Switching back restores model A draft'
  Select-Chat 0
  Assert-Check ((Read-State).activeThreadId -eq 'a-older' -and (Control-Text 113).Contains('A_OLDER_ONLY')) 'Selecting an older pinned chat stays within the model'
  Select-Item 107 1; Select-Item 107 0
  Assert-Check ((Read-State).activeThreadId -eq 'a-older') 'Model remembers the last selected older chat'
  Select-Chat 1
  Send-Prompt 'Continue A'
  $contents=(Latest-Request).messages.content -join '|'
  Assert-Check ($contents.Contains('A_HISTORY_ONLY') -and $contents.Contains('A_PRIOR_REPLY') -and $contents.Contains('Continue A')) 'Continuing model A sends its previous conversation turns'
  Assert-Check (-not $contents.Contains('B_HISTORY_ONLY') -and -not $contents.Contains('MODEL_B_REPLY')) 'Continuing model A excludes model B conversation'
  Command 101
  $aNew=(Read-State).activeThreadId
  Assert-Check ((Chat-Count) -eq 3 -and -not (Control-Text 113).Contains('A_HISTORY_ONLY')) 'New chat creates a separate conversation for this model'
  Set-Prompt 'New A draft'
  Select-Item 107 1
  Assert-Check ((Control-Text 114) -eq 'B unsent draft' -and (Control-Text 113).Contains('MODEL_B_REPLY')) 'Model B retains its history and draft after model A starts a new chat'
  Send-Prompt 'Continue B'
  $contents=(Latest-Request).messages.content -join '|'
  Assert-Check ($contents.Contains('B_HISTORY_ONLY') -and $contents.Contains('MODEL_B_REPLY') -and $contents.Contains('Continue B')) 'Continuing model B includes its own earlier response'
  Assert-Check (-not $contents.Contains('A_HISTORY_ONLY')) 'Continued model B request excludes model A history'
  $bMain=(Read-State).activeThreadId
  Command 101
  Assert-Check ((Chat-Count) -eq 2) 'Multiple chats are supported for model B'
  [SparkHistoryUi]::PostMessage($window,0x111,103,0) | Out-Null
  $dialog=[IntPtr]::Zero
  for($i=0;$i -lt 100;$i++){ $dialog=[SparkHistoryUi]::Find($testProcess.Id,'#32770');if($dialog -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 25 }
  Assert-Check ($dialog -ne [IntPtr]::Zero) 'Delete confirmation belongs to the owned test app'
  [SparkHistoryUi]::SendMessage($dialog,0x111,6,0) | Out-Null
  for($i=0;$i -lt 100 -and (Chat-Count) -ne 1;$i++){Start-Sleep -Milliseconds 25}
  Assert-Check ((Read-State).activeThreadId -eq $bMain -and (Chat-Count) -eq 1) 'Deleting a chat restores another chat from the same model'
  Command 102
  $editor=[SparkHistoryUi]::Find($testProcess.Id,'SparkEditor')
  $field=[SparkHistoryUi]::FindWindowEx($editor,[IntPtr]::Zero,'Edit',$null)
  [SparkHistoryUi]::SetWindowText($field,'Renamed model B chat') | Out-Null
  [SparkHistoryUi]::SendMessage($editor,0x111,200,0) | Out-Null
  Command 104
  $saved=Read-State;$b=$saved.threads | Where-Object id -eq $bMain
  Assert-Check ($b.name -eq 'Renamed model B chat' -and $b.pinned) 'Rename and pin preserve conversation identity'
  Set-Prompt 'Restart B draft'
  Command 108;Wait-Idle
  Assert-Check ((Read-State).activeThreadId -eq $bMain -and (Control-Text 114) -eq 'Restart B draft') 'Catalog refresh retains the conversation and draft'
  $server.FailTags=$true;Command 108;Wait-Idle
  Assert-Check ((Read-State).activeThreadId -eq $bMain -and (Chat-Count) -eq 1) 'Failed refresh retains model history'
  $server.FailTags=$false;$server.EmptyCatalog=$true;Command 108;Wait-Idle
  Assert-Check ((Read-State).selectedModel -eq 'model-b:latest' -and (Read-State).activeThreadId -eq $bMain) 'Empty catalog preserves the unavailable model and its history'
  $server.EmptyCatalog=$false;Command 108;Wait-Idle
  Stop-TestApp;Start-TestApp
  Assert-Check ((Read-State).activeThreadId -eq $bMain -and (Selected-Character) -eq 'Code coach') 'Restart restores model B chat and personality'
  Assert-Check ((Control-Text 113).Contains('Continue B') -and (Control-Text 114) -eq 'Restart B draft') 'Restart restores transcript and unsent draft'
  Select-Item 107 0
  Assert-Check ((Read-State).activeThreadId -eq $aNew -and (Chat-Count) -eq 3 -and (Control-Text 114) -eq 'New A draft') 'Restart preserves model A separate active chat and draft'
  Select-Chat 2
  Assert-Check ((Read-State).activeThreadId -eq 'a-main' -and (Control-Text 113).Contains('Continue A')) 'Older model A history remains accessible after restart'
  Select-Item 107 1;Stop-TestApp;Start-TestApp 'model-a:latest'
  Assert-Check ((Read-State).activeThreadId -eq 'a-main' -and (Control-Text 113).Contains('A_PRIOR_REPLY')) 'Command-line model selection restores saved chat history'
  Stop-TestApp
  $receipt=@{passed=$true;checks=$checks.ToArray();httpRequests=$server.Requests.Count;scenario='per-model histories through real Windows controls and HTTP'}
  $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
  $receipt | ConvertTo-Json -Depth 5
} catch {
  @{passed=$false;checks=$checks.ToArray();error=$_.Exception.Message} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
  throw
} finally { Stop-TestApp;$server.Dispose() }




