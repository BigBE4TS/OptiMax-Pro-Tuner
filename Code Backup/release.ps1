<#
.SYNOPSIS
    Cuts an OptiMax release: verifies the build, publishes the binary, then the
    checksum, then the version marker - in that order, which is load-bearing.

.DESCRIPTION
    The client update flow (OptiMaxUpdater.h) reads three separate locations:

        1. version.txt              (raw.githubusercontent.com, main branch)
        2. releases/latest/download (the binary)
        3. *.exe.sha256             (raw.githubusercontent.com, main branch)

    version.txt is the TRIGGER: the app only downloads once it sees a higher
    version there. So version.txt must be the LAST thing published. Push it
    first and every user who checks in the gap downloads a binary that does not
    match the published checksum, gets "does not match the published checksum.
    Stopping", and cannot update until the release lands.

    This script enforces that order and refuses to proceed if anything is
    inconsistent.

    It also writes every published file WITHOUT a UTF-8 BOM. This is not
    cosmetic. The client sanitizer at OptiMaxApp.h:632 strips \n, \r and spaces
    but not a BOM, so a BOM-prefixed "4.3.0" reaches std::stoi as "<BOM>4",
    throws, and is silently parsed as major version 0 - which is never newer
    than the running build, so the update prompt never appears and nothing is
    logged. PowerShell 5.1's Out-File and > both emit a BOM by default, which
    makes this an extremely easy mistake to make by hand.

.PARAMETER RepoDir
    Local clone of the GitHub repo holding version.txt and the .sha256.

.PARAMETER ExePath
    The release binary. Defaults to the newest OptiMax_Profiler.exe under out\.

.PARAMETER Check
    Audit the CURRENTLY PUBLISHED files and exit. Changes nothing. Run this
    first if updates are misbehaving in the wild.

.PARAMETER SkipStaleCheck
    Allow a binary older than the sources. Use this for a build that has already
    been submitted to Microsoft for analysis: it will be days old by the time
    approval comes back, and it is the exact file that must ship. Rebuilding to
    make a timestamp look right would produce a different binary from the one
    that was approved, with a different hash.

.PARAMETER DryRun
    Do everything except commit, push, and create the release.

.EXAMPLE
    .\release.ps1 -Check
    .\release.ps1 -RepoDir C:\src\OptiMax-Pro-Tuner -DryRun
    .\release.ps1 -RepoDir C:\src\OptiMax-Pro-Tuner
#>

[CmdletBinding()]
param(
    [string]$SourceDir = $PSScriptRoot,
    [string]$RepoDir,
    [string]$ExePath,
    [switch]$Check,
    [switch]$DryRun,
    [switch]$SkipStaleCheck,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$RAW_BASE   = 'https://raw.githubusercontent.com/BigBE4TS/OptiMax-Pro-Tuner/main'
$VERSION_URL = "$RAW_BASE/version.txt"
$HASH_URL    = "$RAW_BASE/OptiMax_Profiler.exe.sha256"
$EXE_NAME    = 'OptiMax_Profiler.exe'

function Write-Step  { param($m) Write-Host "`n==> $m" -ForegroundColor Cyan }
function Write-Ok    { param($m) Write-Host "    [ok]   $m" -ForegroundColor Green }
function Write-Warn2 { param($m) Write-Host "    [warn] $m" -ForegroundColor Yellow }
function Write-Bad   { param($m) Write-Host "    [FAIL] $m" -ForegroundColor Red }

# Writes text with no BOM and a single trailing newline. See the BOM note above -
# this function is the entire reason the script exists rather than three git commands.
function Write-PlainUtf8 {
    param([string]$Path, [string]$Text)
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, ($Text.Trim() + "`n"), $enc)
}

