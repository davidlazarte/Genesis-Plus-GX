param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$Sha256,

    # #45.A: cual de las dos CRT. El fork publica con msvcrt, pero el fallo que
    # #38 dejo acotado -- STATUS_STACK_BUFFER_OVERRUN, o sea __fastfail-- solo
    # aparece con ucrt, asi que hace falta poder pedir la otra.
    [ValidateSet('msvcrt', 'ucrt')]
    [string]$Crt = 'msvcrt'
)

$ErrorActionPreference = 'Stop'

if (-not $env:GITHUB_PATH) {
    throw 'GITHUB_PATH is not available; this script is intended for GitHub Actions.'
}

$archive = Join-Path $env:RUNNER_TEMP "llvm-mingw-$Crt.zip"
$url = "https://github.com/mstorsjo/llvm-mingw/releases/download/$Version/llvm-mingw-$Version-$Crt-x86_64.zip"
Invoke-WebRequest -Uri $url -OutFile $archive

$actualHash = (Get-FileHash -Algorithm SHA256 -Path $archive).Hash.ToLowerInvariant()
if ($actualHash -ne $Sha256.ToLowerInvariant()) {
    throw "llvm-mingw checksum mismatch: $actualHash"
}

$destination = Join-Path $env:RUNNER_TEMP "llvm-mingw-$Crt"
Expand-Archive -Path $archive -DestinationPath $destination
$toolchainBin = Join-Path $destination "llvm-mingw-$Version-$Crt-x86_64\bin"
$mingwMake = Join-Path $toolchainBin 'mingw32-make.exe'
$make = Join-Path $toolchainBin 'make.exe'
if (-not (Test-Path $mingwMake)) {
    throw "The pinned llvm-mingw archive does not contain $mingwMake"
}
Copy-Item -Path $mingwMake -Destination $make
$toolchainBin | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
