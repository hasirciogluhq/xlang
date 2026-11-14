# Installs `xl`, the xlang tool, on Windows (amd64), and then the compiler with `xl lc install` and the stdlib with `xl std install`.
#
#   irm https://github.com/hasirciogluhq/xlang/releases/download/<tag>/install.ps1 | iex
#
# Every release, stable or pre-release, carries this script as an asset. Whichever release it comes from, it installs the newest xl
# unless XL_VERSION names another one.
#
# Environment:
#   XL_VERSION        release of xl to install: latest (the newest stable, else the newest pre-release; default), latest-stable,
#                     latest-rc (the newest pre-release) or a tag (v1.0.0, 1.0.0-rc.1)
#   XL_INSTALL_DIR    where xl.exe is placed (default: %LOCALAPPDATA%\xlang\bin; added to the user PATH)
#   XL_NO_TOOLCHAIN   set to 1 to skip `xl lc install` and `xl std install`
#
# The archive `xl_<version>_windows_amd64.zip` is checked against the release's checksums.txt and the new binary is run once before
# it replaces an existing one (xlang-docs/compiler/install.md).
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repo = 'hasirciogluhq/xlang'
$dir = if ($env:XL_INSTALL_DIR) { $env:XL_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA 'xlang\bin' }

# Windows on arm64 has no xl release yet (xlang-docs known-limitations); x64 programs run there under emulation.
$arch = 'amd64'

$request = if ($env:XL_VERSION) { $env:XL_VERSION } else { 'latest' }
if ($request -in 'latest', 'latest-stable', 'latest-rc') {
    Write-Host "==> Looking up the $request release"
    # Published releases sorted newest first: the API does not order them by date (rc.9 comes before rc.10). Drafts are not
    # visible without a token (https://docs.github.com/en/rest/releases/releases#list-releases).
    $list = @(Invoke-RestMethod -Headers @{ Accept = 'application/vnd.github+json' } "https://api.github.com/repos/$repo/releases?per_page=100" |
        Where-Object { -not $_.draft } | Sort-Object { [datetime]$_.published_at } -Descending)
    $stable = $list | Where-Object { -not $_.prerelease } | Select-Object -First 1
    $tag = switch ($request) {
        'latest' { if ($stable) { $stable.tag_name } else { ($list | Select-Object -First 1).tag_name } }
        'latest-stable' { $stable.tag_name }
        'latest-rc' { ($list | Where-Object { $_.prerelease } | Select-Object -First 1).tag_name }
    }
    if (-not $tag) { throw "$repo has no $request release yet" }
} else {
    $tag = if ($request.StartsWith('v')) { $request } else { "v$request" }
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
    Write-Host '==> Installing the compiler (xl lc install)'
    & (Join-Path $dir 'xl.exe') lc install
    if ($LASTEXITCODE -ne 0) { throw 'xl is installed, but `xl lc install` failed; run it again later' }
    Write-Host '==> Installing the stdlib (xl std install)'
    & (Join-Path $dir 'xl.exe') std install
    if ($LASTEXITCODE -ne 0) { throw 'xl is installed, but `xl std install` failed; run it again later' }
}
Write-Host 'update later with: xl self-update, xl lc update and xl std update'