# Downloads a raw URL with cache busting, and reports whether the payload
# carries a BOM. Returns a PSCustomObject so callers can inspect both.
function Get-PublishedFile {
    param([string]$Url)
    $bust = "?t=" + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    try {
        $resp = Invoke-WebRequest -Uri ($Url + $bust) -UseBasicParsing -TimeoutSec 20
    } catch {
        return [PSCustomObject]@{ Ok = $false; Text = ''; HasBom = $false; Error = $_.Exception.Message }
    }

    $bytes = $resp.Content
    if ($bytes -is [string]) { $bytes = [System.Text.Encoding]::UTF8.GetBytes($bytes) }

    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $start  = 0
    if ($hasBom) { $start = 3 }
    $text = [System.Text.Encoding]::UTF8.GetString($bytes, $start, $bytes.Length - $start).Trim()

    return [PSCustomObject]@{ Ok = $true; Text = $text; HasBom = $hasBom; Error = '' }
}

function ConvertTo-VersionParts {
    param([string]$V)
    $parts = @(0, 0, 0)
    $split = $V -split '\.'
    for ($i = 0; $i -lt 3; $i++) {
        if ($i -lt $split.Count) {
            $n = 0
            if ([int]::TryParse($split[$i], [ref]$n)) { $parts[$i] = $n }
        }
    }
    return $parts
}

function Test-VersionNewer {
    param([string]$Candidate, [string]$Baseline)
    $c = ConvertTo-VersionParts $Candidate
    $b = ConvertTo-VersionParts $Baseline
    for ($i = 0; $i -lt 3; $i++) {
        if ($c[$i] -gt $b[$i]) { return $true }
        if ($c[$i] -lt $b[$i]) { return $false }
    }
    return $false
}

# ---------------------------------------------------------------------------
# 1. Version, read from the single source of truth in the source tree
# ---------------------------------------------------------------------------

Write-Step 'Reading version from OptiMaxApp.h'

$appHeader = Join-Path $SourceDir 'OptiMaxApp.h'
if (-not (Test-Path $appHeader)) { throw "Cannot find OptiMaxApp.h at $appHeader" }

$verMatch = Select-String -Path $appHeader -Pattern 'CURRENT_VERSION\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"'
if (-not $verMatch) { throw 'Could not find CURRENT_VERSION in OptiMaxApp.h' }

$version = $verMatch.Matches[0].Groups[1].Value
Write-Ok "CURRENT_VERSION = $version  (OptiMaxApp.h:$($verMatch.LineNumber))"

# ---------------------------------------------------------------------------
# 2. Audit what is published right now
# ---------------------------------------------------------------------------

Write-Step 'Auditing currently published files'

$pubVer  = Get-PublishedFile $VERSION_URL
$pubHash = Get-PublishedFile $HASH_URL
$auditFailed = $false

if (-not $pubVer.Ok) {
    Write-Bad "version.txt unreachable: $($pubVer.Error)"
    $auditFailed = $true
} else {
    Write-Ok "version.txt = '$($pubVer.Text)'"
    if ($pubVer.HasBom) {
        Write-Bad 'version.txt has a UTF-8 BOM. The client parses this as version 0.x.x'
        Write-Bad 'and will NEVER offer an update. This alone breaks the updater silently.'
        $auditFailed = $true
    }
    if ($pubVer.Text -notmatch '^v?[0-9]+\.[0-9]+\.[0-9]+$') {
        Write-Bad "version.txt is not a bare semantic version - client sanitizer may reject it."
        $auditFailed = $true
    }
    if ($pubVer.Text.Length -gt 15) {
        Write-Bad 'version.txt exceeds 15 chars; OptiMaxApp.h:638 discards it outright.'
        $auditFailed = $true
    }
}

if (-not $pubHash.Ok) {
    Write-Bad "checksum file unreachable: $($pubHash.Error)"
    $auditFailed = $true
} else {
    $cleanHash = ($pubHash.Text -replace '[^0-9a-fA-F]', '').ToLower()
    if ($cleanHash.Length -eq 64) {
        Write-Ok "published sha256 = $cleanHash"
    } else {
        Write-Bad "checksum file does not contain 64 hex chars (got $($cleanHash.Length))."
        Write-Bad 'The client refuses to install anything when this is malformed.'
        $auditFailed = $true
    }
    if ($pubHash.HasBom) { Write-Warn2 'checksum file has a BOM (client strips non-hex, so tolerated).' }
}

