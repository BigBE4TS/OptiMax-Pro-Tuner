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

.PARAMETER GitUserName
.PARAMETER GitUserEmail
    Identity used for the two release commits. Defaults match the identity every
    prior public commit carries. See the identity note in step B.

.PARAMETER PropagationTimeoutSec
    How long to wait for the pushed checksum to actually reach the raw CDN before
    publishing version.txt. See the propagation gate between steps B and C.

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
    [switch]$Force,
    # IDENTITY: these defaults are deliberate, not incidental. Every public commit
    # on BigBE4TS/OptiMax-Pro-Tuner before v4.2.0 was authored as
    # "BE4TS <andreispid@yahoo.com>", specifically so a second address of the
    # owner's never lands in public history. A fresh clone inherits NO identity
    # (there is no global git identity on the build machine), and the dev repo's
    # local identity is a DIFFERENT address - so taking the identity from there,
    # or letting git guess, publishes the wrong one.
    # This happened for real on the v4.2.0 release: commits 4544194, 1970d24 and
    # fc9b345 went out under the wrong address. Pinning it here is the fix.
    # The address itself is deliberately not written here - this file is mirrored
    # to the public repo, and naming it would publish the very address the pinning
    # exists to keep out. The three commit SHAs are enough to find the incident.
    [string]$GitUserName  = 'BE4TS',
    [string]$GitUserEmail = 'andreispid@yahoo.com',
    [int]$PropagationTimeoutSec = 900,

    # Release notes. --generate-notes used to be hardcoded with no override, and
    # on this repo it produces near-empty notes: the public repository's history
    # is release-artefact commits, so there is nothing for GitHub to summarise.
    # The v4.2.1 release shipped with generated notes that omitted the mandatory
    # throttling caveat and had to be corrected by hand with `gh release edit
    # --notes-file` straight afterwards. That worked only because version.txt
    # lands last, so no client had seen the release yet - a one-time reprieve
    # that depended entirely on remembering to do it.
    [string]$NotesFile
)

$ErrorActionPreference = 'Stop'

$RAW_BASE   = 'https://raw.githubusercontent.com/BigBE4TS/OptiMax-Pro-Tuner/main'
$VERSION_URL = "$RAW_BASE/version.txt"
$HASH_URL    = "$RAW_BASE/OptiMax_Profiler.exe.sha256"
$EXE_NAME    = 'OptiMax_Profiler.exe'

# THE 'v' PREFIX IS MANDATORY. version.txt has carried it for all 13 releases from
# v3.2.0 to v4.1.1. v4.2.0 shipped bare as "4.2.0" and had to be republished the
# same night, because the clients that must parse it - v3.2 through v4.1.1 - have
# no surviving source, so nothing could confirm a bare string was safe for them.
# The current client strips a leading v/V (OptiMaxApp.h:704) BEFORE validating
# digits-and-dots, so the prefix is correct for new clients and is the only format
# old ones have ever seen. Do not "tidy" this away.
$VERSION_PREFIX = 'v'

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

# Downloads a raw URL and reports whether the payload carries a BOM. Returns a
# PSCustomObject so callers can inspect both.
#
# ⚠️ THE CACHE BUSTER DOES NOT WORK, and the query string is kept only because it
# is harmless. raw.githubusercontent.com keys its cache on the PATH ONLY, so
# "?t=<ticks>" changes nothing and a request can still be served a value that is
# minutes stale. Measured on the v4.2.0 release: git had pushed and the GitHub API
# reported the new content while raw still served the old for over a minute.
# Consequences, both of which this script now handles:
#   - never treat a raw read as authoritative for "did my push land" - poll it
#     until it AGREES, which is what Wait-ForPropagation does;
#   - the same false assumption is in the client (OptiMaxUpdater.h:83,
#     OptiMaxCloudSync.h:143) - tracked as A5-4, harmless there but the comments
#     overstate what it buys.
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
    # THE FIX: this could not parse a leading 'v', so published "v4.1.1" read as
    # 0.1.1. That made Test-VersionNewer answer "yes, newer" against almost
    # anything - the preflight version guard was effectively disabled, and it
    # would have stayed disabled forever now that the 'v' prefix is mandatory.
    # It never fired wrongly only because releases happened to go up in order.
    $V = $V.Trim()
    if ($V -match '^[vV]') { $V = $V.Substring(1) }
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

