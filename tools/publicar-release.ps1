# ============================================================
#  espToy — publicar-release.ps1
#  Compila el firmware, calcula el MD5, genera version.json
#  y publica una release en GitHub con ambos archivos.
#
#  Uso:
#    cd <raiz-del-proyecto>
#    .\tools\publicar-release.ps1
#
#  Prerrequisitos:
#    - PlatformIO CLI (pio) en el PATH
#    - GitHub CLI (gh) en el PATH y autenticado
#    - PowerShell 5.1 (compatible con Windows por defecto)
# ============================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---- Paso 1: Leer FW_VERSION de include/config.h ---------------
Write-Host ""
Write-Host "=== espToy publicar-release ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "[1/5] Leyendo version desde include/config.h..."

$configPath = "include\config.h"
if (-not (Test-Path $configPath)) {
    Write-Host "ERROR: no se encontro $configPath — ejecuta el script desde la raiz del proyecto." -ForegroundColor Red
    exit 1
}

$configContenido = Get-Content $configPath -Raw
$matchVer = [regex]::Match($configContenido, '#define\s+FW_VERSION\s+"([^"]+)"')
if (-not $matchVer.Success) {
    Write-Host "ERROR: no se encontro la linea #define FW_VERSION en $configPath." -ForegroundColor Red
    exit 1
}

$version = $matchVer.Groups[1].Value
Write-Host "    Version encontrada: $version" -ForegroundColor Green

# ---- Paso 2: Compilar con PlatformIO ---------------------------
Write-Host ""
Write-Host "[2/5] Compilando con PlatformIO (pio run)..."
pio run
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: la compilacion fallo (pio run retorno $LASTEXITCODE)." -ForegroundColor Red
    exit 1
}
Write-Host "    Compilacion exitosa." -ForegroundColor Green

# ---- Paso 3: Calcular MD5 del firmware -------------------------
Write-Host ""
Write-Host "[3/5] Calculando MD5 del firmware..."

$firmwarePath = ".pio\build\seeed_xiao_esp32s3\firmware.bin"
if (-not (Test-Path $firmwarePath)) {
    Write-Host "ERROR: no se encontro el firmware en $firmwarePath." -ForegroundColor Red
    exit 1
}

$hashObj = Get-FileHash -Path $firmwarePath -Algorithm MD5
# Get-FileHash devuelve el hash en mayusculas; lo convertimos a minusculas
$md5 = $hashObj.Hash.ToLower()
Write-Host "    MD5: $md5" -ForegroundColor Green

# ---- Paso 4: Generar version.json ------------------------------
Write-Host ""
Write-Host "[4/5] Generando version.json..."

$versionJsonPath = [System.IO.Path]::GetTempPath() + "esptoy_version.json"
$jsonContenido = '{"version":"' + $version + '","md5":"' + $md5 + '"}'

# Escribir sin BOM (UTF-8 puro) para que el parser del ESP32 lo lea bien
[System.IO.File]::WriteAllText($versionJsonPath, $jsonContenido, [System.Text.UTF8Encoding]::new($false))

Write-Host "    version.json: $jsonContenido" -ForegroundColor Green
Write-Host "    Guardado en: $versionJsonPath"

# ---- Paso 5: Publicar release en GitHub ------------------------
Write-Host ""
Write-Host "[5/5] Publicando release en GitHub..."

$tag   = "v$version"
$titulo = "espToy $tag"
$notas  = "Release automatica generada por publicar-release.ps1"

# Verificar si la release ya existe para no pisarla
$releaseExiste = $false
$checkOutput = gh release view $tag 2>&1
if ($LASTEXITCODE -eq 0) {
    $releaseExiste = $true
}

if ($releaseExiste) {
    Write-Host ""
    Write-Host "ERROR: la release '$tag' ya existe en GitHub." -ForegroundColor Red
    Write-Host "       Si queres reemplazarla, primero borrala con:" -ForegroundColor Yellow
    Write-Host "       gh release delete $tag --yes" -ForegroundColor Yellow
    exit 1
}

gh release create $tag $firmwarePath $versionJsonPath --title $titulo --notes $notas
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: gh release create fallo (codigo $LASTEXITCODE)." -ForegroundColor Red
    exit 1
}

# Limpiar el archivo temporal
Remove-Item $versionJsonPath -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "=== Release $tag publicada exitosamente ===" -ForegroundColor Green
Write-Host "    Los dispositivos la detectaran en el proximo chequeo OTA (hasta 24 h)."
Write-Host ""