if ($Check) {
    Write-Step 'Check complete'
    if ($auditFailed) {
        Write-Bad 'Published state is BROKEN - see above.'
        exit 1
    }
    Write-Ok 'Published state looks consistent.'
    exit 0
}

# ---------------------------------------------------------------------------
# 3. Preflight the release
# ---------------------------------------------------------------------------

Write-Step 'Preflight'

if ($pubVer.Ok -and -not (Test-VersionNewer $version $pubVer.Text)) {
    if (-not $Force) {
        throw "CURRENT_VERSION ($version) is not newer than published ($($pubVer.Text)). Bump OptiMaxApp.h first, or pass -Force."
    }
    Write-Warn2 "Version is not newer than published, continuing because -Force was given."
}
Write-Ok "$version is newer than published $($pubVer.Text)"

if (-not $ExePath) {
    $outDir = Join-Path $SourceDir 'out'
    if (Test-Path $outDir) {
        $cand = Get-ChildItem -Path $outDir -Filter $EXE_NAME -Recurse -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($cand) { $ExePath = $cand.FullName }
    }
}
if (-not $ExePath -or -not (Test-Path $ExePath)) {
    throw "Could not locate $EXE_NAME. Build a Release configuration first, or pass -ExePath."
}

$exe = Get-Item $ExePath
Write-Ok "binary: $($exe.FullName)"
Write-Ok "        $([math]::Round($exe.Length/1MB,2)) MB, built $($exe.LastWriteTime)"

# A binary older than the sources means you are about to ship a stale build.
$newestSource = Get-ChildItem -Path $SourceDir -Include *.h, *.cpp -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -notmatch '\\(out|\.vs|imgui)\\' } |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1

if ($SkipStaleCheck) {
    # Expected whenever a binary has been away at Microsoft for analysis: the
    # approved file is days old by the time it ships, and the sources have moved
    # on. The approved binary is the one that must go out - rebuilding to satisfy
    # a timestamp would produce a different file that Microsoft never saw.
    Write-Warn2 'Stale-build check skipped (-SkipStaleCheck).'
}
elseif ($newestSource -and $newestSource.LastWriteTime -gt $exe.LastWriteTime) {
    Write-Bad "STALE BUILD: $($newestSource.Name) was modified after the binary was built."
    Write-Bad "  source: $($newestSource.LastWriteTime)   binary: $($exe.LastWriteTime)"
    throw 'Rebuild before releasing - or pass -SkipStaleCheck if this is a binary that has already been through Microsoft analysis.'
}
else {
    Write-Ok 'binary is newer than all sources'
}

$hash = (Get-FileHash -Path $exe.FullName -Algorithm SHA256).Hash.ToLower()
Write-Ok "sha256: $hash"

if (-not $RepoDir) { throw 'Specify -RepoDir (local clone of BigBE4TS/OptiMax-Pro-Tuner).' }
if (-not (Test-Path (Join-Path $RepoDir '.git'))) { throw "$RepoDir is not a git repository." }

$gh = Get-Command gh -ErrorAction SilentlyContinue
if (-not $gh) { Write-Warn2 'GitHub CLI (gh) not found - the release upload step will be skipped and printed for you to do by hand.' }

if ($DryRun) { Write-Warn2 'DRY RUN - nothing below will be committed, pushed, or published.' }

if (-not $Force -and -not $DryRun) {
    Write-Host ''
    $ans = Read-Host "Publish v$version to BigBE4TS/OptiMax-Pro-Tuner? (type 'yes')"
    if ($ans -ne 'yes') { Write-Warn2 'Aborted.'; exit 1 }
}

