# Prints a short path to this checkout, making it on first use: a junction C:\fw\<name>, named after the checkout's
# folder (at most its last 12 characters). MSVC fails in a deep worktree path, past Windows' 260-character limit
# (C1041 on a program database, C1083 on a nested include), so configure and build through the junction into a short
# build directory beside it, and keep git on the real path:
#
#     $src = scripts\short-path.ps1
#     scripts\msvc.ps1 cmake --preset msvc-x64-debug -S $src -B "$src-build"
#     scripts\msvc.ps1 cmake --build "$src-build" --target FenrirAnimationTests
#
# To remove the junction, delete the link itself, `(Get-Item C:\fw\<name>).Delete()`: deleting its contents deletes the
# checkout's files.

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$name = Split-Path $root -Leaf
if ($name.Length -gt 12) {
    $name = $name.Substring($name.Length - 12)
}
$base = Join-Path $env:SystemDrive 'fw'
New-Item -ItemType Directory -Force -Path $base | Out-Null
$link = Join-Path $base $name
if (Test-Path $link) {
    $target = (Get-Item $link).Target
    if ($target -ne $root) {
        Write-Error "short-path.ps1: $link already points at $target, not $root."
        exit 1
    }
}
else {
    New-Item -ItemType Junction -Path $link -Target $root | Out-Null
}
$link