# Blocks until the raw CDN actually serves the value we just pushed.
#
# WHY THIS EXISTS. Publishing in the order binary -> checksum -> version.txt is
# necessary but NOT sufficient, because the two text files reach the CDN
# independently and not in commit order. Measured during the v4.2.0 release:
#
#   00:13:04  version=v4.1.1  sha=aafc762b     <- both old
#   00:13:34  version=4.2.0   sha=aafc762b     <- NEW version, OLD checksum
#   00:14:34  version=4.2.0   sha=e89c8fd8     <- consistent
#
# For ~60 seconds a client would have seen the new version, downloaded the new
# binary, compared it against the STALE checksum, and reported a mismatch - which
# the user is shown as tampering (S3-32). That release got away with it only
# because it had no userbase yet.
#
# So: after pushing the checksum, wait until raw genuinely serves it, and only
# then publish the trigger. Polling is the only option - see the cache-buster note
# on Get-PublishedFile.
function Wait-ForPropagation {
    param(
        [string]$Url,
        [string]$Expected,      # compared after normalising, so 'v4.2.0' matches '4.2.0'
        [int]$TimeoutSec,
        [string]$What
    )
    $norm = { param($s) ($s -replace '^[vV]', '').Trim().ToLower() }
    $target = & $norm $Expected
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $attempt = 0

    while ((Get-Date) -lt $deadline) {
        $attempt++
        $got = Get-PublishedFile $Url
        if ($got.Ok -and (& $norm $got.Text) -eq $target) {
            Write-Ok "$What propagated to the CDN (after $attempt check(s))"
            return $true
        }
        $shown = if ($got.Ok) { $got.Text } else { "unreachable: $($got.Error)" }
        Write-Warn2 "waiting for $What - CDN still serves '$shown'"
        Start-Sleep -Seconds 20
    }
    return $false
}

# ---------------------------------------------------------------------------
# 1. Version, read from the single source of truth in the source tree
# ---------------------------------------------------------------------------

Write-Step 'Reading version from OptiMaxVersion.h'

# The version used to live in ten places and this script grew a five-site drift
# guard for resource.rc, which was the half that actually shipped wrong. As of
# v4.2.2 there is exactly one site: OptiMaxVersion.h, included by both the C++
# and resource.rc. The old textual guard is gone because it was defending
# against a drift that can no longer happen - what IS still worth checking is
# that the binary about to ship really carries this version, which is done in
# the preflight below against the file's own resource.
$verHeader = Join-Path $SourceDir 'OptiMaxVersion.h'
if (-not (Test-Path $verHeader)) { throw "Cannot find OptiMaxVersion.h at $verHeader" }

$verText = Get-Content $verHeader -Raw
$vMajor = [regex]::Match($verText, '(?m)^\s*#define\s+OPTIMAX_VERSION_MAJOR\s+([0-9]+)')
$vMinor = [regex]::Match($verText, '(?m)^\s*#define\s+OPTIMAX_VERSION_MINOR\s+([0-9]+)')
$vPatch = [regex]::Match($verText, '(?m)^\s*#define\s+OPTIMAX_VERSION_PATCH\s+([0-9]+)')
if (-not ($vMajor.Success -and $vMinor.Success -and $vPatch.Success)) {
    throw 'Could not parse OPTIMAX_VERSION_MAJOR/MINOR/PATCH out of OptiMaxVersion.h'
}

$version = "$($vMajor.Groups[1].Value).$($vMinor.Groups[1].Value).$($vPatch.Groups[1].Value)"
Write-Ok "OPTIMAX_VERSION = $version  (OptiMaxVersion.h)"

# Regression guard, not a drift guard: if someone hardcodes a version back into
# resource.rc, the single-source property is quietly lost and nothing else would
# ever notice. Cheap to check, so check it.
$rcPath = Join-Path $SourceDir 'resource.rc'
if (Test-Path $rcPath) {
    $rc = Get-Content $rcPath -Raw
    $hard = @()
    if ($rc -match '(?m)^\s*FILEVERSION\s+[0-9]')      { $hard += 'FILEVERSION is a literal again' }
    if ($rc -match '(?m)^\s*PRODUCTVERSION\s+[0-9]')   { $hard += 'PRODUCTVERSION is a literal again' }
    if ($rc -match 'VALUE\s+"FileVersion"\s*,\s*"')    { $hard += '"FileVersion" is a literal again' }
    if ($rc -match 'VALUE\s+"ProductVersion"\s*,\s*"') { $hard += '"ProductVersion" is a literal again' }
    if ($rc -notmatch 'OptiMaxVersion\.h')             { $hard += 'no longer includes OptiMaxVersion.h' }

    if ($hard.Count -gt 0) {
        foreach ($b in $hard) { Write-Bad "resource.rc: $b" }
        if (-not $Force) {
            throw 'resource.rc has stopped deriving its version from OptiMaxVersion.h. Put the macros back, or pass -Force.'
        }
        Write-Warn2 'resource.rc regression ignored because -Force was given.'
    }
    else { Write-Ok 'resource.rc derives all five version fields from OptiMaxVersion.h' }
}

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