# ---------------------------------------------------------------------------
# 4. STEP A - the binary. Must exist before anything points at it.
# ---------------------------------------------------------------------------

Write-Step "STEP A: publishing the binary (v$version)"

if ($DryRun -or -not $gh) {
    Write-Warn2 "would run: gh release create v$version `"$($exe.FullName)`" --repo BigBE4TS/OptiMax-Pro-Tuner --title `"v$version`" --generate-notes"
} else {
    & gh release create "v$version" $exe.FullName --repo BigBE4TS/OptiMax-Pro-Tuner --title "v$version" --generate-notes
    if ($LASTEXITCODE -ne 0) { throw 'gh release create failed. Nothing else has been published, so clients are unaffected.' }
    Write-Ok "release v$version created with $EXE_NAME attached"
}

# ---------------------------------------------------------------------------
# 5. STEP B - the checksum. Must land before the version marker.
# ---------------------------------------------------------------------------

Write-Step 'STEP B: publishing the checksum'

$hashFile = Join-Path $RepoDir 'OptiMax_Profiler.exe.sha256'
if ($DryRun) {
    Write-Warn2 "would write $hashFile = $hash"
} else {
    Write-PlainUtf8 -Path $hashFile -Text $hash
    Push-Location $RepoDir
    try {
        & git add -- 'OptiMax_Profiler.exe.sha256'
        & git commit -m "Checksum for v$version"
        if ($LASTEXITCODE -ne 0) { throw 'git commit failed for the checksum.' }
        & git push
        if ($LASTEXITCODE -ne 0) { throw 'git push failed for the checksum. Do NOT publish version.txt.' }
    } finally { Pop-Location }
    Write-Ok 'checksum pushed'
}

# ---------------------------------------------------------------------------
# 6. STEP C - the trigger. LAST, always.
# ---------------------------------------------------------------------------

Write-Step 'STEP C: publishing version.txt (the trigger)'

$versionFile = Join-Path $RepoDir 'version.txt'
if ($DryRun) {
    Write-Warn2 "would write $versionFile = $version (no BOM)"
} else {
    Write-PlainUtf8 -Path $versionFile -Text $version
    Push-Location $RepoDir
    try {
        & git add -- 'version.txt'
        & git commit -m "Release v$version"
        if ($LASTEXITCODE -ne 0) { throw 'git commit failed for version.txt.' }
        & git push
        if ($LASTEXITCODE -ne 0) { throw 'git push failed for version.txt. Clients will not see the release yet.' }
    } finally { Pop-Location }
    Write-Ok 'version.txt pushed - clients will now begin updating'
}

# ---------------------------------------------------------------------------
# 7. Verify what the client will actually see
# ---------------------------------------------------------------------------

if (-not $DryRun) {
    Write-Step 'Verifying published state (this is what the client sees)'
    Start-Sleep -Seconds 5

    $vNow = Get-PublishedFile $VERSION_URL
    $hNow = Get-PublishedFile $HASH_URL
    $bad = $false

    if ($vNow.Text -ne $version) { Write-Bad "version.txt reads '$($vNow.Text)', expected '$version' (CDN may lag ~5 min)"; $bad = $true }
    else { Write-Ok "version.txt = $($vNow.Text)" }

    if ($vNow.HasBom) { Write-Bad 'version.txt came back WITH a BOM - updates will not fire.'; $bad = $true }

    $hNowClean = ($hNow.Text -replace '[^0-9a-fA-F]', '').ToLower()
    if ($hNowClean -ne $hash) { Write-Bad "checksum reads '$hNowClean', expected '$hash'"; $bad = $true }
    else { Write-Ok "checksum matches the uploaded binary" }

    if ($bad) {
        Write-Warn2 'Re-run with -Check in a few minutes; raw.githubusercontent caches for ~5 minutes.'
    } else {
        Write-Host "`nRelease v$version published and verified." -ForegroundColor Green
    }
}
