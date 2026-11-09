#requires -Version 5.1
<#
Matched ordinary-application measurements on a DISPOSABLE Windows desktop.
Runs old/new sequentially from the same newly-created save fixture. UI Automation
invokes only this process's actual popup menu; no global mouse/keyboard injection.
Autonomous activity is intentionally labelled nondeterministic. Deterministic
64/128/256-frame cache stress belongs to the native cache regression, not this
old/new comparison. No result is reported until a scenario actually completes.

powershell.exe -NoProfile -File tools/benchmark_windows.ps1 -ValidateOnly
powershell.exe -NoProfile -File tools/benchmark_windows.ps1 `
  -OldExe C:\old\YeopjariCat.exe -NewExe C:\new\YeopjariCat.exe -DisposableProfile
#>
[CmdletBinding()]
param(
    [string]$OldExe,
    [string]$NewExe,
    [string]$OutputDirectory = (Join-Path (Get-Location) 'build\windows-benchmark'),
    [ValidateRange(5,40)][int]$IdleSeconds = 20,
    [ValidateRange(5,45)][int]$ActiveSeconds = 30,
    [ValidateRange(4,30)][int]$SwitchCycles = 20,
    [ValidateRange(500,2000)][int]$SwitchIntervalMs = 1000,
    [ValidateRange(5,20)][int]$SettleSeconds = 10,
    [ValidateRange(100,1000)][int]$SampleIntervalMs = 250,
    [ValidateRange(5,45)][int]$StartupTimeoutSeconds = 30,
    [switch]$DisposableProfile,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This harness requires an actual Windows desktop.' }

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;
public static class CatBenchmarkNative {
    [StructLayout(LayoutKind.Sequential)] public struct Counters {
        public uint cb, PageFaultCount;
        public UIntPtr PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage,
            QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage,
            PagefileUsage, PeakPagefileUsage, PrivateUsage;
    }
    public class Reading {
        public double CpuSeconds;
        public ulong PrivateBytes, WorkingSetBytes, PeakWorkingSetBytes, PeakCommitBytes;
        public uint PageFaultCount;
    }
    delegate bool EnumProc(IntPtr hwnd, IntPtr state);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr state);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd,StringBuilder name,int count);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll",SetLastError=true)] public static extern bool PostMessage(IntPtr hwnd,uint message,UIntPtr wp,IntPtr lp);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr process,out long created,out long exited,out long kernel,out long user);
    [DllImport("psapi.dll",SetLastError=true)] static extern bool GetProcessMemoryInfo(IntPtr process,out Counters counters,uint bytes);
    [DllImport("shell32.dll",CharSet=CharSet.Unicode)] static extern int SHGetFolderPath(IntPtr owner,int folder,IntPtr token,uint flags,StringBuilder path);
    [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    public static IntPtr[] Windows(int pid,string className,bool visibleOnly) {
        var found=new List<IntPtr>();
        EnumWindows((h,s)=>{uint p;GetWindowThreadProcessId(h,out p);var name=new StringBuilder(256);GetClassName(h,name,name.Capacity);
            if((pid<0||p==(uint)pid)&&name.ToString()==className&&(!visibleOnly||IsWindowVisible(h)))found.Add(h);return true;},IntPtr.Zero);
        return found.ToArray();
    }
    public static string SaveDirectory() {
        var path=new StringBuilder(260);if(SHGetFolderPath(IntPtr.Zero,0x001c,IntPtr.Zero,0,path)!=0)throw new Exception("CSIDL_LOCAL_APPDATA lookup failed");
        return System.IO.Path.Combine(path.ToString(),"YeopjariCat");
    }
    public static Reading Read(Process process) {
        long created,exited,kernel,user;Counters memory;
        if(!GetProcessTimes(process.Handle,out created,out exited,out kernel,out user))throw new System.ComponentModel.Win32Exception();
        if(!GetProcessMemoryInfo(process.Handle,out memory,(uint)Marshal.SizeOf(typeof(Counters))))throw new System.ComponentModel.Win32Exception();
        return new Reading { CpuSeconds=(kernel+user)/10000000.0,PrivateBytes=memory.PrivateUsage.ToUInt64(),
            WorkingSetBytes=memory.WorkingSetSize.ToUInt64(),PeakWorkingSetBytes=memory.PeakWorkingSetSize.ToUInt64(),
            PeakCommitBytes=memory.PeakPagefileUsage.ToUInt64(),PageFaultCount=memory.PageFaultCount };
    }
    public static string SignedSettings(string body) {
        ulong hash=14695981039346656037UL;
        foreach(byte value in Encoding.UTF8.GetBytes(body)){hash^=value;unchecked{hash*=1099511628211UL;}}
        return body+"checksum="+hash.ToString("x16")+"\n";
    }
}
'@

if ($ValidateOnly) {
    $probe = [CatBenchmarkNative]::Read([Diagnostics.Process]::GetCurrentProcess())
    [ordered]@{status='validated';logical_processors=[Environment]::ProcessorCount;
        counters_available=($probe.PrivateBytes -gt 0);ui_automation_loaded=$true} | ConvertTo-Json
    exit 0
}
if (-not $DisposableProfile -and $env:GITHUB_ACTIONS -ne 'true') {
    throw 'Use a disposable Windows user profile/CI runner. This script refuses ordinary user profiles by default.'
}
if (([CatBenchmarkNative]::Windows(-1,'YeopjariCat.Native.v1',$false)).Count) {
    throw 'An existing cat window is present. The benchmark will not interact with another app instance.'
}
foreach ($file in @($OldExe,$NewExe)) {
    if (-not $file -or -not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'Both -OldExe and -NewExe are required.' }
}
$OldExe = (Resolve-Path -LiteralPath $OldExe).Path
$NewExe = (Resolve-Path -LiteralPath $NewExe).Path
if ((Get-Item -LiteralPath $OldExe).VersionInfo.FileVersion -notmatch '^2\.0\.12(?:\.|$)') {
    throw 'The comparison baseline must be the real v2.0.12 executable.'
}
$budget = $IdleSeconds + $ActiveSeconds + $SwitchCycles*$SwitchIntervalMs/1000 + $SettleSeconds + $StartupTimeoutSeconds
if ($budget -gt 180) { throw 'Timed scenarios plus startup timeout exceed three minutes per executable.' }
$save = [CatBenchmarkNative]::SaveDirectory()
if (Test-Path -LiteralPath $save) { throw 'YeopjariCat save directory already exists. Allocate a clean disposable profile; no existing records will be touched.' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $OutputDirectory) -and @(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count) { throw 'Output directory is not empty; use a fresh output directory.' }
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$token = [Guid]::NewGuid().ToString('N')
$marker = '.codex-benchmark-owned'
$logical = [Environment]::ProcessorCount
$clock = [Diagnostics.Stopwatch]::StartNew()
$events = [System.Collections.Generic.List[object]]::new()
$ownedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$encoding = [System.Text.UTF8Encoding]::new($false)

function Write-Text([string]$Path,[string]$Text) { [IO.File]::WriteAllText($Path,$Text,$encoding) }
function Find-AppWindow([Diagnostics.Process]$Process) {
    $deadline=[Diagnostics.Stopwatch]::StartNew()
    while ($deadline.Elapsed.TotalSeconds -lt $StartupTimeoutSeconds) {
        $Process.Refresh();if($Process.HasExited){throw "App exited during startup: $($Process.ExitCode)"}
        $windows=[CatBenchmarkNative]::Windows($Process.Id,'YeopjariCat.Native.v1',$true)
        if($windows.Count -eq 1){return $windows[0]}
        Start-Sleep -Milliseconds 50
    }
    throw 'No unique visible main window was discovered before startup timeout.'
}
function Start-Cat([string]$Exe) {
    $started=[Diagnostics.Stopwatch]::StartNew()
    $process=Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) -PassThru
    $ownedProcesses.Add($process)
    $window=Find-AppWindow $process
    $reading=[CatBenchmarkNative]::Read($process)
    return [pscustomobject]@{process=$process;window=$window;startup_ms=$started.Elapsed.TotalMilliseconds;startup=$reading;elapsed=$started}
}
function Close-Cat($App) {
    if(-not $App){return};$App.process.Refresh()
    if(-not $App.process.HasExited){
        [CatBenchmarkNative]::PostMessage($App.window,0x0010,[UIntPtr]::Zero,[IntPtr]::Zero) | Out-Null
        if(-not $App.process.WaitForExit(5000)){throw 'App did not exit normally after WM_CLOSE.'}
        if($App.process.ExitCode -ne 0){throw "App exited with $($App.process.ExitCode)"}
    }
}
function Find-MenuItem([int]$AppPid,[string]$Prefix) {
    $pidCondition=[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ProcessIdProperty,$AppPid)
    $typeCondition=[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::MenuItem)
    $conditions=[System.Windows.Automation.Condition[]]@($pidCondition,$typeCondition)
    $condition=[System.Windows.Automation.AndCondition]::new($conditions)
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt 3){
        $items=[System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Descendants,$condition)
        foreach($item in $items){
            if(-not $item.Current.IsOffscreen -and $item.Current.Name.StartsWith($Prefix,[StringComparison]::Ordinal)){return $item}
        }
        Start-Sleep -Milliseconds 40
    }
    throw "Visible menu item was not found: $Prefix"
}
function Invoke-AppMenu($App,[string]$Item,[string]$Parent='') {
    $timer=[Diagnostics.Stopwatch]::StartNew()
    try {
        # TrackPopupMenu uses TPM_RETURNCMD; sending WM_COMMAND would silently
        # do nothing in v2.0.12. Invoke the real process-owned menu through UIA.
        if(-not [CatBenchmarkNative]::PostMessage($App.window,0x007b,[UIntPtr]::Zero,[IntPtr](-1))){throw 'Context menu request failed'}
        if($Parent){
            $node=Find-MenuItem $App.process.Id $Parent
            $expand=$node.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
            $expand.Expand()
        }
        $node=Find-MenuItem $App.process.Id $Item
        $pattern=$null
        if($node.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}
        elseif($node.TryGetCurrentPattern([System.Windows.Automation.LegacyIAccessiblePattern]::Pattern,[ref]$pattern)){$pattern.DoDefaultAction()}
        else{throw 'Menu item supports no invocation pattern'}
        $events.Add([ordered]@{item=$Item;parent=$Parent;pid=$App.process.Id;elapsed_ms=$timer.Elapsed.TotalMilliseconds;status='invoked'})
    } catch {
        [CatBenchmarkNative]::PostMessage($App.window,0x001f,[UIntPtr]::Zero,[IntPtr]::Zero) | Out-Null
        throw
    }
}
function Wait-SavedValue([string]$File,[string]$Value) {
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt 3){
        if(Test-Path -LiteralPath (Join-Path $save $File)){
            $text=[IO.File]::ReadAllText((Join-Path $save $File))
            if(($text -split "`n") -contains $Value){return}
        }
        Start-Sleep -Milliseconds 30
    }
    throw "Menu action was not confirmed in owned fixture state: $File $Value"
}
function Read-Sample($App,[Diagnostics.Stopwatch]$Timer) {
    $r=[CatBenchmarkNative]::Read($App.process)
    return [pscustomobject][ordered]@{elapsed_ms=$Timer.Elapsed.TotalMilliseconds;cpu_seconds=$r.CpuSeconds;
        private_bytes=$r.PrivateBytes;working_set_bytes=$r.WorkingSetBytes;
        os_peak_working_set_bytes=$r.PeakWorkingSetBytes;os_peak_commit_bytes=$r.PeakCommitBytes;page_faults=$r.PageFaultCount}
}
function Measure-Scenario($App,[string]$Name,[double]$Seconds,[scriptblock]$Action=$null,[int]$ActionCount=0) {
    $samples=[System.Collections.Generic.List[object]]::new();$timer=[Diagnostics.Stopwatch]::StartNew();$nextAction=0;$index=0
    $samples.Add((Read-Sample $App $timer))
    while($(if($Action){$index -lt $ActionCount}else{$timer.Elapsed.TotalSeconds -lt $Seconds})){
        if($App.elapsed.Elapsed.TotalSeconds -gt 175){throw 'Per-executable benchmark exceeded the three-minute budget'}
        if($Action -and $timer.Elapsed.TotalMilliseconds -ge $nextAction){& $Action $index;$index++;$nextAction=$index*$SwitchIntervalMs}
        Start-Sleep -Milliseconds $SampleIntervalMs
        $App.process.Refresh();if($App.process.HasExited){throw "App exited during $Name"}
        $samples.Add((Read-Sample $App $timer))
    }
    $first=$samples[0];$last=$samples[$samples.Count-1];$wall=($last.elapsed_ms-$first.elapsed_ms)/1000
    $cpu=($last.cpu_seconds-$first.cpu_seconds);$meanTime=($samples | Measure-Object elapsed_ms -Average).Average
    $meanPrivate=($samples | Measure-Object private_bytes -Average).Average;$numerator=0.0;$denominator=0.0
    foreach($s in $samples){$dt=($s.elapsed_ms-$meanTime)/1000;$numerator+=$dt*($s.private_bytes-$meanPrivate);$denominator+=$dt*$dt}
    return [ordered]@{name=$Name;wall_seconds=$wall;cpu_seconds=$cpu;
        cpu_one_core_percent=100*$cpu/$wall;cpu_machine_percent=100*$cpu/$wall/$logical;
        private_bytes_start=$first.private_bytes;private_bytes_end=$last.private_bytes;
        private_bytes_peak_sampled=($samples | Measure-Object private_bytes -Maximum).Maximum;
        working_set_peak_sampled=($samples | Measure-Object working_set_bytes -Maximum).Maximum;
        os_peak_working_set_bytes=$last.os_peak_working_set_bytes;os_peak_commit_bytes=$last.os_peak_commit_bytes;
        observed_private_growth_bytes=([double]$last.private_bytes-[double]$first.private_bytes);
        private_growth_slope_bytes_per_second=$(if($denominator){$numerator/$denominator}else{0});
        action_count=$index;samples=$samples.ToArray()}
}

