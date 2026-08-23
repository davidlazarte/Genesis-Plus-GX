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

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

# #32: el closure de la superficie AYTHER lo decide tests/ci/check_exports.sh
# contra tests/ci/allowed_exports.txt — la MISMA lista y el MISMO verificador
# que usa el job de Linux. Antes la regla estaba escrita dos veces, una aca y
# otra en el workflow, y habian divergido: este permitia
# ayther_recompose_multilayer y el otro no. Ninguno de los dos podia notarlo
# porque cada uno solo corre en su plataforma.
$exportNames = Join-Path $artifactRoot 'export_names.txt'
([regex]::Matches($exports, '(?m)^\s*Name:\s*(\S+)\s*$') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique) |
    Set-Content -Encoding ascii $exportNames

$closureScript = (Join-Path $repositoryRoot 'tests\ci\check_exports.sh') -replace '\\', '/'
$exportNamesPosix = $exportNames -replace '\\', '/'
# Git Bash explicitamente: `bash` a secas puede resolver al de WSL, que no sabe
# leer rutas C:/..., y el sintoma es un "No such file or directory" con la ruta
# sin barras que no se parece en nada a la causa. Las barras invertidas hay que
# cambiarlas igual: bash se las come como escapes.
# Se deriva de la ubicacion de git.exe en vez de adivinar Program Files: git
# esta garantizado -el repo se acaba de clonar con el- y $env:ProgramFiles
# apunta al de 32 bits segun como haya arrancado pwsh, asi que buscarlo ahi
# falla en maquinas donde el bash correcto si existe.
$bash = $null
$gitCommand = Get-Command git -ErrorAction SilentlyContinue
if ($gitCommand) {
    $gitRoot = Split-Path (Split-Path $gitCommand.Source -Parent) -Parent
    foreach ($candidate in @(
        (Join-Path $gitRoot 'bin\bash.exe'),
        (Join-Path $gitRoot 'usr\bin\bash.exe'))) {
        if (Test-Path $candidate) { $bash = $candidate; break }
    }
}
foreach ($candidate in @(
    "$env:ProgramW6432\Git\bin\bash.exe",
    "$env:ProgramFiles\Git\bin\bash.exe",
    "${env:ProgramFiles(x86)}\Git\bin\bash.exe")) {
    if (-not $bash -and $candidate -and (Test-Path $candidate)) { $bash = $candidate }
}
if (-not $bash) { $bash = 'bash' }

$closure = (& $bash $closureScript $exportNamesPosix $AytherExtensions $LegacyProfile $SoundProbe 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "The AYTHER export closure failed:`n$closure"
}
Write-Host $closure.Trim()

# Lo que tiene que RESOLVER dinamicamente sale de la misma lista.
$requiredExports = @('retro_init', 'retro_run')
if ($closure -match '(?m)^export closure OK \([^)]*\):\s*(.*)$') {
    $requiredExports += ($Matches[1].Trim() -split '\s+' | Where-Object { $_ })
}

foreach ($symbol in $requiredExports) {
    if ($exports -notmatch "(?m)^\s*Name: $([regex]::Escape($symbol))\s*`$") {
        throw "Missing PE export: $symbol"
    }
}
if ($AytherExtensions -eq 0 -and
    ($symbols -match '(?m)\bayther_' -or $symbols -match '(?m)\baudio_probe_')) {
    throw 'AYTHER implementation symbols or buffers leaked from the extensions-off build.'
}

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

# #33: un cliente compilado contra ABI 1.0, que no conoce 1.1 ni 1.2. Congelar
# offsets contra el header actual detecta un reordenamiento, pero no contesta la
# pregunta que importa: ¿un frontend ya compilado sigue funcionando?
if ($AytherExtensions -eq 1) {
    $compatSource = Join-Path $repositoryRoot 'tests\ci\abi_compat_1_0.c'
    $compatBinary = Join-Path $artifactRoot 'abi_compat_1_0.exe'
    & $Compiler -O2 -Wall -Wextra -o $compatBinary $compatSource
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to build the ABI 1.0 compatibility client.'
    }
    & $compatBinary $dll
    if ($LASTEXITCODE -ne 0) {
        throw 'A 1.0 client can no longer use this core.'
    }
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
