$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$OutDir = Join-Path $RootDir "web\dist"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$empp = Get-Command em++ -ErrorAction SilentlyContinue
if ($empp) {
    Write-Host "[build] Using local em++: $($empp.Source)"
    & em++ -O3 -std=gnu++17 `
        -I "$RootDir\web\shims" `
        -I "$RootDir\include" `
        -I "$RootDir\src" `
        "$RootDir\web\web_main.cpp" `
        "$RootDir\src\beat.cpp" `
        "$RootDir\src\garden_game.cpp" `
        "$RootDir\src\game_params.cpp" `
        -o "$OutDir\crystal_garden.js" `
        -s MODULARIZE=1 `
        -s EXPORT_NAME="createCrystalGarden" `
        -s ENVIRONMENT=web `
        -s INITIAL_MEMORY=67108864 `
        -s ALLOW_MEMORY_GROWTH=1 `
        -s "EXPORTED_FUNCTIONS=['_emu_init','_emu_step','_emu_render','_emu_get_rgba_ptr','_emu_pointer_down','_emu_pointer_move','_emu_pointer_up','_emu_trigger_pet','_emu_send_command','_emu_save','_emu_get_wave','_emu_get_run','_emu_get_party_count']" `
        -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPU8']"

    Copy-Item "$RootDir\web\index.html" "$OutDir\index.html" -Force
    New-Item -ItemType File -Force -Path "$OutDir\.nojekyll" | Out-Null
} else {
    Write-Host "[build] Local em++ not found; building via Docker (emscripten/emsdk:3.1.68)..."
    docker run --rm -v "${RootDir}:/src" -w /src emscripten/emsdk:3.1.68 bash /src/web/build.sh
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Get-ChildItem "$OutDir" | Format-Table Name, @{N='KB';E={[math]::Round($_.Length/1KB,1)}}
