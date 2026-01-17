# ===================== НАСТРОЙКИ (правь только это) =====================
$root = "D:\esp\jinny_lamp_brain"          # корень проекта
$dst  = "C:\Users\necha\Desktop\Lamp"         # куда сохранить

# Смешивай как хочешь: папки, маски, отдельные файлы (все пути ОТНОСИТЕЛЬНО $root)
$pick = @(
  "\CMakeLists.txt",
  "\sdkconfig",
  "docs\*.md",
  "main\*.c",
  "main\*.h",
  "partitions\*.csv",
  "main\*.txt"
)

# Какие расширения считать "текстовыми" (для папок/масок)
$ext = @(".c",".h",".cpp",".hpp",".ino",".py",".js",".ts",".json",".yml",".yaml",".md",".txt",".cmake")

# Дополнительно: имена файлов БЕЗ расширения (часто полезно в ESP-IDF)
$namesNoExt = @("CMakeLists.txt","Kconfig","Kconfig.projbuild","idf_component.yml")
# =======================================================================

$rootPath = (Resolve-Path -LiteralPath $root).Path.TrimEnd('\')
New-Item -ItemType Directory -Force -Path $dst | Out-Null

function Add-OneFileAsTxt([string]$fullPath) {
  $full = (Resolve-Path -LiteralPath $fullPath).Path
  if ($full -notlike "$rootPath\*") { throw "File outside root: $full" }

  $rel = $full.Substring($rootPath.Length + 1)
  $targetDir = Join-Path $dst (Split-Path $rel -Parent)
  New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

  $target = Join-Path $dst ($rel + ".txt")
  Get-Content -LiteralPath $full -Raw -Encoding utf8 | Set-Content -LiteralPath $target -Encoding utf8
  Write-Host "OK: $rel"
}

foreach ($p in $pick) {
  $abs = Join-Path $rootPath $p

  # 1) Если это существующая папка
  if (Test-Path -LiteralPath $abs -PathType Container) {
    Get-ChildItem -LiteralPath $abs -Recurse -File |
      Where-Object {
        ($ext -contains $_.Extension.ToLowerInvariant()) -or ($namesNoExt -contains $_.Name)
      } |
      ForEach-Object { Add-OneFileAsTxt $_.FullName }
    continue
  }

  # 2) Если это существующий файл (без wildcard)
  $hasWildcard = [System.Management.Automation.WildcardPattern]::ContainsWildcardCharacters($p)
  if (-not $hasWildcard -and (Test-Path -LiteralPath $abs -PathType Leaf)) {
    Add-OneFileAsTxt $abs
    continue
  }

  # 3) Иначе считаем что это маска (wildcard) и собираем совпадения
  if ($hasWildcard) {
    $items = Get-ChildItem -Path $abs -File -ErrorAction Stop |
      Where-Object {
        ($ext -contains $_.Extension.ToLowerInvariant()) -or ($namesNoExt -contains $_.Name)
      }
    foreach ($f in $items) { Add-OneFileAsTxt $f.FullName }
    continue
  }

  throw "Not found (and not a wildcard): $p"
}