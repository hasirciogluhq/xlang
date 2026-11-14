# Removes xlang from this Windows machine: every file that `xl`, `xl lc`, `xl std`, the install script or the VS Code extension put in a
# well-known place (Linux and macOS: scripts/uninstall.sh).
#
#   irm https://raw.githubusercontent.com/hasirciogluhq/xlang/development/scripts/uninstall.ps1 | iex
#
# Unlike the install scripts this one is not a release asset: it lives on the `development` branch and is used from there, so it
# always knows every layout up to the newest. It scans, prints a numbered list, and asks which entries to remove (`all`, numbers
# and ranges such as `1 3 5-7`, or nothing).
#
# Environment:
#   XL_UNINSTALL_YES    set to 1 to remove everything found without asking
#   XL_UNINSTALL_LIST   set to 1 to only print what was found
#
# What it looks for (xlang-docs/compiler/install.md "Uninstall"):
#   %LOCALAPPDATA%\xlang and $XL_INSTALL_DIR   xl.exe, the xl.old.exe kept by `xl self-update`, and their user PATH entries
#   xl.exe, xlc.exe on PATH                     only a file whose bytes carry the xlang marker (`hasirciogluhq` for xl,
#                                               `_xl_initAll` for xlc)
#   $XLANG_HOME                                 the compilers and stdlibs of `xl lc` and `xl std` (default %USERPROFILE%\.xlang)
#   %TEMP%\xl-install-*                         left by an interrupted install.ps1
#   editors                                     the `xlang.xlang` extension of VS Code, VS Code Insiders, VSCodium and Cursor
# A project's own `.xlang\cache` belongs to the project and is left alone (`xl pkg clean` empties it).
$ErrorActionPreference = 'Stop'

