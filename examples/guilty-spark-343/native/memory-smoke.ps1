param([string]$Executable = '', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (-not $Executable) { $Executable = Join-Path $repository 'output/guilty-spark-343/windows-x64/GuiltySpark343.exe' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository ('output/guilty-spark-343/memory-smoke-' + [Guid]::NewGuid().ToString('N')) }
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
  version=3;selectedModel='model-a:latest';activeCharacterId='guide';activeThreadId='a-main'
  modelThreads=@{'model-a:latest'='a-main'}
  modelCharacters=@{'model-a:latest'='guide';'model-b:latest'='coach'}
  characters=@(@{id='guide';name='Patient guide';systemPrompt='A_PERSONALITY_ONLY'},@{id='coach';name='Code coach';systemPrompt='B_PERSONALITY_ONLY'})
  threads=@(@{id='a-main';model='model-a:latest';name='Existing chat';messages=@(@{role='user';content='My name is OLD_HISTORY_ONLY.'})})
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $data 'workspace.json') -Encoding utf8
try {
  Start-TestApp
  Assert-Check ((Read-State).version -eq 5) 'Format 3 migrates to format 5'
  Assert-Check ((Control-Text 120) -eq 'Memories (0)') 'Model starts with no inferred historical memories'
  Assert-Check ((Read-State).activeThreadId -eq 'a-main' -and (Control-Text 113).Contains('OLD_HISTORY_ONLY')) 'Existing conversation is preserved'
  Send-Prompt "My name is ALPHA_ONLY.`nI prefer short answers."
  Assert-Check ((Notes 'model-a:latest').Contains('ALPHA_ONLY') -and (Notes 'model-a:latest').Contains('short answers')) 'New user facts and preferences save automatically'
  Assert-Check ((Control-Text 120) -eq 'Memories (2)') 'Memory button reflects saved note count'
  Assert-Check (-not (Notes 'model-a:latest').Contains('OLD_HISTORY_ONLY') -and -not (Notes 'model-a:latest').Contains('MODEL_A_REPLY')) 'Old conversations and assistant replies are not learned'
  Fresh-Question
  Assert-Check ((Request-Context).Contains('ALPHA_ONLY') -and (Request-Context).Contains('short answers')) 'A new chat receives model A saved memory in its actual request'
  Assert-Check (@((Latest-Request).messages | Where-Object role -eq 'user').Count -eq 1) 'Memory recall does not copy old conversation turns'
  Select-Item 107 1;Fresh-Question
  Assert-Check (-not (Request-Context).Contains('ALPHA_ONLY') -and (Control-Text 120) -eq 'Memories (0)') 'Model B cannot see model A memory'
  Send-Prompt 'My name is BETA_ONLY.';Fresh-Question
  Assert-Check ((Request-Context).Contains('BETA_ONLY') -and -not (Request-Context).Contains('ALPHA_ONLY')) 'Model B uses only its own saved memory across chats'
  Select-Item 107 0;Open-Memories
  $noteField=[SparkMemoryUi]::GetDlgItem($editor,205)
  Assert-Check ([SparkMemoryUi]::Text($noteField).Contains('ALPHA_ONLY') -and -not [SparkMemoryUi]::Text($noteField).Contains('BETA_ONLY')) 'Memory editor shows only selected model notes'
  Set-Notes "My name is ALPHA_CORRECTED.`r`nI prefer code examples.";Save-Memories;Fresh-Question
  Assert-Check ((Request-Context).Contains('ALPHA_CORRECTED') -and -not (Request-Context).Contains('ALPHA_ONLY') -and -not (Request-Context).Contains('short answers')) 'Editing notes replaces the context sent to new chats'
  Open-Memories;Memory-Check 207 $false;Save-Memories
  Send-Prompt 'I live in UNSAVED_PLACE.'
  Assert-Check (-not (Notes 'model-a:latest').Contains('UNSAVED_PLACE')) 'Turning off automatic saving stops capture for this model'
  Send-Prompt 'Remember that I am building EXPLICIT_ONLY.';Fresh-Question
  Assert-Check ((Request-Context).Contains('EXPLICIT_ONLY')) 'Explicit remember requests work with automatic saving off'
  Open-Memories;Memory-Check 206 $false;Save-Memories;Fresh-Question
  Assert-Check (-not (Request-Context).Contains('ALPHA_CORRECTED') -and -not (Request-Context).Contains('EXPLICIT_ONLY')) 'Turning memory off omits all stored notes from new requests'
  Assert-Check ((Control-Text 120) -eq 'Memories off' -and (Notes 'model-a:latest').Contains('ALPHA_CORRECTED')) 'Turning memory off preserves notes for later use'
  Send-Prompt '/remember DISABLED_CAPTURE'
  Assert-Check (-not (Notes 'model-a:latest').Contains('DISABLED_CAPTURE') -and (Control-Text 117).Contains('turned off')) 'Disabled explicit save reports that no memory was saved'
  Select-Item 107 1;Send-Prompt 'I live in BETA_CITY.'
  Assert-Check ((Notes 'model-b:latest').Contains('BETA_CITY') -and (Read-State).modelMemories.'model-b:latest'.automatic) 'Other model retains its independent automatic setting'
  Select-Item 107 0;Open-Memories;Memory-Check 206 $true;Set-Notes 'My name is ALPHA_CORRECTED.';Save-Memories;Fresh-Question
  Assert-Check ((Request-Context).Contains('ALPHA_CORRECTED') -and -not (Request-Context).Contains('EXPLICIT_ONLY')) 'Removing a note removes it from new-chat context'
  Open-Memories;[SparkMemoryUi]::SendMessage($editor,0x111,204,0) | Out-Null;[SparkMemoryUi]::SendMessage($editor,0x111,201,0) | Out-Null
  Assert-Check ((Notes 'model-a:latest').Contains('ALPHA_CORRECTED')) 'Cancel after Clear notes leaves saved memory intact'
  Open-Memories;[SparkMemoryUi]::SendMessage($editor,0x111,204,0) | Out-Null;Save-Memories;Fresh-Question
  Assert-Check (-not (Request-Context).Contains('ALPHA_CORRECTED') -and (Control-Text 120) -eq 'Memories (0)') 'Saving Clear notes removes this model memory'
  Assert-Check ((Notes 'model-b:latest').Contains('BETA_ONLY') -and (Notes 'model-b:latest').Contains('BETA_CITY')) 'Clearing model A leaves model B notes intact'
  Open-Memories;Set-Notes 'My favorite color is CYAN_MANUAL.';Save-Memories
  Select-Item 110 2;Fresh-Question
  Assert-Check ((Request-Context).Contains('CYAN_MANUAL') -and (Selected-Character) -eq 'Code coach') 'Changing character preserves model-specific memory'
  Stop-TestApp;Start-TestApp;Fresh-Question
  Assert-Check ((Request-Context).Contains('CYAN_MANUAL') -and -not (Read-State).modelMemories.'model-a:latest'.automatic) 'Restart restores model A notes and automatic preference'
  Select-Item 107 1;Fresh-Question
  Assert-Check ((Request-Context).Contains('BETA_ONLY') -and (Request-Context).Contains('BETA_CITY') -and -not (Request-Context).Contains('CYAN_MANUAL')) 'Restart restores model B separate notes'
  Send-Prompt '/forget all';Fresh-Question
  Assert-Check (-not (Request-Context).Contains('BETA_ONLY') -and -not (Request-Context).Contains('BETA_CITY')) 'Forget command clears only selected model memory'
  Stop-TestApp;Start-TestApp 'model-a:latest';Fresh-Question
  Assert-Check ((Request-Context).Contains('CYAN_MANUAL')) 'Command-line model selection recalls its saved memory'
  Stop-TestApp
  $receipt=@{passed=$true;checks=$checks.ToArray();httpRequests=$server.Requests.Count;scenario='per-model memories through real Windows controls and HTTP'}
  $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
  $receipt | ConvertTo-Json -Depth 5
} catch {
  @{passed=$false;checks=$checks.ToArray();error=$_.Exception.Message} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
  throw
} finally { Stop-TestApp;$server.Dispose() }



