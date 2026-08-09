param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath,

    [Parameter(Mandatory = $true)]
    [ValidateSet(0, 1)]
    [int]$SoundProbe,

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
    'audio_probe_get_context',
    'audio_probe_set_channel_gain',
    'audio_probe_get_channel_gain'
)
if ($SoundProbe -eq 1) {
    $requiredExports += $probeExports
}

foreach ($symbol in $requiredExports) {
    if ($exports -notmatch "(?m)^\s*Name: $([regex]::Escape($symbol))\s*`$") {
        throw "Missing PE export: $symbol"
    }
}
if ($SoundProbe -eq 0 -and $exports -match '(?m)^\s*Name: audio_probe_') {
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