$items = [System.Collections.Generic.List[object]]::new()
function Add-Item($kind, $target, $label) {
    if ($items | Where-Object { $_.Kind -eq $kind -and $_.Target -eq $target }) { return }
    $items.Add([pscustomobject]@{ Kind = $kind; Target = $target; Label = $label })
}
function Get-Size($path) {
    $bytes = (Get-ChildItem -LiteralPath $path -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
    '{0:N1} MB' -f ($bytes / 1MB)
}
# Binary files are matched as Latin-1 text, which maps every byte to one character, so an ASCII marker is found as is.
function Test-Marker($file, $marker) {
    try { [System.IO.File]::ReadAllText($file, [System.Text.Encoding]::GetEncoding(28591)).Contains($marker) } catch { $false }
}

Write-Host '==> Looking for xlang'

# The install directory of install.ps1 and its user PATH entry.
$installDirs = @($env:XL_INSTALL_DIR, (Join-Path $env:LOCALAPPDATA 'xlang\bin')) | Where-Object { $_ } | Select-Object -Unique
$appDir = Join-Path $env:LOCALAPPDATA 'xlang'
if (Test-Path $appDir) { Add-Item 'dir' $appDir "$appDir  ($(Get-Size $appDir); xl.exe)" }
if ($env:XL_INSTALL_DIR -and (Test-Path $env:XL_INSTALL_DIR)) {
    foreach ($name in 'xl.exe', 'xl.old.exe') {
        $f = Join-Path $env:XL_INSTALL_DIR $name
        if (Test-Path $f) { Add-Item 'file' $f $f }
    }
}
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
foreach ($d in $installDirs) {
    if ($userPath -split ';' | Where-Object { $_ -and $_.TrimEnd('\') -eq $d.TrimEnd('\') }) {
        Add-Item 'path' $d "user PATH entry $d"
    }
}

# Copies elsewhere on PATH.
foreach ($name in 'xl', 'xlc') {
    $marker = if ($name -eq 'xl') { 'hasirciogluhq' } else { '_xl_initAll' }
    foreach ($c in @(Get-Command "$name.exe" -All -CommandType Application -ErrorAction SilentlyContinue)) {
        $f = $c.Source
        if ($f.StartsWith($appDir, [StringComparison]::OrdinalIgnoreCase)) { continue }
        if (Test-Marker $f $marker) { Add-Item 'file' $f $f }
    }
}

# The toolchain of `xl lc` and `xl std` (src/lc/toolchain.xl).
foreach ($h in @($env:XLANG_HOME, (Join-Path $env:USERPROFILE '.xlang')) | Where-Object { $_ } | Select-Object -Unique) {
    if (-not (Test-Path $h)) { continue }
    $installed = (@('xlc', 'std') | ForEach-Object { Get-ChildItem -Directory -ErrorAction SilentlyContinue (Join-Path $h $_) | ForEach-Object { "$($_.Parent.Name)/$($_.Name)" } }) -join ' '
    Add-Item 'dir' $h "$h  ($(Get-Size $h); $(if ($installed) { $installed } else { 'no releases' }))"
}

Get-ChildItem -Directory -ErrorAction SilentlyContinue (Join-Path ([System.IO.Path]::GetTempPath()) 'xl-install-*') |
    ForEach-Object { Add-Item 'dir' $_.FullName "$($_.FullName)  (left by an interrupted install)" }

# Editor extensions: <editor dir>\extensions\xlang.xlang-<version>. The editor's CLI removes it from its own index as well.
foreach ($e in @(@('.vscode', 'code'), @('.vscode-insiders', 'code-insiders'), @('.vscode-oss', 'codium'), @('.cursor', 'cursor'))) {
    Get-ChildItem -Directory -ErrorAction SilentlyContinue (Join-Path $env:USERPROFILE "$($e[0])\extensions\xlang.xlang-*") |
        ForEach-Object { Add-Item 'ext' "$($e[1])|$($_.FullName)" "$($_.FullName)  (editor extension)" }
}

if ($items.Count -eq 0) { Write-Host 'nothing of xlang was found'; return }

Write-Host ''
for ($i = 0; $i -lt $items.Count; $i++) { Write-Host ('  {0,2}  {1}' -f ($i + 1), $items[$i].Label) }
Write-Host ''
if ($env:XL_UNINSTALL_LIST -eq '1') { return }

$picked = @()
if ($env:XL_UNINSTALL_YES -eq '1') {
    $picked = 0..($items.Count - 1)
} else {
    $answer = Read-Host 'Remove which? [all, numbers like 1 3 5-7, empty for none]'
    foreach ($word in ($answer -split '[\s,]+' | Where-Object { $_ })) {
        if ($word -in 'all', 'a') { $picked = 0..($items.Count - 1) }
        elseif ($word -match '^(\d+)-(\d+)$') { $picked += ([int]$Matches[1] - 1)..([int]$Matches[2] - 1) }
        elseif ($word -match '^\d+$') { $picked += [int]$word - 1 }
        else { throw "'$word' is not a number or a range" }
    }
    if ($picked.Count -eq 0) { Write-Host 'nothing removed'; return }
}

$failed = $false
foreach ($i in ($picked | Select-Object -Unique)) {
    if ($i -lt 0 -or $i -ge $items.Count) { Write-Warning "there is no entry $($i + 1)"; $failed = $true; continue }
    $it = $items[$i]
    try {
        switch ($it.Kind) {
            # A running xl.exe cannot be deleted (sharing violation); the warning below names it.
            { $_ -in 'file', 'dir' } { Remove-Item -LiteralPath $it.Target -Recurse -Force }
            'path' {
                $current = [Environment]::GetEnvironmentVariable('Path', 'User')
                $kept = ($current -split ';' | Where-Object { $_ -and $_.TrimEnd('\') -ne $it.Target.TrimEnd('\') }) -join ';'
                [Environment]::SetEnvironmentVariable('Path', $kept, 'User')
            }
            'ext' {
                $cli, $dir = $it.Target -split '\|', 2
                if (Get-Command $cli -ErrorAction SilentlyContinue) { & $cli --uninstall-extension xlang.xlang *> $null }
                if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
            }
        }
        Write-Host "removed $($it.Label)"
    } catch {
        Write-Warning "could not remove $($it.Label): $($_.Exception.Message)"
        $failed = $true
    }
}

if ($failed) { throw 'some entries were not removed (see the warnings above)' }
Write-Host 'open a new terminal so it picks up the changed PATH'
