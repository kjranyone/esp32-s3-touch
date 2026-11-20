$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$OutDir = Join-Path $RootDir "web\dist"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$empp = Get-Command em++ -ErrorAction SilentlyContinue
if ($empp) {
    Write-Host "[build] Using local em++ via bash web/build.sh..."
    bash (Join-Path $RootDir "web\build.sh")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host "[build] Local em++ not found; building via Docker (emscripten/emsdk:3.1.68)..."
    docker run --rm -v "${RootDir}:/src" -w /src emscripten/emsdk:3.1.68 bash /src/web/build.sh
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Get-ChildItem "$OutDir\apps" | Format-Table Name, @{N='KB';E={[math]::Round($_.Length/1KB,1)}}
