# ============================================================
#  espToy - publicar-release.ps1
#  Compila el firmware, calcula el MD5, genera version.json
#  y publica una release en GitHub con ambos archivos.
#
#  Uso:  .\tools\publicar-release.ps1   (desde la raiz del proyecto)
#
#  Prerrequisitos: pio y gh en el PATH, gh autenticado.
#  Nota: archivo en ASCII puro para evitar problemas de encoding
#  en PowerShell 5.1 (que lee .ps1 sin BOM como ANSI).
# ============================================================

$ErrorActionPreference = "Stop"

# ---- Paso 1: leer FW_VERSION de include/config.h ----------------
Write-Host ""
Write-Host "[1/5] Leyendo version de include/config.h..." -ForegroundColor Cyan
$configPath = "include\config.h"
if (-not (Test-Path $configPath)) {
    Write-Host "ERROR: no se encuentra $configPath. Ejecutar desde la raiz del proyecto." -ForegroundColor Red
    exit 1
}
$contenido = Get-Content $configPath -Raw
$m = [regex]::Match($contenido, '#define\s+FW_VERSION\s+"([^"]+)"')
if (-not $m.Success) {
    Write-Host "ERROR: no se encontro FW_VERSION en config.h" -ForegroundColor Red
    exit 1
}
$version = $m.Groups[1].Value
$tag = "v$version"
Write-Host "      Version: $version (tag $tag)"

# ---- Paso 2: compilar --------------------------------------------
Write-Host "[2/5] Compilando con pio run..." -ForegroundColor Cyan
pio run
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: fallo la compilacion." -ForegroundColor Red
    exit 1
}

# ---- Paso 3: MD5 del binario -------------------------------------
Write-Host "[3/5] Calculando MD5 del firmware.bin..." -ForegroundColor Cyan
$binPath = ".pio\build\seeed_xiao_esp32s3\firmware.bin"
if (-not (Test-Path $binPath)) {
    Write-Host "ERROR: no existe $binPath" -ForegroundColor Red
    exit 1
}
$md5 = (Get-FileHash $binPath -Algorithm MD5).Hash.ToLower()
Write-Host "      MD5: $md5"

# ---- Paso 4: generar version.json (UTF8 sin BOM) ------------------
Write-Host "[4/5] Generando version.json..." -ForegroundColor Cyan
$jsonPath = Join-Path $env:TEMP "version.json"
$json = '{"version":"' + $version + '","md5":"' + $md5 + '"}'
[System.IO.File]::WriteAllText($jsonPath, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "      $json"

# ---- Paso 5: crear la release en GitHub ---------------------------
Write-Host "[5/5] Publicando release $tag en GitHub..." -ForegroundColor Cyan
# cmd /c para que el stderr de gh no dispare el ErrorActionPreference=Stop de PS 5.1
cmd /c "gh release view $tag >nul 2>&1"
if ($LASTEXITCODE -eq 0) {
    Write-Host "ERROR: la release $tag ya existe. Subi la version en config.h y reintentEa." -ForegroundColor Red
    exit 1
}
gh release create $tag $binPath $jsonPath --title "espToy $tag" --notes "Release automatica de espToy $version"
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: fallo gh release create." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Listo: release $tag publicada con firmware.bin y version.json" -ForegroundColor Green
Write-Host "Los juguetes con version anterior la van a detectar en su proximo chequeo." -ForegroundColor Green
