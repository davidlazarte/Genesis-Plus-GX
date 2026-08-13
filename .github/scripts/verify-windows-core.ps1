param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath,

    [Parameter(Mandatory = $true)]
    [ValidateSet(0, 1)]
    [int]$SoundProbe,

    [ValidateSet(0, 1)]
    [int]$AytherExtensions = 1,

    [ValidateSet(0, 1)]
    [int]$LegacyProfile = 0,

    [string]$Compiler = 'clang',
    [string]$ArtifactDirectory = 'ayther-artifacts'
)

$ErrorActionPreference = 'Stop'

$dll = (Resolve-Path $DllPath).Path
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
$artifactRoot = (Resolve-Path $ArtifactDirectory).Path

$imports = (& llvm-readobj --coff-imports $dll | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw 'llvm-readobj failed while reading PE imports.'
}
$imports | Set-Content -Encoding utf8 (Join-Path $artifactRoot 'imports.txt')
if ($imports -match '(?i)ucrtbase\.dll') {
    throw 'The Windows target imported UCRT instead of MSVCRT.'
}
if ($imports -notmatch '(?i)msvcrt\.dll') {
    throw 'The Windows target did not import msvcrt.dll.'
}

$symbols = (& llvm-nm --defined-only $dll | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw 'llvm-nm failed while reading defined symbols.'
}
$symbols | Set-Content -Encoding utf8 (Join-Path $artifactRoot 'symbols.txt')

$exports = (& llvm-readobj --coff-exports $dll | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw 'llvm-readobj failed while reading PE exports.'
}
$exports | Set-Content -Encoding utf8 (Join-Path $artifactRoot 'exports.txt')

$requiredExports = @('retro_init', 'retro_run')
$probeExports = @(
    'audio_probe_poll',
    'audio_probe_set_callback',
    'audio_probe_get_transport_stats',
    'audio_probe_reset_transport_stats',
    'audio_probe_get_context',
    'audio_probe_set_channel_gain',
    'audio_probe_get_channel_gain'
)
if ($AytherExtensions -eq 1) {
    $requiredExports += 'ayther_get_interface'
    # Segunda puerta de entrada, deliberada. La recomposicion multicapa se
    # resuelve por simbolo directo y no por la interfaz: el frontend la carga
    # con GetProcAddress (ayther_session.cpp) y su oraculo tambien. Al agregarla
    # nadie actualizo este verificador, y como el job que lo corre ya estaba
    # rojo por otra causa, la regla siguio diciendo "solo ayther_get_interface"
    # sin que nadie lo notara.
    $requiredExports += 'ayther_recompose_multilayer'
}
if ($AytherExtensions -eq 1 -and $SoundProbe -eq 1) {
    $requiredExports += $probeExports
}

foreach ($symbol in $requiredExports) {
    if ($exports -notmatch "(?m)^\s*Name: $([regex]::Escape($symbol))\s*`$") {
        throw "Missing PE export: $symbol"
    }
}
# La superficie AYTHER exportada es CERRADA: exactamente las dos de arriba y
# ninguna mas. Que sean dos y no una no debilita el chequeo — lo que evita es
# que un simbolo nuevo se escape sin decision explicita, que es para lo que
# existe. Agregar una tercera exige tocar esta lista a proposito.
if ($AytherExtensions -eq 1 -and
    $exports -match '(?m)^\s*Name: ayther_(?!(get_interface|recompose_multilayer)\s*$)') {
    throw 'An unexpected additional AYTHER entry point was exported.'
}
if ($AytherExtensions -eq 0 -and
    ($symbols -match '(?m)\bayther_' -or $symbols -match '(?m)\baudio_probe_')) {
    throw 'AYTHER implementation symbols or buffers leaked from the extensions-off build.'
}
if (($AytherExtensions -eq 0 -or $SoundProbe -eq 0) -and
    $exports -match '(?m)^\s*Name: audio_probe_') {
    throw 'audio_probe symbols leaked from the feature-off build.'
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$loaderSource = Join-Path $repositoryRoot 'tests\ci\load_core.c'
$loader = Join-Path $artifactRoot 'load_core.exe'
& $Compiler -O2 -Wall -Wextra -o $loader $loaderSource
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to build the x64 dynamic-load verifier.'
}

& $loader $dll @requiredExports
if ($LASTEXITCODE -ne 0) {
    throw 'The x64 DLL load/export verification failed.'
}

$abiSource = Join-Path $repositoryRoot 'tests\ci\verify_ayther_api.c'
$abiVerifier = Join-Path $artifactRoot 'verify_ayther_api.exe'
$coreInclude = Join-Path $repositoryRoot 'core'
& $Compiler -O2 -Wall -Wextra -I $coreInclude -o $abiVerifier $abiSource
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to build the AYTHER ABI verifier.'
}

if ($AytherExtensions -eq 1) {
    $abiArguments = @($dll, '--require')
    if ($LegacyProfile -eq 1) {
        $abiArguments += '--legacy'
    }
    & $abiVerifier @abiArguments
} else {
    & $abiVerifier $dll
}
if ($LASTEXITCODE -ne 0) {
    throw 'The AYTHER ABI negotiation/compatibility verification failed.'
}

$mockSource = Join-Path $repositoryRoot 'tests\ci\mock_stock_core.c'
$mockCore = Join-Path $artifactRoot 'mock_stock_core.dll'
& $Compiler -shared -o $mockCore $mockSource
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to build the stock-core discovery fixture.'
}

& $abiVerifier $mockCore
if ($LASTEXITCODE -ne 0) {
    throw 'A stock/pre-ABI core was not handled safely.'
}