# Check the ARTEFACT, not the source. This replaces the old five-site resource.rc
# text comparison, and it is a strictly better question to ask: the source can be
# correct while the binary in hand was built before the bump, and that binary is
# what users get. -SkipStaleCheck deliberately allows shipping an older approved
# build, which makes this the only thing standing between a stale exe and a
# release whose file properties contradict its own version.txt.
$exeVer = $exe.VersionInfo.FileVersion
$exeProd = $exe.VersionInfo.ProductVersion
$wantVer = "$version.0"
if (-not $exeVer) {
    Write-Warn2 'binary carries no version resource - cannot verify it matches.'
}
elseif (($exeVer.Trim() -ne $wantVer) -or ($exeProd -and $exeProd.Trim() -ne $wantVer)) {
    Write-Bad "binary reports FileVersion '$exeVer', ProductVersion '$exeProd' - expected '$wantVer'"
    if (-not $Force) {
        throw "The binary was built from a different version than OptiMaxVersion.h says ($version). Rebuild, or pass -Force if you know this is the approved file."
    }
    Write-Warn2 'binary version mismatch ignored because -Force was given.'
}
else {
    Write-Ok "binary file properties report $exeVer, matching OptiMaxVersion.h"
}

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

# -NotesFile wins over --generate-notes when given. Resolved and read BEFORE
# anything is published: a missing notes file must stop the release here, while
# nothing has been pushed, rather than after the binary is already up.
$notesArgs = @('--generate-notes')
if ($NotesFile) {
    if (-not (Test-Path $NotesFile)) {
        throw "-NotesFile '$NotesFile' does not exist. Nothing has been published."
    }
    $resolvedNotes = (Resolve-Path $NotesFile).Path
    if (-not (Get-Content $resolvedNotes -Raw).Trim()) {
        throw "-NotesFile '$resolvedNotes' is empty. Nothing has been published."
    }
    $notesArgs = @('--notes-file', $resolvedNotes)
    Write-Ok "release notes will come from $resolvedNotes"
}
else {
    Write-Warn2 'No -NotesFile given, falling back to --generate-notes. On this repo that produces near-empty notes - see the parameter comment.'
}

if ($DryRun -or -not $gh) {
    Write-Warn2 "would run: gh release create v$version `"$($exe.FullName)`" --repo BigBE4TS/OptiMax-Pro-Tuner --title `"v$version`" $($notesArgs -join ' ')"
} else {
    & gh release create "v$version" $exe.FullName --repo BigBE4TS/OptiMax-Pro-Tuner --title "v$version" @notesArgs
    if ($LASTEXITCODE -ne 0) { throw 'gh release create failed. Nothing else has been published, so clients are unaffected.' }
    Write-Ok "release v$version created with $EXE_NAME attached"
}

# ---------------------------------------------------------------------------
# 5. STEP B - the checksum. Must land before the version marker.
# ---------------------------------------------------------------------------

Write-Step 'STEP B: publishing the checksum'

