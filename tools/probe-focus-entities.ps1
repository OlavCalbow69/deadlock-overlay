$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LiveMemory {
 [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, UIntPtr count, out UIntPtr read);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
$gameProcess = Get-Process -Name deadlock
$clientModule = $gameProcess.Modules | Where-Object { $_.ModuleName -eq 'client.dll' } | Select-Object -First 1
$moduleBase = $clientModule.BaseAddress.ToInt64()
$memoryHandle = [LiveMemory]::OpenProcess(0x410, $false, $gameProcess.Id)
if ($memoryHandle -eq [IntPtr]::Zero) { throw "Read-only OpenProcess failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
function Read-Memory([long]$address, [int]$length) {
 $buffer = New-Object byte[] $length
 $actual = [UIntPtr]::Zero
 if (-not [LiveMemory]::ReadProcessMemory($memoryHandle,[IntPtr]$address,$buffer,[UIntPtr]$length,[ref]$actual) -or $actual.ToUInt64() -ne $length) { return $null }
 return ,$buffer
}
function Read-Pointer([long]$address) {
 $buffer = Read-Memory $address 8
 if ($null -eq $buffer) { return 0L }
 return [BitConverter]::ToInt64($buffer,0)
}
function Get-RTTIName([long]$entity) {
 $vtable = Read-Pointer $entity
 if ($vtable -le 0) { return $null }
 if ($typeCache.ContainsKey($vtable)) { return $typeCache[$vtable] }
 $name = $null
 $locator = Read-Pointer ($vtable-8)
 if ($locator -gt 0) {
  $col = Read-Memory $locator 24
  if ($col -and [BitConverter]::ToUInt32($col,0) -eq 1) {
   $imageBase = $locator - [BitConverter]::ToUInt32($col,20)
   $typeAddress = $imageBase + [BitConverter]::ToUInt32($col,12)
   $nameBytes = Read-Memory ($typeAddress+16) 240
   if ($nameBytes) { $name = [Text.Encoding]::ASCII.GetString($nameBytes).Split([char]0)[0] }
  }
 }
 $typeCache[$vtable] = $name
 return $name
}
function Get-Matrix {
 $bytes = Read-Memory ($moduleBase+0x3C1D6E0) 64
 if ($null -eq $bytes) { throw 'Matrix read failed' }
 $values = @()
 for ($i=0;$i -lt 16;$i++) { $values += [double][BitConverter]::ToSingle($bytes,$i*4) }
 return ,$values
}
function Project-Point($matrix,$point) {
 $x=$point[0]; $y=$point[1]; $z=$point[2]
 $clipX=$matrix[0]*$x+$matrix[1]*$y+$matrix[2]*$z+$matrix[3]
 $clipY=$matrix[4]*$x+$matrix[5]*$y+$matrix[6]*$z+$matrix[7]
 $clipW=$matrix[12]*$x+$matrix[13]*$y+$matrix[14]*$z+$matrix[15]
 if ($clipW -lt 0.001) { return @{behind=$true;w=$clipW} }
 return @{behind=$false;w=$clipW;ndcX=$clipX/$clipW;ndcY=$clipY/$clipW;inView=([Math]::Abs($clipX/$clipW) -le 1 -and [Math]::Abs($clipY/$clipW) -le 1)}
}
$typeCache = @{}
try {
 $system = Read-Pointer ($moduleBase+0x3BE87C0)
 if ($system -le 0) { throw 'Null entity system' }
 $matrix = Get-Matrix
 $chunks = Read-Memory ($system+0x10) 512
 if ($null -eq $chunks) { throw 'Chunk table read failed' }
 $entities = @(); $allocatedChunks=0; $identityCount=0; $classCounts=@{}
 for ($chunkIndex=0;$chunkIndex -lt 64;$chunkIndex++) {
  $chunkAddress = [BitConverter]::ToInt64($chunks,$chunkIndex*8)
  if ($chunkAddress -le 0) { continue }
  $allocatedChunks++
  $identities = Read-Memory $chunkAddress (512*0x70)
  if ($null -eq $identities) { continue }
  for ($slot=0;$slot -lt 512;$slot++) {
   $offset=$slot*0x70
   $entity=[BitConverter]::ToInt64($identities,$offset)
   $handle=[BitConverter]::ToUInt32($identities,$offset+0x10)
   $index=$chunkIndex*512+$slot
   if ($entity -le 0 -or ($handle -band 0x7FFF) -ne $index -or $index -gt 0x7FFE) { continue }
   $identityCount++
   $className=Get-RTTIName $entity
   if ($className) { if (-not $classCounts.ContainsKey($className)) { $classCounts[$className]=0 }; $classCounts[$className]++ }
   if ($className -notmatch 'CitadelPlayerPawn|CitadelPlayerController|CitadelNPC|C_NPC_Trooper|CItemXP') { continue }
   $baseData=Read-Memory ($entity+0x330) 0xC0
   if ($null -eq $baseData) { continue }
   $scene= [BitConverter]::ToInt64($baseData,0)
   $health=[BitConverter]::ToInt32($baseData,0x24)
   $maxHealth=[BitConverter]::ToInt32($baseData,0x20)
   $position=$null; $projection=$null; $dormant=$null
   if ($scene -gt 0) {
    $sceneData=Read-Memory ($scene+0xC8) 0x3C
    if ($sceneData) {
     $position=@([double][BitConverter]::ToSingle($sceneData,0),[double][BitConverter]::ToSingle($sceneData,4),[double][BitConverter]::ToSingle($sceneData,8))
     $projection=Project-Point $matrix $position
     $dormant=$sceneData[0x3B] -ne 0
    }
   }
   $pawnIndex=$null; $localController=$null
   if ($className -match 'PlayerController') {
    $controllerData=Read-Memory ($entity+0x6BC) 0xD5
    if ($controllerData) {
     $pawnHandle=[BitConverter]::ToUInt32($controllerData,0)
     if ($pawnHandle -ne [uint32]::MaxValue) { $pawnIndex=$pawnHandle -band 0x7FFF }
     $localController=$controllerData[0xD4] -ne 0
    }
   }
   $entities += [ordered]@{index=$index;class=$className;address=('0x{0:X}' -f $entity);pawnIndex=$pawnIndex;localController=$localController;health=$health;maxHealth=$maxHealth;lifeState=$baseData[0x2C];team=$baseData[0xBF];sceneNode=('0x{0:X}' -f $scene);dormant=$dormant;position=$position;projection=$projection}
  }
 }
 $report=[ordered]@{processId=$gameProcess.Id;clientBase=('0x{0:X}' -f $moduleBase);entitySystem=('0x{0:X}' -f $system);matrix=$matrix;allocatedChunks=$allocatedChunks;validIdentities=$identityCount;playersAndNPCs=$entities;classes=$classCounts}
 $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath 'C:\Users\varos\Desktop\dl\project\verification\focus-entities-live.json'
 [ordered]@{processId=$gameProcess.Id;entitySystem=$report.entitySystem;matrix=$matrix;allocatedChunks=$allocatedChunks;validIdentities=$identityCount;playersAndNPCs=$entities} | ConvertTo-Json -Depth 7
} finally { [void][LiveMemory]::CloseHandle($memoryHandle) }

