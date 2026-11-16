param(
    [string]$Generator,
    [string]$MSBuildPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$LibRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Prefix = Join-Path $LibRoot "windows-x86_64"
$BuildRoot = Join-Path $LibRoot ".build\windows-x86_64"
$HidapiSource = Join-Path $LibRoot "hidapi"
$LibusbSource = Join-Path $LibRoot "libusb"

. (Join-Path $LibRoot "find-msvc.ps1")
if (-not $Generator -or -not $MSBuildPath) {
    $Toolchain = Get-CmsisDapMsvcToolchain
    if (-not $Generator) { $Generator = $Toolchain.Generator }
    if (-not $MSBuildPath) { $MSBuildPath = $Toolchain.MSBuild }
}

foreach ($path in @($HidapiSource, $LibusbSource)) {
    if (-not (Test-Path (Join-Path $path ".git"))) {
        throw "Missing dependency submodule at $path. Run: git submodule update --init --recursive"
    }
}

New-Item -ItemType Directory -Force $Prefix, $BuildRoot | Out-Null

$MSBuildPath (Join-Path $LibusbSource "msvc\libusb_static.vcxproj") `
    /m /p:Configuration=Release-MT /p:Platform=x64

$LibusbLibrary = Get-ChildItem -Path (Join-Path $LibusbSource "build") `
    -Filter "libusb-1.0.lib" -Recurse |
    Where-Object { $_.FullName -match "\\Release-MT\\" } |
    Select-Object -First 1
if (-not $LibusbLibrary) {
    throw "The libusb static library was not produced by MSBuild."
}

$IncludeDirectory = Join-Path $Prefix "include\libusb-1.0"
$LibraryDirectory = Join-Path $Prefix "lib"
New-Item -ItemType Directory -Force $IncludeDirectory, $LibraryDirectory | Out-Null
Copy-Item (Join-Path $LibusbSource "libusb\libusb.h") $IncludeDirectory -Force
Copy-Item $LibusbLibrary.FullName (Join-Path $LibraryDirectory "libusb-1.0.lib") -Force

cmake -S $HidapiSource -B (Join-Path $BuildRoot "hidapi") `
    -G $Generator -A x64 `
    -DBUILD_SHARED_LIBS=OFF `
    -DHIDAPI_WITH_TESTS=OFF `
    -DHIDAPI_BUILD_HIDTEST=OFF `
    -DHIDAPI_BUILD_PP_DATA_DUMP=OFF `
    -DCMAKE_INSTALL_PREFIX=$Prefix `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build (Join-Path $BuildRoot "hidapi") --config Release --parallel
cmake --install (Join-Path $BuildRoot "hidapi") --config Release

Write-Host "Static dependencies installed in $Prefix"