function Write-PerformanceSummary($Data,[string]$Path) {
    $rows=[System.Collections.Generic.List[object]]::new()
    for($scenario=0;$scenario -lt 4;$scenario++){
        for($version=0;$version -lt 2;$version++){
            $run=$Data.runs[$version];$phase=$run.scenarios[$scenario]
            $rows.Add([pscustomobject]@{label=($run.label+' / '+$phase.name);version=$version;
                cpu=[double]$phase.cpu_one_core_percent;private_mib=[double]$phase.private_bytes_peak_sampled/1048576})
        }
    }
    $maxCpu=[math]::Max(1,($rows | Measure-Object cpu -Maximum).Maximum)
    $maxMemory=[math]::Max(1,($rows | Measure-Object private_mib -Maximum).Maximum)
    $bitmap=[Drawing.Bitmap]::new(1200,650)
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    $title=[Drawing.Font]::new('Segoe UI',[single]22,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
    $font=[Drawing.Font]::new('Segoe UI',[single]13,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
    $colors=@([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(48,115,186)),[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(221,132,35)))
    try {
        $graphics.Clear([Drawing.Color]::White)
        $graphics.DrawString('Measured ordinary-app comparison: v2.0.12 / candidate',$title,[Drawing.Brushes]::Black,16,14)
        $graphics.DrawString('Process CPU, one-core mean %',$font,[Drawing.Brushes]::Black,355,61)
        $graphics.DrawString('Peak sampled PrivateUsage, MiB',$font,[Drawing.Brushes]::Black,790,61)
        $index=0
        foreach($row in $rows){
            $y=94+$index*50
            $graphics.DrawString($row.label,$font,[Drawing.Brushes]::Black,16,[single]($y+4))
            $graphics.FillRectangle($colors[$row.version],355,[single]$y,[single](270*$row.cpu/$maxCpu),25)
            $graphics.FillRectangle($colors[$row.version],790,[single]$y,[single](270*$row.private_mib/$maxMemory),25)
            $graphics.DrawString($row.cpu.ToString('F2',[Globalization.CultureInfo]::InvariantCulture),$font,[Drawing.Brushes]::Black,635,[single]($y+4))
            $graphics.DrawString($row.private_mib.ToString('F2',[Globalization.CultureInfo]::InvariantCulture),$font,[Drawing.Brushes]::Black,1070,[single]($y+4))
            $index++
        }
        $graphics.DrawString('Same disposable fixture and time schedule; one sequential run; autonomous action mix is nondeterministic.',$font,[Drawing.Brushes]::Black,16,518)
        $graphics.DrawString('Sampled memory peaks and short-term growth are observations, not proof that no leak exists.',$font,[Drawing.Brushes]::Black,16,541)
        $y=570
        foreach($run in $Data.runs){
            $phase=$run.scenarios[2]
            $text=$run.label+': repeated-switch private growth '+([double]$phase.observed_private_growth_bytes/1048576).ToString('F3',[Globalization.CultureInfo]::InvariantCulture)+' MiB; completed commands '+$phase.action_count
            $graphics.DrawString($text,$font,[Drawing.Brushes]::Black,16,[single]$y);$y+=25
        }
        $bitmap.Save($Path,[Drawing.Imaging.ImageFormat]::Png)
    } finally {
        foreach($brush in $colors){$brush.Dispose()};$font.Dispose();$title.Dispose();$graphics.Dispose();$bitmap.Dispose()
    }
}

$result=[ordered]@{schema=1;status='running';started_utc=[DateTime]::UtcNow.ToString('o');
    conditions=[ordered]@{disposable_profile=$true;logical_processors=$logical;windows=[Environment]::OSVersion.VersionString;
        system_dpi=[CatBenchmarkNative]::GetDpiForSystem();screen_width=[CatBenchmarkNative]::GetSystemMetrics(0);
        screen_height=[CatBenchmarkNative]::GetSystemMetrics(1);sample_interval_ms=$SampleIntervalMs;
        idle_seconds=$IdleSeconds;active_seconds=$ActiveSeconds;switch_cycles=$SwitchCycles;switch_interval_ms=$SwitchIntervalMs;
        settle_seconds=$SettleSeconds;cpu_normalization='process kernel+user CPU / measured wall / logical processors';
        executable_file_cache='each executable stream read immediately before launch; warm filesystem cache';
        autonomy='same saved state and wall schedule; random action mix is not deterministic';
        repetition='real UI menu character changes, confirmed in private fixture files; deterministic frame-cache stress is separate';
        peak_private='maximum sampled PrivateUsage; OS peak commit is reported separately';
        leakage='observed growth/slope and post-loop settling only; a short run cannot prove absence of leaks'};
    runs=@();automation_events=@()}
$app=$null
try {
    [IO.Directory]::CreateDirectory($save) | Out-Null;Write-Text (Join-Path $save $marker) $token
    Write-Text (Join-Path $save 'update-settings.ini') "schema=1`nautomatic=0`n"
    Write-Text (Join-Path $save 'interaction-settings.ini') ([CatBenchmarkNative]::SignedSettings("schema=1`nwork_interference=0`nseasonal_events=0`n"))
    Write-Text (Join-Path $save 'preferences.ini') ([CatBenchmarkNative]::SignedSettings("schema=2`ncharacter=cream`noutfit=none`n"))
    $app=Start-Cat $OldExe;Start-Sleep -Milliseconds 1200;Close-Cat $app;$app=$null
    $fixture=Join-Path $OutputDirectory 'fixture';[IO.Directory]::CreateDirectory($fixture) | Out-Null
    Get-ChildItem -LiteralPath $save -File | Where-Object Name -ne $marker | Copy-Item -Destination $fixture
    $result.fixture_hashes=@(Get-ChildItem -LiteralPath $fixture -File | Sort-Object Name | ForEach-Object {
        [ordered]@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
    foreach($entry in @(@{label='baseline';exe=$OldExe},@{label='candidate';exe=$NewExe})) {
        Get-ChildItem -LiteralPath $save -File | Where-Object Name -ne $marker | Remove-Item -Force
        Get-ChildItem -LiteralPath $fixture -File | Copy-Item -Destination $save
        # Match warm filesystem-cache conditions without altering the app.
        $exeStream=[IO.File]::OpenRead($entry.exe);try{$buffer=New-Object byte[] (1024*1024);while($exeStream.Read($buffer,0,$buffer.Length) -gt 0){}}finally{$exeStream.Dispose()}
        $app=Start-Cat $entry.exe
        $run=[ordered]@{label=$entry.label;exe_sha256=(Get-FileHash -LiteralPath $entry.exe -Algorithm SHA256).Hash.ToLowerInvariant();
            file_version=(Get-Item -LiteralPath $entry.exe).VersionInfo.FileVersion;exe_bytes=(Get-Item -LiteralPath $entry.exe).Length;
            startup_ms=$app.startup_ms;startup_cpu_seconds=$app.startup.CpuSeconds;startup_private_bytes=$app.startup.PrivateBytes;scenarios=@()}
        Invoke-AppMenu $app '이 자리에서 쉬기';Wait-SavedValue 'pet.ini' 'paused=1'
        $run.scenarios+=,(Measure-Scenario $app 'paused_idle' $IdleSeconds)
        Invoke-AppMenu $app '이 자리에서 쉬기';Wait-SavedValue 'pet.ini' 'paused=0'
        $run.scenarios+=,(Measure-Scenario $app 'ordinary_autonomous_activity' $ActiveSeconds)
        Invoke-AppMenu $app '이 자리에서 쉬기';Wait-SavedValue 'pet.ini' 'paused=1'
        $switch={param($index)
            if($index%2 -eq 0){Invoke-AppMenu $app '턱시도냥' '캐릭터 선택';Wait-SavedValue 'preferences.ini' 'character=tuxedo'}
            else{Invoke-AppMenu $app '크림 치즈냥' '캐릭터 선택';Wait-SavedValue 'preferences.ini' 'character=cream'}
        }
        $run.scenarios+=,(Measure-Scenario $app 'repeated_character_switch' ($SwitchCycles*$SwitchIntervalMs/1000) $switch $SwitchCycles)
        $run.scenarios+=,(Measure-Scenario $app 'post_repeat_idle_settle' $SettleSeconds)
        Close-Cat $app;$app=$null;$result.runs+=,$run
        $csvRows=foreach($scenario in $run.scenarios){foreach($sample in $scenario.samples){
            [pscustomobject][ordered]@{version=$run.label;scenario=$scenario.name;elapsed_ms=$sample.elapsed_ms;
                cpu_seconds=$sample.cpu_seconds;private_bytes=$sample.private_bytes;working_set_bytes=$sample.working_set_bytes;
                os_peak_working_set_bytes=$sample.os_peak_working_set_bytes;os_peak_commit_bytes=$sample.os_peak_commit_bytes}
        }}
        $csvRows | Export-Csv -LiteralPath (Join-Path $OutputDirectory ($entry.label+'-samples.csv')) -NoTypeInformation -Encoding UTF8
        Write-Text (Join-Path $OutputDirectory 'partial-result.json') ($result | ConvertTo-Json -Depth 20)
    }
    $result.status='completed';$result.completed_utc=[DateTime]::UtcNow.ToString('o');$result.automation_events=$events.ToArray()
    Write-PerformanceSummary $result (Join-Path $OutputDirectory 'performance-summary.png')
    Write-Text (Join-Path $OutputDirectory 'result.json') ($result | ConvertTo-Json -Depth 20)
    Write-Output (Join-Path $OutputDirectory 'result.json')
} catch {
    $result.status='failed';$result.error=$_.Exception.Message;$result.automation_events=$events.ToArray()
    Write-Text (Join-Path $OutputDirectory 'failed-result.json') ($result | ConvertTo-Json -Depth 20)
    throw
} finally {
    foreach($process in $ownedProcesses){
        $process.Refresh();if(-not $process.HasExited){$process.CloseMainWindow() | Out-Null;if(-not $process.WaitForExit(2000)){$process.Kill();$process.WaitForExit()}}
        $process.Dispose()
    }
    if((Test-Path -LiteralPath (Join-Path $save $marker)) -and ([IO.File]::ReadAllText((Join-Path $save $marker)) -eq $token)){
        Remove-Item -LiteralPath $save -Recurse -Force
    }
}
