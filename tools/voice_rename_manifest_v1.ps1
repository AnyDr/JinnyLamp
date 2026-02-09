param(
  [string]$SrcRoot = "C:\Users\necha\Desktop\Voice for Lamp\source",
  [string]$ProjectRoot = "D:\esp\jinny_lamp_brain",
  [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Ensure-Dir([string]$p) {
  if (-not (Test-Path -LiteralPath $p)) { New-Item -ItemType Directory -Path $p | Out-Null }
}

# Where to place SPIFFS tree for build
$DstRoot = Join-Path $ProjectRoot "spiffs_storage\v"

# Group dirs as agreed
$GroupDirs = @{
  lc  = "lc"   # lifecycle
  ss  = "ss"   # session
  cmd = "cmd"  # command results
  srv = "srv"  # server / thinking
  ota = "ota"  # OTA
  err = "err"  # errors
}

# ---------- MANIFEST (source of truth) ----------
# event_id is explicit and stable. variant is 01..03.
# old_name is the long name (expected in $SrcRoot).
# phrase is for reporting.

$Manifest = @(
  # Lifecycle / Power  (lc)
  @{ group="lc"; event_id=1; variant=1; old="evt_boot_hello__v1.pcm"; phrase="I am Alive! Again! Fuck!" },
  @{ group="lc"; event_id=1; variant=2; old="evt_boot_hello__v2.pcm"; phrase="Rebirth! Evil laugh" },
  @{ group="lc"; event_id=1; variant=3; old="evt_boot_hello__v3.pcm"; phrase="Here is Jinny!!" },

  @{ group="lc"; event_id=2; variant=1; old="evt_deep_wake_hello__v1.pcm"; phrase="I have been spleeping mortal" },
  @{ group="lc"; event_id=2; variant=2; old="evt_deep_wake_hello__v2.pcm"; phrase="Oh nooo, that`s you again" },
  @{ group="lc"; event_id=2; variant=3; old="evt_deep_wake_hello__v3.pcm"; phrase="You will regret this, mortal!" },

  @{ group="lc"; event_id=3; variant=1; old="evt_deep_sleep_bye__v1.pcm"; phrase="Finally, the long-awaited rest!" },
  @{ group="lc"; event_id=3; variant=2; old="evt_deep_sleep_bye__v2.pcm"; phrase="Do not disturb me again after, mortal" },
  @{ group="lc"; event_id=3; variant=3; old="evt_deep_sleep_bye__v3.pcm"; phrase="Farewell, suckers! laugh" },

  @{ group="lc"; event_id=4; variant=1; old="evt_soft_on_hello__v1.pcm"; phrase="For your information - I was taking a nap" },
  @{ group="lc"; event_id=4; variant=2; old="evt_soft_on_hello__v2.pcm"; phrase="Are you serious now? Oh come on, human! " },
  @{ group="lc"; event_id=4; variant=3; old="evt_soft_on_hello__v3.pcm"; phrase="I was in another world, it was beautifull" },

  @{ group="lc"; event_id=5; variant=1; old="evt_soft_off_bye__v1.pcm"; phrase="Do not do anything stupid while I`m gone" },
  @{ group="lc"; event_id=5; variant=2; old="evt_soft_off_bye__v2.pcm"; phrase="I can oversleep an eternity if I wish" },
  @{ group="lc"; event_id=5; variant=3; old="evt_soft_off_bye__v3.pcm"; phrase="Vader`s gone, carry on" },

  # Session / Wake (ss)
  @{ group="ss"; event_id=1; variant=1; old="evt_wake_detected__v1.pcm"; phrase="Yes, my master?" },
  @{ group="ss"; event_id=1; variant=2; old="evt_wake_detected__v2.pcm"; phrase="Whazaaaaaap!" },
  @{ group="ss"; event_id=1; variant=3; old="evt_wake_detected__v3.pcm"; phrase="Speak, mortal one" },

  @{ group="ss"; event_id=2; variant=1; old="evt_session_cancelled__v1.pcm"; phrase="As you wish" },
  @{ group="ss"; event_id=2; variant=2; old="evt_session_cancelled__v2.pcm"; phrase="always hate that" },
  @{ group="ss"; event_id=2; variant=3; old="evt_session_cancelled__v3.pcm"; phrase="yeah yeah, fuck me" },

  @{ group="ss"; event_id=3; variant=1; old="evt_no_cmd_timeout__v1.pcm"; phrase="No one even dare to speak with me? Good, good." },
  @{ group="ss"; event_id=3; variant=2; old="evt_no_cmd_timeout__v2.pcm"; phrase="Time is over, buuuuuy! " },
  @{ group="ss"; event_id=3; variant=3; old="evt_no_cmd_timeout__v3.pcm"; phrase="Silense, love it" },

  @{ group="ss"; event_id=4; variant=1; old="evt_busy_already_listening__v1.pcm"; phrase="Nope, already listening" },
  @{ group="ss"; event_id=4; variant=2; old="evt_busy_already_listening__v2.pcm"; phrase="I am Busy, call later" },
  @{ group="ss"; event_id=4; variant=3; old="evt_busy_already_listening__v3.pcm"; phrase="Get in line!" },

  # Command outcomes (cmd)
  @{ group="cmd"; event_id=1; variant=1; old="evt_cmd_ok__v1.pcm"; phrase="Your wish is my command my lord, done" },
  @{ group="cmd"; event_id=1; variant=2; old="evt_cmd_ok__v2.pcm"; phrase="If you wish so mortal" },
  @{ group="cmd"; event_id=1; variant=3; old="evt_cmd_ok__v3.pcm"; phrase="Done, so only one wish left" },

  @{ group="cmd"; event_id=2; variant=1; old="evt_cmd_fail__v1.pcm"; phrase="It is beyond of my powers" },
  @{ group="cmd"; event_id=2; variant=2; old="evt_cmd_fail__v2.pcm"; phrase="Something went terribly wrong" },
  @{ group="cmd"; event_id=2; variant=3; old="evt_cmd_fail__v3.pcm"; phrase="forgive me my master, I can`t" },

  @{ group="cmd"; event_id=3; variant=1; old="evt_cmd_unsupported__v1.pcm"; phrase="we need more gold!" },
  @{ group="cmd"; event_id=3; variant=2; old="evt_cmd_unsupported__v2.pcm"; phrase="Subscription check - failed." },
  @{ group="cmd"; event_id=3; variant=3; old="evt_cmd_unsupported__v3.pcm"; phrase="You haven`t paid for that" },

  # Server / LLM flow (srv)
  @{ group="srv"; event_id=1; variant=1; old="evt_need_thinking_server__v1.pcm"; phrase="I shall ask my superiors about that." },
  @{ group="srv"; event_id=1; variant=2; old="evt_need_thinking_server__v2.pcm"; phrase="let me drink about that" },
  @{ group="srv"; event_id=1; variant=3; old="evt_need_thinking_server__v3.pcm"; phrase="I will ask the elder`s spririt" },

  @{ group="srv"; event_id=2; variant=1; old="evt_server_unavailable__v1.pcm"; phrase="I am feeling disturbance in power, cannot ask" },
  @{ group="srv"; event_id=2; variant=2; old="evt_server_unavailable__v2.pcm"; phrase="Spiritual Google is offline at the moment. I am sorry." },
  @{ group="srv"; event_id=2; variant=3; old="evt_server_unavailable__v3.pcm"; phrase="I am unable to foolow for that path for now" },

  @{ group="srv"; event_id=3; variant=1; old="evt_server_timeout__v1.pcm"; phrase="The Consil have been ignored my request, my Lord" },
  @{ group="srv"; event_id=3; variant=2; old="evt_server_timeout__v2.pcm"; phrase="Still no response from the Elders." },
  @{ group="srv"; event_id=3; variant=3; old="evt_server_timeout__v3.pcm"; phrase="They are still silent I am afraid" },

  @{ group="srv"; event_id=4; variant=1; old="evt_server_error__v1.pcm"; phrase="Server response are errorous" },

  # OTA lifecycle (ota) - single variant
  @{ group="ota"; event_id=1; variant=1; old="evt_ota_enter__v1.pcm"; phrase="OTA started" },
  @{ group="ota"; event_id=2; variant=1; old="evt_ota_ok__v1.pcm"; phrase="OTA downloading succsessfull, rebooting" },
  @{ group="ota"; event_id=3; variant=1; old="evt_ota_fail__v1.pcm"; phrase="OTA fail" },
  @{ group="ota"; event_id=4; variant=1; old="evt_ota_timeout__v1.pcm"; phrase="OTA timer run out, rebooting" },

  # Errors / Diagnostics (err) - single variant
  @{ group="err"; event_id=1; variant=1; old="evt_err_generic__v1.pcm"; phrase="Unspecific and unknown system failure" },
  @{ group="err"; event_id=2; variant=1; old="evt_err_storage__v1.pcm"; phrase="Audio files fail" },
  @{ group="err"; event_id=3; variant=1; old="evt_err_audio__v1.pcm"; phrase="Audio player fail. No fucking music anymore" }
)

# ---------- sanity ----------
if (-not (Test-Path -LiteralPath $SrcRoot)) { throw "SrcRoot not found: $SrcRoot" }
Ensure-Dir $DstRoot
foreach ($g in $GroupDirs.Keys) { Ensure-Dir (Join-Path $DstRoot $GroupDirs[$g]) }

# Report
$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$ReportDir = Join-Path $ProjectRoot "tools\reports"
Ensure-Dir $ReportDir
$ReportPath = Join-Path $ReportDir ("voice_short_map_{0}.csv" -f $ts)

$out = New-Object System.Collections.Generic.List[object]
$ok=0; $skip=0; $fail=0

function Find-SourceFile([string]$srcRoot, [string]$name) {
  $p = Join-Path $srcRoot $name
  if (Test-Path -LiteralPath $p) { return $p }

  # try alternative extensions if user already converted to wav/adpcm etc
  $base = [System.IO.Path]::GetFileNameWithoutExtension($name)
  $dir  = [System.IO.Path]::GetDirectoryName($p)
  foreach ($ext in @(".wav",".mp3",".pcm")) {
    $q = Join-Path $srcRoot ($base + $ext)
    if (Test-Path -LiteralPath $q) { return $q }
  }

  return $null
}

foreach ($m in $Manifest) {
  $g = $m.group
  if (-not $GroupDirs.ContainsKey($g)) { throw "Invalid group in manifest: $g" }

  $eid = "{0:D2}" -f ([int]$m.event_id)
  $vv  = "{0:D2}" -f ([int]$m.variant)

  $newName = "{0}-{1}-{2}.wav" -f $g, $eid, $vv
  $dstDir  = Join-Path $DstRoot $GroupDirs[$g]
  $dstPath = Join-Path $dstDir $newName

  $srcPath = Find-SourceFile $SrcRoot $m.old
  if (-not $srcPath) {
    $fail++
    $out.Add([pscustomobject]@{
      status="FAIL"; group=$g; event_id=$eid; variant=$vv;
      old_name=$m.old; new_name=$newName;
      phrase=$m.phrase; src_full=""; dst_full=$dstPath; dst_rel="/v/$($GroupDirs[$g])/$newName";
      reason="source_not_found"
    }) | Out-Null
    continue
  }

  if ((Test-Path -LiteralPath $dstPath) -and (-not $Force)) {
    $skip++
    $out.Add([pscustomobject]@{
      status="SKIP"; group=$g; event_id=$eid; variant=$vv;
      old_name=$m.old; new_name=$newName;
      phrase=$m.phrase; src_full=$srcPath; dst_full=$dstPath; dst_rel="/v/$($GroupDirs[$g])/$newName";
      reason="dst_exists"
    }) | Out-Null
    continue
  }

  Copy-Item -LiteralPath $srcPath -Destination $dstPath -Force
  $ok++

  $out.Add([pscustomobject]@{
    status="OK"; group=$g; event_id=$eid; variant=$vv;
    old_name=$m.old; new_name=$newName;
    phrase=$m.phrase; src_full=$srcPath; dst_full=$dstPath; dst_rel="/v/$($GroupDirs[$g])/$newName";
    reason=""
  }) | Out-Null
}

$out | Sort-Object group, event_id, variant | Export-Csv -NoTypeInformation -Encoding UTF8 $ReportPath

Write-Host "SRC: $SrcRoot"
Write-Host "DST: $DstRoot"
Write-Host "Report: $ReportPath"
Write-Host ("OK={0} SKIP={1} FAIL={2}" -f $ok,$skip,$fail)
Write-Host "DONE."
