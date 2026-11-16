function Invoke-CmsisDapCommand($Command, $CommandArguments) {
    & $Command @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE."
    }
}

function Get-CmsisDapMsvcToolchain {
    $vswhereCommand = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($vswhereCommand) {
        $vswhere = $vswhereCommand.Source
    } else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    }
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe was not found. Install Visual Studio with the C++ desktop workload."
    }

    $installPath = [string](Invoke-CmsisDapCommand $vswhere @(
        "-latest", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"))
    $installVersion = [string](Invoke-CmsisDapCommand $vswhere @(
        "-latest", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationVersion"))
    if ([string]::IsNullOrWhiteSpace($installPath) -or [string]::IsNullOrWhiteSpace($installVersion)) {
        throw "No Visual Studio installation with the x64 C++ toolset was found."
    }
    $installPath = $installPath.Trim()
    $installVersion = $installVersion.Trim()

    $major = [int]($installVersion.Split('.')[0])
    $generator = $null
    foreach ($line in (Invoke-CmsisDapCommand cmake @("--help"))) {
        if ($line -match "Visual Studio ([0-9]+) ([0-9]{4})" -and [int]$Matches[1] -eq $major) {
            $generator = "Visual Studio $($Matches[1]) $($Matches[2])"
            break
        }
    }
    if (-not $generator) {
        throw "CMake does not support a generator for Visual Studio major version $major."
    }

    $msbuildCandidates = @(
        (Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"),
        (Join-Path $installPath "MSBuild\$major.0\Bin\MSBuild.exe")
    )
    $msbuild = $msbuildCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $msbuild) {
        throw "MSBuild was not found under $installPath."
    }

    [pscustomobject]@{
        Generator = $generator
        MSBuild = $msbuild
        InstallPath = $installPath
    }
}
