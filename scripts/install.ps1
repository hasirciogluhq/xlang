# Installs `xl`, the xlang tool, on Windows (amd64), and then the compiler and stdlib with `xl lc install`.
#
#   irm https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.ps1 | iex
#
# Environment:
#   XL_VERSION        release of xl to install (default: the newest stable, else the newest pre-release)
#   XL_INSTALL_DIR    where xl.exe is placed (default: %LOCALAPPDATA%\xlang\bin; added to the user PATH)
#   XL_NO_TOOLCHAIN   set to 1 to skip `xl lc install`
#
# The archive `xl_<version>_windows_amd64.zip` is checked against the release's checksums.txt and the new binary is run once before
# it replaces an existing one (xlang-docs/compiler/install.md).
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repo = 'hasirciogluhq/xlang'
$dir = if ($env:XL_INSTALL_DIR) { $env:XL_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA 'xlang\bin' }

# Windows on arm64 has no xl release yet (xlang-docs known-limitations); x64 programs run there under emulation.
$arch = 'amd64'

$tag = $env:XL_VERSION
if (-not $tag) {
    Write-Host '==> Looking up the newest release'
    $headers = @{ Accept = 'application/vnd.github+json' }
    try {
        # `releases/latest` never returns a pre-release and answers 404 while there is none.
        $tag = (Invoke-RestMethod -Headers $headers "https://api.github.com/repos/$repo/releases/latest").tag_name
    } catch {
        $tag = (Invoke-RestMethod -Headers $headers "https://api.github.com/repos/$repo/releases?per_page=20" | Where-Object { -not $_.draft } | Select-Object -First 1).tag_name
    }
    if (-not $tag) { throw "could not find a release of $repo" }
} elseif (-not $tag.StartsWith('v')) {
    $tag = "v$tag"
}
$version = $tag.Substring(1)

$asset = "xl_${version}_windows_${arch}.zip"
$base = "https://github.com/$repo/releases/download/$tag"
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("xl-install-" + [System.Guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    Write-Host "==> Downloading $asset"
    Invoke-WebRequest "$base/$asset" -OutFile (Join-Path $tmp $asset)
    Invoke-WebRequest "$base/checksums.txt" -OutFile (Join-Path $tmp 'checksums.txt')

    $want = Get-Content (Join-Path $tmp 'checksums.txt') |
        ForEach-Object { $f = $_ -split '\s+'; if ($f.Count -ge 2 -and ($f[1] -eq $asset -or $f[1] -eq "*$asset")) { $f[0].ToLower() } } |
        Select-Object -First 1
    if (-not $want) { throw "checksums.txt has no entry for $asset; refusing to install an unverified binary" }
    $have = (Get-FileHash -Algorithm SHA256 (Join-Path $tmp $asset)).Hash.ToLower()
    if ($have -ne $want) { throw "checksum mismatch for $asset (expected $want, got $have)" }

    Expand-Archive (Join-Path $tmp $asset) -DestinationPath (Join-Path $tmp 'xl')
    $fresh = Join-Path $tmp 'xl\xl.exe'
    if (-not (Test-Path $fresh)) { throw 'the archive has no xl.exe' }
    & $fresh version | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'the downloaded xl does not start on this machine' }

    Write-Host "==> Installing to $dir"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $target = Join-Path $dir 'xl.exe'
    # A running xl.exe cannot be overwritten but can be renamed (xl self-update removes xl.old.exe later).
    if (Test-Path $target) { Move-Item -Force $target (Join-Path $dir 'xl.old.exe') }
    Move-Item $fresh $target
    Write-Host "installed xl $version -> $target"
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}

$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not ($userPath -split ';' | Where-Object { $_ -eq $dir })) {
    [Environment]::SetEnvironmentVariable('Path', "$dir;$userPath", 'User')
    $env:Path = "$dir;$env:Path"
    Write-Host "added $dir to the user PATH (new terminals see it)"
}

if ($env:XL_NO_TOOLCHAIN -ne '1') {
    Write-Host '==> Installing the compiler and stdlib (xl lc install)'
    & (Join-Path $dir 'xl.exe') lc install
    if ($LASTEXITCODE -ne 0) { throw 'xl is installed, but `xl lc install` failed; run it again later' }
}
Write-Host 'update later with: xl self-update and xl lc update'
