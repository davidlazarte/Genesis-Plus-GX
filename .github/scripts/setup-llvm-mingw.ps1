param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$Sha256
)

$ErrorActionPreference = 'Stop'

if (-not $env:GITHUB_PATH) {
    throw 'GITHUB_PATH is not available; this script is intended for GitHub Actions.'
}

$archive = Join-Path $env:RUNNER_TEMP 'llvm-mingw-msvcrt.zip'
$url = "https://github.com/mstorsjo/llvm-mingw/releases/download/$Version/llvm-mingw-$Version-msvcrt-x86_64.zip"
Invoke-WebRequest -Uri $url -OutFile $archive

$actualHash = (Get-FileHash -Algorithm SHA256 -Path $archive).Hash.ToLowerInvariant()
if ($actualHash -ne $Sha256.ToLowerInvariant()) {
    throw "llvm-mingw checksum mismatch: $actualHash"
}

$destination = Join-Path $env:RUNNER_TEMP 'llvm-mingw-msvcrt'
Expand-Archive -Path $archive -DestinationPath $destination
$toolchainBin = Join-Path $destination "llvm-mingw-$Version-msvcrt-x86_64\bin"
$mingwMake = Join-Path $toolchainBin 'mingw32-make.exe'
$make = Join-Path $toolchainBin 'make.exe'
if (-not (Test-Path $mingwMake)) {
    throw "The pinned llvm-mingw archive does not contain $mingwMake"
}
Copy-Item -Path $mingwMake -Destination $make
$toolchainBin | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
