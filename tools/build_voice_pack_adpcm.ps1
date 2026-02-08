<#
build_voice_pack_adpcm.ps1

Flat source folder (mp3/wav) -> IMA ADPCM 4-bit WAV (.wav)
Output into: D:\esp\jinny_lamp_brain\spiffs_storage\voice\<group>\

Safety:
- does not overwrite existing .wav unless -Force
- writes CSV report
#>

param(
    [string]$SrcRoot = "C:\Users\necha\Desktop\Voice for Lamp\source",
    [string]$DstRoot = "D:\esp\jinny_lamp_brain\spiffs_storage\voice",
    [switch]$UseLoudnorm,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-Exe([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "Executable not found in PATH: $name" }
}

function Ensure-Dir([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        New-Item -ItemType Directory -Path $path | Out-Null
    }
}

function Get-GroupFromName([string]$baseName) {
    if ($baseName -notmatch '^evt_[a-z0-9_]+__v[1-3]$') { return $null }

    if ($baseName -match '^evt_ota_') { return "ota" }
    if ($baseName -match '^evt_err_') { return "error" }
    if ($baseName -match '^evt_server_' -or $baseName -match '^evt_need_') { return "server" }
    if ($baseName -match '^evt_cmd_') { return "cmd" }

    if ($baseName -match '^evt_wake_' -or
        $baseName -match '^evt_session_' -or
        $baseName -match '^evt_no_cmd_' -or
        $baseName -match '^evt_busy_') { return "session" }

    if ($baseName -match '^evt_boot_' -or
        $baseName -match '^evt_deep_' -or
        $baseName -match '^evt_soft_') { return "lifecycle" }

    return $null
}

function Convert-One([string]$inFile, [string]$outFile, [bool]$useLoudnorm) {
    # input -> IMA ADPCM WAV (4-bit), mono 16k
    $args = @("-y", "-hide_banner", "-loglevel", "error", "-i", $inFile)

    if ($useLoudnorm) {
        $args += @("-af", "loudnorm")
    }

    # Force mono 16k, encode as adpcm_ima_wav into .wav container
    $args += @("-ac", "1", "-ar", "16000", "-c:a", "adpcm_ima_wav", $outFile)

    & ffmpeg @args
}

# ---- checks ----
Require-Exe "ffmpeg"

if (-not (Test-Path -LiteralPath $SrcRoot)) { throw "SrcRoot does not exist: $SrcRoot" }
if (-not (Test-Path -LiteralPath $DstRoot)) { throw "DstRoot does not exist: $DstRoot" }

@("lifecycle","session","cmd","server","ota","error") | ForEach-Object {
    Ensure-Dir (Join-Path $DstRoot $_)
}

$inputs = Get-ChildItem -LiteralPath $SrcRoot -File |
    Where-Object { $_.Extension -in @(".mp3",".wav",".MP3",".WAV") }

if ($inputs.Count -eq 0) {
    throw "No .mp3/.wav files found in SrcRoot: $SrcRoot"
}

Write-Host "SRC: $SrcRoot"
Write-Host "DST: $DstRoot"
Write-Host "UseLoudnorm: $UseLoudnorm"
Write-Host "Force: $Force"
Write-Host ("Found inputs: {0}" -f $inputs.Count)
Write-Host ""

$ok = 0
$skipped = 0
$failed = 0
$details = New-Object System.Collections.Generic.List[object]

foreach ($f in $inputs) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
    $group = Get-GroupFromName $base

    if (-not $group) {
        $failed++
        $details.Add([pscustomobject]@{
            Status="FAIL"; Group=""; In=$f.FullName; Out=""; Reason="Bad name or unknown prefix (expected evt_<name>__v1..v3)"
        })
        continue
    }

    $dstDir = Join-Path $DstRoot $group
    $out = Join-Path $dstDir ($base + ".wav")   # ADPCM-in-WAV

    if ((Test-Path -LiteralPath $out) -and (-not $Force)) {
        $skipped++
        $details.Add([pscustomobject]@{
            Status="SKIP"; Group=$group; In=$f.FullName; Out=$out; Reason="Exists"
        })
        continue
    }

    try {
        Convert-One -inFile $f.FullName -outFile $out -useLoudnorm $UseLoudnorm.IsPresent
        $ok++
        $details.Add([pscustomobject]@{
            Status="OK"; Group=$group; In=$f.FullName; Out=$out; Reason=""
        })
    } catch {
        $failed++
        $details.Add([pscustomobject]@{
            Status="FAIL"; Group=$group; In=$f.FullName; Out=$out; Reason=$_.Exception.Message
        })
    }
}

Write-Host ""
Write-Host ("OK={0} SKIP={1} FAIL={2}" -f $ok, $skipped, $failed)

$wavs = Get-ChildItem -LiteralPath $DstRoot -Recurse -File -Filter "*.wav" -ErrorAction SilentlyContinue
$totalBytes = ($wavs | Measure-Object -Property Length -Sum).Sum
$totalMB = [Math]::Round($totalBytes / 1MB, 2)

Write-Host ""
Write-Host ("Total ADPCM WAV files: {0}" -f $wavs.Count)
Write-Host ("Total ADPCM WAV size:  {0} MB" -f $totalMB)

if ($wavs.Count -gt 0) {
    Write-Host ""
    Write-Host "Top-10 biggest ADPCM WAV:"
    $wavs | Sort-Object Length -Descending | Select-Object -First 10 `
        @{n="SizeKB";e={[Math]::Round($_.Length/1KB,1)}}, FullName | Format-Table -AutoSize
}

$repDir = ".\tools\reports"
if (-not (Test-Path -LiteralPath $repDir)) { New-Item -ItemType Directory -Path $repDir | Out-Null }
$reportPath = Join-Path $repDir ("voice_pack_adpcm_report_{0}.csv" -f (Get-Date -Format "yyyyMMdd_HHmmss"))

$details | Export-Csv -NoTypeInformation -Encoding UTF8 $reportPath
Write-Host ""
Write-Host ("Report saved: {0}" -f $reportPath)
