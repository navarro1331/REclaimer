param([string]$Executable = '', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (-not $Executable) { $Executable = Join-Path $repository 'output/guilty-spark-343/windows-x64/GuiltySpark343.exe' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository ('output/guilty-spark-343/personality-smoke-' + [Guid]::NewGuid().ToString('N')) }
$Executable = [IO.Path]::GetFullPath($Executable)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh test directory; existing workspaces are preserved.' }
$data = Join-Path $OutputDirectory 'workspace'
New-Item -ItemType Directory -Path $data -Force | Out-Null

# Operate only on windows belonging to the test process started below. The
# user's open application and account workspace are never selected or edited.
if (-not ('SparkPersonalityUi' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class SparkPersonalityUi {
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
  if (-not $Condition) { throw "Personality UI check failed: $Name" }
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
    $script:window = [SparkPersonalityUi]::Find($testProcess.Id, 'GuiltySpark343')
    if ($window -ne [IntPtr]::Zero) {
      $combo = [SparkPersonalityUi]::GetDlgItem($window,107)
      if ([SparkPersonalityUi]::SendMessage($combo,0x146,0,0).ToInt64() -eq 2 -and [SparkPersonalityUi]::IsWindowEnabled($combo)) { return }
    }
    Start-Sleep -Milliseconds 50
  }
  throw 'The test app did not finish model discovery.'
}
function Stop-TestApp {
  if ($script:testProcess -and -not $testProcess.HasExited) {
    [SparkPersonalityUi]::PostMessage($window,0x10,0,0) | Out-Null
    if (-not $testProcess.WaitForExit(5000)) { $testProcess.Kill(); $testProcess.WaitForExit(); throw 'The owned test application did not close normally.' }
  }
  if ($script:testProcess) { $testProcess.Dispose(); $script:testProcess = $null }
}
function Select-Item([int]$Control, [int]$Index) {
  $combo = [SparkPersonalityUi]::GetDlgItem($window,$Control)
  [SparkPersonalityUi]::SendMessage($combo,0x14E,$Index,0) | Out-Null
  [SparkPersonalityUi]::SendMessage($window,0x111,(0x10000 -bor $Control),$combo) | Out-Null
}
function Selected-Character { [SparkPersonalityUi]::Text([SparkPersonalityUi]::GetDlgItem($window,110)) }
function Read-State {
  for($attempt=0;$attempt -lt 30;$attempt++) {
    try { return (Get-Content -LiteralPath (Join-Path $data 'workspace.json') -Raw | ConvertFrom-Json) }
    catch { if($attempt -eq 29){throw};Start-Sleep -Milliseconds 25 }
  }
}

$script:checks = [Collections.Generic.List[string]]::new()
$script:testProcess = $null
$script:window = [IntPtr]::Zero
foreach ($model in @('a','b')) { Write-GgufFixture (Join-Path $OutputDirectory "$model.gguf") }
@{mode='local';llamaServer='unused-test-runner.exe';modelFolders=@();models=@(@{name='model-a:latest';path='a.gguf'},@{name='model-b:latest';path='b.gguf'})} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'config.json') -Encoding utf8
@{version=1;selectedModel='model-a:latest';activeCharacterId='guide';activeThreadId='conversation';threads=@(@{id='conversation';name='Existing conversation';messages=@(@{role='user';content='Preserve this history.'})});characters=@(@{id='guide';name='Patient guide';systemPrompt='MODEL_A_ONLY'},@{id='coder';name='Code coach';systemPrompt='MODEL_B_ONLY'})} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $data 'workspace.json') -Encoding utf8
try {
  Start-TestApp
  Assert-Check ((Selected-Character) -eq 'Patient guide') 'Legacy character remains on the original model'
  Assert-Check ((Read-State).version -eq 5) 'Workspace migrates to version 5'
  Select-Item 107 1
  Assert-Check ((Selected-Character) -eq 'Default assistant') 'An unassigned model uses Default assistant'
  Select-Item 110 2
  Select-Item 107 0
  Assert-Check ((Selected-Character) -eq 'Patient guide') 'Switching to model A restores its character'
  Select-Item 107 1
  Assert-Check ((Selected-Character) -eq 'Code coach') 'Switching to model B restores its character'
  Select-Item 110 0
  Select-Item 107 0
  Select-Item 107 1
  Assert-Check ((Selected-Character) -eq 'Default assistant') 'Default selection is remembered independently'
  [SparkPersonalityUi]::SendMessage($window,0x111,111,0) | Out-Null
  $editor = [SparkPersonalityUi]::Find($testProcess.Id,'SparkEditor')
  Assert-Check ($editor -ne [IntPtr]::Zero) 'Character editor opens'
  [SparkPersonalityUi]::SendMessage($editor,0x111,202,0) | Out-Null
  $fields = @(); $field = [IntPtr]::Zero
  for ($i=0; $i -lt 5; $i++) { $field=[SparkPersonalityUi]::FindWindowEx($editor,$field,'Edit',$null); $fields += $field }
  Assert-Check ($fields.Count -eq 5 -and $fields[-1] -ne [IntPtr]::Zero) 'Character fields are available'
  [SparkPersonalityUi]::SetWindowText($fields[0],'Story keeper') | Out-Null
  [SparkPersonalityUi]::SetWindowText($fields[1],'MODEL_B_STORY_ONLY: use a narrative style.') | Out-Null
  [SparkPersonalityUi]::SendMessage($editor,0x111,200,0) | Out-Null
  Assert-Check ((Selected-Character) -eq 'Story keeper') 'Saving a new character assigns it to the selected model'
  Select-Item 107 0
  Assert-Check ((Selected-Character) -eq 'Patient guide') 'Creating a character leaves the other model unchanged'
  Select-Item 107 1
  $saved = Read-State
  Assert-Check ($saved.modelCharacters.'model-a:latest' -eq 'guide') 'Model A assignment is on disk'
  Assert-Check ($saved.modelCharacters.'model-b:latest' -eq $saved.activeCharacterId) 'Model B assignment is on disk'
  Stop-TestApp
  Start-TestApp
  Assert-Check ((Selected-Character) -eq 'Story keeper') 'Reopening restores the selected model and character'
  Select-Item 107 0
  Assert-Check ((Selected-Character) -eq 'Patient guide') 'Reopening retains the other model assignment'
  $saved = Read-State
  $existing = $saved.threads | Where-Object id -eq 'conversation'
  Assert-Check ($saved.threads.Count -eq 2 -and $existing.messages[0].content -eq 'Preserve this history.' -and $existing.model -eq 'model-a:latest') 'Existing conversation remains intact on its original model'
  Stop-TestApp
  Start-TestApp 'model-b:latest'
  Assert-Check ((Selected-Character) -eq 'Story keeper') 'Command-line model selection restores its saved personality'
  Stop-TestApp
  $receipt = @{passed=$true;checks=$checks.ToArray();application='Guilty Spark 343';scenario='per-model personalities and restart'}
  $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
  $receipt | ConvertTo-Json -Depth 5
} catch {
  @{passed=$false;checks=$checks.ToArray();error=$_.Exception.Message} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
  throw
} finally { Stop-TestApp }


