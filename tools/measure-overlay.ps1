param([int]$OverlayProcessId,[int]$DurationSeconds=45,[string]$ReportPath)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class OverlayResourceNative { [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flag); }'
$processorCount=[Environment]::ProcessorCount
$clock=[Diagnostics.Stopwatch]::StartNew()
$samples=@()
$initial=Get-Process -Id $OverlayProcessId
$lastCPU=$initial.TotalProcessorTime.TotalSeconds
$lastTime=$clock.Elapsed.TotalSeconds
$initial.Dispose()
while($clock.Elapsed.TotalSeconds -lt $DurationSeconds) {
 Start-Sleep -Seconds 1
 $process=Get-Process -Id $OverlayProcessId
 $now=$clock.Elapsed.TotalSeconds
 $cpu=$process.TotalProcessorTime.TotalSeconds
 $statePath=Join-Path (Split-Path $process.Path) 'session.json'
 $state=$null
 try { $state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json } catch {}
 $samples += [pscustomobject]@{seconds=$now;cpu_total_percent=100*($cpu-$lastCPU)/($now-$lastTime)/$processorCount;working_set_mib=$process.WorkingSet64/1MB;private_mib=$process.PrivateMemorySize64/1MB;handles=$process.HandleCount;threads=$process.Threads.Count;gdi=[OverlayResourceNative]::GetGuiResources($process.Handle,0);user=[OverlayResourceNative]::GetGuiResources($process.Handle,1);visible=$state.overlay_visible;fps=$state.render_fps}
 $lastCPU=$cpu;$lastTime=$now;$process.Dispose()
}
[pscustomobject]@{pid=$OverlayProcessId;logical_processors=$processorCount;duration_seconds=$clock.Elapsed.TotalSeconds;samples=$samples} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
$samples | Measure-Object cpu_total_percent,working_set_mib,private_mib,handles,threads,gdi,user -Minimum -Maximum -Average | Select-Object Property,Minimum,Maximum,Average | ConvertTo-Json