$hashFile = Join-Path $RepoDir 'OptiMax_Profiler.exe.sha256'
if ($DryRun) {
    Write-Warn2 "would set commit identity to $GitUserName <$GitUserEmail> on $RepoDir"
    Write-Warn2 "would write $hashFile = $hash"
} else {
    Write-PlainUtf8 -Path $hashFile -Text $hash
    Push-Location $RepoDir
    try {
        # IDENTITY, set on the CLONE and every time. Two separate failures made
        # this necessary during the v4.2.0 release:
        #   1. A fresh clone inherits no identity and there is no global one on
        #      this machine, so `git commit` died with "Author identity unknown"
        #      AFTER the binary had been published - a half-finished release.
        #      Only the publish order saved it: version.txt had not moved, so no
        #      client ever saw the inconsistent state.
        #   2. Setting it by hand then used the DEV repo's address, publishing a
        #      second email of the owner's into public history, contrary to the
        #      deliberate choice recorded for every earlier commit.
        # Local to this clone: --global is never touched.
        & git config user.name  $GitUserName
        & git config user.email $GitUserEmail
        if ($LASTEXITCODE -ne 0) { throw "Could not set the commit identity on $RepoDir." }
        Write-Ok "commit identity: $GitUserName <$GitUserEmail>"

        & git add -- 'OptiMax_Profiler.exe.sha256'
        & git commit -m "Checksum for v$version"
        if ($LASTEXITCODE -ne 0) { throw 'git commit failed for the checksum.' }
        & git push
        if ($LASTEXITCODE -ne 0) { throw 'git push failed for the checksum. Do NOT publish version.txt.' }
    } finally { Pop-Location }
    Write-Ok 'checksum pushed'
}

# ---------------------------------------------------------------------------
# 5b. THE PROPAGATION GATE - between B and C, and the reason both exist
# ---------------------------------------------------------------------------
# Pushing is not publishing. Until raw serves the new checksum, publishing
# version.txt opens a window where clients see a new version and verify it
# against the old hash. See Wait-ForPropagation for the measured evidence.

Write-Step 'Waiting for the checksum to reach the CDN before publishing the trigger'

if ($DryRun) {
    Write-Warn2 "would poll $HASH_URL until it serves $hash (timeout ${PropagationTimeoutSec}s)"
} else {
    if (-not (Wait-ForPropagation -Url $HASH_URL -Expected $hash -TimeoutSec $PropagationTimeoutSec -What 'checksum')) {
        Write-Bad "The checksum has not appeared on the CDN within $PropagationTimeoutSec seconds."
        Write-Bad 'version.txt has deliberately NOT been published, so no client can see a broken release.'
        Write-Bad 'The binary and checksum ARE published; only the trigger is outstanding.'
        Write-Bad 'Wait for the CDN, confirm with:  .\release.ps1 -Check'
        Write-Bad 'then publish the trigger by hand from the clone:'
        Write-Bad "    [System.IO.File]::WriteAllText('$(Join-Path $RepoDir 'version.txt')', `"$VERSION_PREFIX$version``n`", (New-Object System.Text.UTF8Encoding(`$false)))"
        Write-Bad "    git -C '$RepoDir' commit -am 'Release v$version' && git -C '$RepoDir' push"
        throw 'Propagation gate timed out - release is incomplete but SAFE (no client sees it).'
    }
}

# ---------------------------------------------------------------------------
# 6. STEP C - the trigger. LAST, always.
# ---------------------------------------------------------------------------

Write-Step 'STEP C: publishing version.txt (the trigger)'

$versionFile = Join-Path $RepoDir 'version.txt'
$versionMarker = "$VERSION_PREFIX$version"     # 'v4.2.1' - see the $VERSION_PREFIX note

if ($DryRun) {
    Write-Warn2 "would write $versionFile = $versionMarker (no BOM)"
} else {
    Write-PlainUtf8 -Path $versionFile -Text $versionMarker
    Push-Location $RepoDir
    try {
        & git config user.name  $GitUserName
        & git config user.email $GitUserEmail
        & git add -- 'version.txt'
        & git commit -m "Release v$version"
        if ($LASTEXITCODE -ne 0) { throw 'git commit failed for version.txt.' }
        & git push
        if ($LASTEXITCODE -ne 0) { throw 'git push failed for version.txt. Clients will not see the release yet.' }
    } finally { Pop-Location }
    Write-Ok "version.txt pushed as '$versionMarker' - clients will now begin updating"
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

    # Compared with the prefix normalised away, so this keeps working whichever
    # form is published - the client tolerates both, and only the write path
    # decides which one goes out.
    $vNowNorm = ($vNow.Text -replace '^[vV]', '').Trim()
    if ($vNowNorm -ne $version) { Write-Bad "version.txt reads '$($vNow.Text)', expected '$versionMarker' (CDN may lag ~5 min)"; $bad = $true }
    else { Write-Ok "version.txt = $($vNow.Text)" }

    if ($vNow.Ok -and $vNow.Text -notmatch '^[vV]') {
        Write-Warn2 "version.txt is published WITHOUT the 'v' prefix. Every client from v3.2 to"
        Write-Warn2 "v4.1.1 has only ever parsed the prefixed form and their source no longer exists."
    }

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
