# Initialize submodules, build bundled dependencies, and build the project.

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RootDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = if ($env:CMSIS_DAP_BUILD_DIR) { $env:CMSIS_DAP_BUILD_DIR } else { Join-Path $RootDir "build" }
$BuildConfig = if ($env:CONFIGURATION) { $env:CONFIGURATION } else { "Release" }

. (Join-Path $RootDir "lib\find-msvc.ps1")
$Toolchain = Get-CmsisDapMsvcToolchain

Invoke-CmsisDapCommand git @("-C", $RootDir, "submodule", "update", "--init", "--recursive")

function Checkout-DependencyTag($Name, $Path) {
    $ModulesFile = Join-Path $RootDir ".gitmodules"
    $Tag = [string](Invoke-CmsisDapCommand git @(
        "-C", $RootDir, "config", "-f", $ModulesFile, "submodule.$Name.tag"))
    $Tag = $Tag.Trim()
    if (-not $Tag) {
        throw "Missing tag for submodule $Name in .gitmodules"
    }

    $SubmoduleDir = Join-Path $RootDir $Path
    git -C $SubmoduleDir rev-parse --verify --quiet "refs/tags/$Tag" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Invoke-CmsisDapCommand git @("-C", $SubmoduleDir, "fetch", "--tags", "--quiet")
    }
    Invoke-CmsisDapCommand git @("-C", $SubmoduleDir, "checkout", "--detach", $Tag)
}

Checkout-DependencyTag "lib/hidapi" "lib\hidapi"
Checkout-DependencyTag "lib/libusb" "lib\libusb"
& (Join-Path $RootDir "lib\build-dependencies.ps1") `
    -Generator $Toolchain.Generator -MSBuildPath $Toolchain.MSBuild `
    -InstallPath $Toolchain.InstallPath -Configuration $BuildConfig

Invoke-CmsisDapCommand cmake @(
    "-S", $RootDir, "-B", $BuildDir, "-G", $Toolchain.Generator, "-A", "x64",
    "-DCMAKE_GENERATOR_INSTANCE=$($Toolchain.InstallPath)")
Invoke-CmsisDapCommand cmake @("--build", $BuildDir, "--config", $BuildConfig, "--parallel")
