# Runs a command with the Visual Studio x64 build environment loaded (vcvars64.bat), which the MSVC presets need, from
# PowerShell:
#
#     scripts\msvc.ps1 cmake --preset msvc-x64-debug -DFENRIR_BUILD_EXAMPLES=OFF
#     scripts\msvc.ps1 cmake --build out/build/msvc-x64-debug --target FenrirScriptingTests
#     scripts\msvc.ps1 ctest --test-dir out/build/msvc-x64-debug -R "Scripting|RefCompile"
#
# The command runs in PowerShell itself, so its arguments arrive as written (a cmd wrapper would re-read a | in them).
# The session's environment is put back afterwards. From cmd, use scripts\msvc.cmd. The clang presets need neither.

if ($args.Count -eq 0) {
    Write-Error "usage: scripts\msvc.ps1 <command> [arguments...]"
    exit 2
}

$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$vswhere = Join-Path $installer 'vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Write-Error "msvc.ps1: vswhere.exe is not at $vswhere`: install Visual Studio 2022 with the C++ workload."
    exit 1
}
$vcvars = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find 'VC\Auxiliary\Build\vcvars64.bat' | Select-Object -First 1
if (-not $vcvars) {
    Write-Error "msvc.ps1: no Visual Studio with the x64 C++ tools found."
    exit 1
}

$saved = Get-ChildItem env: | ForEach-Object { [pscustomobject]@{ Name = $_.Name; Value = $_.Value } }
try {
    # vcvars64.bat runs vswhere by name itself; its environment is read back from cmd and set here.
    $env:PATH = "$installer;$env:PATH"
    # $env:ComSpec, not `cmd`: another cmd (MSYS2's) can come first on PATH. Starting with `call`, so cmd keeps the
    # quotes around a path with spaces.
    $variables = & $env:ComSpec /c "call `"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "msvc.ps1: $vcvars failed."
        exit 1
    }
    foreach ($line in $variables) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
        }
    }

    $command = $args[0]
    $arguments = @($args | Select-Object -Skip 1)
    & $command @arguments
    $code = $LASTEXITCODE
}
finally {
    Get-ChildItem env: | Where-Object { $_.Name -notin $saved.Name } | ForEach-Object { Remove-Item "env:$($_.Name)" }
    foreach ($variable in $saved) {
        Set-Item -Path "env:$($variable.Name)" -Value $variable.Value
    }
}
exit $code
