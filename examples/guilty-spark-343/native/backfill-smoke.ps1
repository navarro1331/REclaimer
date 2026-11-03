param([string]$Executable = '', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (-not $Executable) { $Executable = Join-Path $repository 'output/guilty-spark-343/windows-x64/GuiltySpark343.exe' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository ('output/guilty-spark-343/backfill-smoke-' + [Guid]::NewGuid().ToString('N')) }
$Executable = [IO.Path]::GetFullPath($Executable)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh test directory; existing workspaces are preserved.' }
$data = Join-Path $OutputDirectory 'workspace'
New-Item -ItemType Directory -Path $data -Force | Out-Null

# Operate only on windows belonging to the test process started below. The
# user's open application and account workspace are never selected or edited.
if (-not ('SparkMemoryUi' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class SparkMemoryUi {
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
  if (-not $Condition) { throw "Memory UI check failed: $Name" }
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
    $script:window = [SparkMemoryUi]::Find($testProcess.Id, 'GuiltySpark343')
    if ($window -ne [IntPtr]::Zero) {
      $combo = [SparkMemoryUi]::GetDlgItem($window,107)
      if ([SparkMemoryUi]::SendMessage($combo,0x146,0,0).ToInt64() -eq 2 -and [SparkMemoryUi]::IsWindowEnabled($combo)) { return }
    }
    Start-Sleep -Milliseconds 50
  }
  throw 'The test app did not finish model discovery.'
}
function Stop-TestApp {
  if ($script:testProcess -and -not $testProcess.HasExited) {
    [SparkMemoryUi]::PostMessage($window,0x10,0,0) | Out-Null
    if (-not $testProcess.WaitForExit(5000)) { $testProcess.Kill(); $testProcess.WaitForExit(); throw 'The owned test application did not close normally.' }
  }
  if ($script:testProcess) { $testProcess.Dispose(); $script:testProcess = $null }
}
function Select-Item([int]$Control, [int]$Index) {
  $combo = [SparkMemoryUi]::GetDlgItem($window,$Control)
  [SparkMemoryUi]::SendMessage($combo,0x14E,$Index,0) | Out-Null
  [SparkMemoryUi]::SendMessage($window,0x111,(0x10000 -bor $Control),$combo) | Out-Null
}
function Selected-Character { [SparkMemoryUi]::Text([SparkMemoryUi]::GetDlgItem($window,110)) }
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
public sealed class SparkMemoryServer : IDisposable {
  readonly TcpListener listener = new TcpListener(IPAddress.Loopback, 0);
  readonly Thread worker;
  volatile bool stopped;
  public volatile bool FailTags, EmptyCatalog;
  public readonly ConcurrentQueue<string> Requests = new ConcurrentQueue<string>();
  public string Address { get; private set; }
  public SparkMemoryServer() {
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
function Control-Text([int]$Id) { [SparkMemoryUi]::Text([SparkMemoryUi]::GetDlgItem($window,$Id)) }
function Set-Prompt([string]$Text) { [SparkMemoryUi]::SetWindowText([SparkMemoryUi]::GetDlgItem($window,114),$Text) | Out-Null }
function Command([int]$Id) { [SparkMemoryUi]::SendMessage($window,0x111,$Id,0) | Out-Null }
function Chat-Count { [SparkMemoryUi]::SendMessage([SparkMemoryUi]::GetDlgItem($window,100),0x18B,0,0).ToInt64() }
function Wait-Idle {
  for($i=0;$i -lt 200;$i++) {
    if($testProcess.HasExited) { throw 'The test app exited unexpectedly.' }
    if([SparkMemoryUi]::IsWindowEnabled([SparkMemoryUi]::GetDlgItem($window,107))) { [SparkMemoryUi]::SendMessage($window,0,0,0) | Out-Null; return }
    Start-Sleep -Milliseconds 25
  }
  throw 'The test application did not finish its request.'
}
function Send-Prompt([string]$Text) { Set-Prompt $Text; Command 115; Wait-Idle }
function Select-Chat([int]$Index) {
  $list=[SparkMemoryUi]::GetDlgItem($window,100)
  [SparkMemoryUi]::SendMessage($list,0x186,$Index,0) | Out-Null
  [SparkMemoryUi]::SendMessage($window,0x111,(0x10000 -bor 100),$list) | Out-Null
}
function Latest-Request { $server.Requests.ToArray()[-1] | ConvertFrom-Json }

function Notes([string]$Model) { @((Read-State).modelMemories.$Model.notes) -join '|' }
function Request-Context { ((Latest-Request).messages | Where-Object role -eq 'system').content -join '|' }
function Open-Memories {
  Command 120
  $script:editor=[SparkMemoryUi]::Find($testProcess.Id,'SparkEditor')
  if($editor -eq [IntPtr]::Zero) { throw 'Memory editor did not open.' }
}
function Set-Notes([string]$Text) { [SparkMemoryUi]::SetWindowText([SparkMemoryUi]::GetDlgItem($editor,205),$Text) | Out-Null }
function Memory-Check([int]$Id,[bool]$Value) { [SparkMemoryUi]::SendMessage([SparkMemoryUi]::GetDlgItem($editor,$Id),0xF1,[int]$Value,0) | Out-Null }
function Save-Memories { [SparkMemoryUi]::SendMessage($editor,0x111,200,0) | Out-Null }
function Fresh-Question { Command 101;Send-Prompt 'What should you know about me?' }

$script:checks=[Collections.Generic.List[string]]::new();$script:testProcess=$null;$script:window=[IntPtr]::Zero
$server=[SparkMemoryServer]::new()
@{mode='ollama';api=$server.Address;modelFolders=@();models=@()} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'config.json') -Encoding utf8
@{
  version=4;selectedModel='model-a:latest';activeThreadId='a-main';characters=@();modelCharacters=@{}
  modelThreads=@{'model-a:latest'='a-main';'model-b:latest'='b-main'}
  modelMemories=@{'model-a:latest'=@{enabled=$true;automatic=$false;notes=@('My name is CURRENT_NAME.')}}
  threads=@(
    @{id='a-main';model='model-a:latest';name='Older A chat';messages=@(
      @{role='user';content="My name is OLD_NAME.`nI prefer tea."},
      @{role='assistant';content='I like ASSISTANT_ONLY.'},
      @{role='user';content='I like OPT_OUT. Do not save this.'},
      @{role='user';content='> I like QUOTED_ONLY.'},
      @{role='user';content='I prefer a question?'}
    )},
    @{id='a-other';model='model-a:latest';name='Another A chat';messages=@(@{role='user';content="I'm learning C."})},
    @{id='b-main';model='model-b:latest';name='Older B chat';messages=@(@{role='user';content='Call me BETA_ONLY.'})}
  )
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $data 'workspace.json') -Encoding utf8
function Learn-Chats { [SparkMemoryUi]::SendMessage($editor,0x111,208,0) | Out-Null }
function Preview-Notes { [SparkMemoryUi]::Text([SparkMemoryUi]::GetDlgItem($editor,205)) }
function Scan-Result { [SparkMemoryUi]::Text([SparkMemoryUi]::GetDlgItem($editor,209)) }
try {
  Start-TestApp
  $original=Read-State
  Assert-Check ((Control-Text 120) -eq 'Memories (1)') 'Loading a workspace does not scan old messages implicitly'
  Open-Memories;Set-Notes "My name is CURRENT_NAME.`r`nEDITOR_DRAFT";Learn-Chats
  Assert-Check ((Preview-Notes).Contains('EDITOR_DRAFT') -and (Preview-Notes).Contains('I prefer tea.') -and (Preview-Notes).Contains("I'm learning C.")) 'Learning preserves unsaved edits and adds facts from both selected-model chats'
  Assert-Check (-not (Preview-Notes).Contains('OLD_NAME') -and -not (Preview-Notes).Contains('BETA_ONLY') -and -not (Preview-Notes).Contains('ASSISTANT_ONLY') -and -not (Preview-Notes).Contains('OPT_OUT') -and -not (Preview-Notes).Contains('QUOTED_ONLY') -and -not (Preview-Notes).Contains('question')) 'Preview excludes other models, assistant text, opt-outs, quotes, questions and superseded names'
  Assert-Check ((Scan-Result).Contains('2 chats, 5 user messages: 2 proposed additions.') -and (Scan-Result).Contains('1 conflicts')) 'Scan reports selected-model chat/message counts, additions and conflicts'
  Assert-Check ((Notes 'model-a:latest') -eq 'My name is CURRENT_NAME.') 'Preview has not persisted proposed notes'
  [SparkMemoryUi]::SendMessage($editor,0x111,201,0) | Out-Null
  Assert-Check ((Notes 'model-a:latest') -eq 'My name is CURRENT_NAME.' -and (Control-Text 120) -eq 'Memories (1)') 'Cancel discards the scan and unsaved edits'
  Open-Memories;Learn-Chats;Save-Memories
  Assert-Check ((Notes 'model-a:latest').Contains('tea') -and (Notes 'model-a:latest').Contains('learning C') -and (Control-Text 120) -eq 'Memories (3)') 'Save commits historical facts for the selected model'
  Assert-Check (-not (Read-State).modelMemories.'model-a:latest'.automatic -and (Read-State).modelMemories.'model-a:latest'.enabled) 'Explicit scan preserves independent automatic and use preferences'
  Assert-Check (-not (Read-State).modelMemories.'model-b:latest') 'Scanning A does not create or edit B memory'
  Fresh-Question
  Assert-Check ((Request-Context).Contains('tea') -and (Request-Context).Contains('CURRENT_NAME') -and -not (Request-Context).Contains('OLD_NAME') -and -not (Request-Context).Contains('BETA_ONLY')) 'A new chat sends A recovered memories and retains current name'
  Assert-Check (@((Latest-Request).messages | Where-Object role -eq 'user').Count -eq 1) 'New chat recall does not copy old transcript turns'
  Open-Memories;Learn-Chats
  Assert-Check ((Scan-Result).Contains('0 proposed additions.') -and (Preview-Notes).Split("`n").Count -eq 3) 'Repeating the scan does not duplicate memories'
  Save-Memories
  Select-Item 107 1;Open-Memories;Learn-Chats
  Assert-Check ((Preview-Notes).Contains('BETA_ONLY') -and -not (Preview-Notes).Contains('tea') -and (Scan-Result).Contains('1 chats, 1 user messages: 1 proposed additions.')) 'Switching model scans only the other model history'
  Save-Memories;Fresh-Question
  Assert-Check ((Request-Context).Contains('BETA_ONLY') -and -not (Request-Context).Contains('CURRENT_NAME') -and -not (Request-Context).Contains('tea')) 'B new-chat request has only B recovered notes'
  Stop-TestApp;Start-TestApp
  Assert-Check ((Read-State).selectedModel -eq 'model-b:latest' -and (Notes 'model-b:latest').Contains('BETA_ONLY')) 'Restart preserves recovered B notes and selection'
  Select-Item 107 0;Fresh-Question
  Assert-Check ((Request-Context).Contains('tea') -and -not (Request-Context).Contains('BETA_ONLY') -and -not (Read-State).modelMemories.'model-a:latest'.automatic) 'Restart restores A recovered notes and its automatic preference'
  $saved=Read-State
  foreach($prior in $original.threads) {
    $restored=$saved.threads | Where-Object id -eq $prior.id
    Assert-Check (($restored.messages | ConvertTo-Json -Depth 5 -Compress) -ceq ($prior.messages | ConvertTo-Json -Depth 5 -Compress)) "Original transcript $($prior.id) remains unchanged"
  }
  Stop-TestApp
  $receipt=@{passed=$true;checks=$checks.ToArray();httpRequests=$server.Requests.Count;scenario='learn saved chats per model through actual Windows UI, persistence and HTTP'}
  $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
  $receipt | ConvertTo-Json -Depth 5
} catch {
  @{passed=$false;checks=$checks.ToArray();error=$_.Exception.Message} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
  throw
} finally { Stop-TestApp;$server.Dispose() }