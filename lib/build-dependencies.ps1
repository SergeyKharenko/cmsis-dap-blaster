param(
    [string]$Generator,
    [string]$MSBuildPath,
    [string]$InstallPath,
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = $(if ($env:CONFIGURATION) { $env:CONFIGURATION } else { "Release" })
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$LibRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Prefix = Join-Path $LibRoot "windows-x86_64"
$BuildRoot = Join-Path $LibRoot ".build\windows-x86_64"
$HidapiSource = Join-Path $LibRoot "hidapi"
$LibusbSource = Join-Path $LibRoot "libusb"

. (Join-Path $LibRoot "find-msvc.ps1")
if (-not $Generator -or -not $MSBuildPath -or -not $InstallPath) {
    $Toolchain = Get-CmsisDapMsvcToolchain
    if (-not $Generator) { $Generator = $Toolchain.Generator }
    if (-not $MSBuildPath) { $MSBuildPath = $Toolchain.MSBuild }
    if (-not $InstallPath) { $InstallPath = $Toolchain.InstallPath }
}

foreach ($path in @($HidapiSource, $LibusbSource)) {
    if (-not (Test-Path (Join-Path $path ".git"))) {
        throw "Missing dependency submodule at $path. Run: git submodule update --init --recursive"
    }
}

New-Item -ItemType Directory -Force $Prefix, $BuildRoot | Out-Null

$LibusbConfiguration = if ($Configuration -eq "Debug") { "Debug-MT" } else { "Release-MT" }
$LibusbOutput = Join-Path $BuildRoot "libusb\$LibusbConfiguration"
# libusb 1.0.29 predates MSVC's C5287 enum warning and builds with /WX.
# Disable only that warning for this dependency, preserving the caller's flags.
$SavedClOptions = $env:_CL_
try {
    $env:_CL_ = "$SavedClOptions /wd5287"
    Invoke-CmsisDapCommand $MSBuildPath @(
        (Join-Path $LibusbSource "msvc\libusb_static.vcxproj"),
        "/m", "/p:Configuration=$LibusbConfiguration", "/p:Platform=x64",
        "/p:OutDir=$LibusbOutput/")
} finally {
    $env:_CL_ = $SavedClOptions
}

$LibusbLibrary = Join-Path $LibusbOutput "libusb-1.0.lib"
if (-not (Test-Path -LiteralPath $LibusbLibrary)) {
    throw "The libusb static library was not produced by MSBuild."
}

$IncludeDirectory = Join-Path $Prefix "include\libusb-1.0"
$LibraryDirectory = Join-Path $Prefix "lib"
New-Item -ItemType Directory -Force $IncludeDirectory, $LibraryDirectory | Out-Null
Copy-Item (Join-Path $LibusbSource "libusb\libusb.h") $IncludeDirectory -Force
Copy-Item $LibusbLibrary (Join-Path $LibraryDirectory "libusb-1.0.lib") -Force

Invoke-CmsisDapCommand cmake @(
    "-S", $HidapiSource, "-B", (Join-Path $BuildRoot "hidapi"),
    "-G", $Generator, "-A", "x64",
    "-DCMAKE_GENERATOR_INSTANCE=$InstallPath",
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_PROJECT_hidapi_INCLUDE=$(Join-Path $LibRoot 'hidapi-windows-static.cmake')",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DHIDAPI_WITH_TESTS=OFF",
    "-DHIDAPI_BUILD_HIDTEST=OFF",
    "-DHIDAPI_BUILD_PP_DATA_DUMP=OFF",
    "-DCMAKE_INSTALL_PREFIX=$Prefix",
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>')
Invoke-CmsisDapCommand cmake @(
    "--build", (Join-Path $BuildRoot "hidapi"), "--config", $Configuration, "--parallel")
Invoke-CmsisDapCommand cmake @(
    "--install", (Join-Path $BuildRoot "hidapi"), "--config", $Configuration)

Write-Host "Static dependencies installed in $Prefix"
