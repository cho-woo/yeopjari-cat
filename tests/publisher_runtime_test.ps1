# Runs the generated publisher against an in-process gh mock. No network calls.
[CmdletBinding()] param([string]$PackagePath, [string]$Python = 'python3')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('Yeopjari-publisher-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $temporary)
$global:PublisherRuntimePackage = if ($PackagePath) { (Resolve-Path -LiteralPath $PackagePath).Path } else { Join-Path $temporary 'package' }
$global:PublisherRuntimeMock = $null
$global:PublisherRuntimeChecks = 0

function Assert-Test([bool]$Condition, [string]$Message) {
    $global:PublisherRuntimeChecks++
    if (-not $Condition) { throw $Message }
}

# Function lookup shadows any installed gh executable, including in the child
# publisher script. Unknown commands throw instead of falling through to gh.
function gh {
    $global:LASTEXITCODE = 0
    $arguments = @($args)
    $global:PublisherRuntimeMock.calls.Add(($arguments -join ' '))
    if ($arguments[0] -eq 'api') {
        if ($global:PublisherRuntimeMock.scenario -eq 'missing-role') {
            return '{"private":false,"archived":false}'
        }
        return '{"private":false,"archived":false,"permissions":{"push":false}}'
    }
    if ($arguments[0] -ne 'release') { throw 'Unexpected mocked GitHub command.' }
    switch ($arguments[1]) {
        'list' {
            if ($global:PublisherRuntimeMock.scenario -eq 'published') {
                return '[{"tagName":"v1.10.0","isDraft":false,"isPrerelease":false}]'
            }
            return '[{"tagName":"v1.10.0","isDraft":true,"isPrerelease":false},{"tagName":"v1.9.0","isDraft":false,"isPrerelease":false}]'
        }
        'view' {
            if ($arguments -contains 'url') {
                return 'https://github.com/cho-woo/yeopjari-cat/releases/tag/v1.10.0'
            }
            return '{"isDraft":true,"isPrerelease":false,"assets":[]}'
        }
        'edit' {
            if ($arguments -contains '--notes-file') {
                $global:PublisherRuntimeMock.noteWrites++
                if ($global:PublisherRuntimeMock.scenario -eq 'denied') {
                    $global:LASTEXITCODE = 1
                    return
                }
                $global:PublisherRuntimeMock.writeChecked = $true
                return
            }
            if ($arguments -contains '--draft=false') {
                Assert-Test ($global:PublisherRuntimeMock.downloads -eq 5) 'Published before verifying all five uploaded files.'
                $global:PublisherRuntimeMock.publishes++
                return
            }
            throw 'Unexpected mocked release edit.'
        }
        'upload' {
            Assert-Test $global:PublisherRuntimeMock.writeChecked 'Uploaded before checking actual draft-write permission.'
            Assert-Test (-not ($arguments -contains '--clobber')) 'Publisher must not overwrite existing assets.'
            $global:PublisherRuntimeMock.uploads++
            return
        }
        'download' {
            $global:PublisherRuntimeMock.downloads++
            $directory = $arguments[[array]::IndexOf($arguments, '--dir') + 1]
            $name = $arguments[[array]::IndexOf($arguments, '--pattern') + 1]
            $destination = Join-Path $directory $name
            if ($global:PublisherRuntimeMock.scenario -eq 'hash-mismatch') {
                [IO.File]::WriteAllText($destination, 'Different remote asset bytes')
            } else {
                Copy-Item -LiteralPath (Join-Path $global:PublisherRuntimePackage $name) -Destination $destination
            }
            return
        }
        default { throw ('Unexpected mocked release command: ' + $arguments[1]) }
    }
}

try {
    # Generate the real publisher from current source; the small fixtures test
    # publisher hashes and control flow, without needing a Windows build.
    if (-not $PackagePath) {
        $fixture = @'
import hashlib, sys
from pathlib import Path
sys.path.insert(0, str(Path(sys.argv[1]) / 'tools'))
import release
package = Path(sys.argv[2]) / 'package'
package.mkdir()
names = ['YeopjariCat-1.10.0.exe', 'YeopjariCat-Windows-1.10.0.zip',
         'latest.txt', 'RELEASE_NOTES_KO.txt', 'SHA256SUMS.txt']
hashes = {}
for name in names:
    content = ('Mock publisher fixture: ' + name + '\n').encode()
    (package / name).write_bytes(content)
    hashes[name] = hashlib.sha256(content).hexdigest()
(package / 'publish-github.ps1').write_text(
    release.publisher_script('cho-woo/yeopjari-cat', '1.10.0', hashes),
    encoding='utf-8-sig')
'@
        $fixture | & $Python - $root $temporary
        if ($LASTEXITCODE -ne 0) { throw 'Could not generate publisher fixtures.' }
    }

    foreach ($scenario in @('push-false', 'missing-role', 'denied', 'published', 'hash-mismatch')) {
        $global:PublisherRuntimeMock = @{
            scenario = $scenario
            calls = [System.Collections.Generic.List[string]]::new()
            uploads = 0; downloads = 0; publishes = 0; noteWrites = 0
            writeChecked = $false
        }
        $failure = $null
        try {
            & (Join-Path $global:PublisherRuntimePackage 'publish-github.ps1') -Publish | Out-Null
        } catch {
            $failure = $_.Exception.Message
        }
        switch ($scenario) {
            { $_ -in @('push-false', 'missing-role') } {
                Assert-Test ($null -eq $failure) "Authorized installation-token scenario failed: $scenario / $failure"
                Assert-Test ($global:PublisherRuntimeMock.noteWrites -eq 1) 'Expected one authoritative draft-write check.'
                Assert-Test ($global:PublisherRuntimeMock.uploads -eq 5) 'Expected five asset uploads.'
                Assert-Test ($global:PublisherRuntimeMock.downloads -eq 5) 'Expected five downloaded hash checks.'
                Assert-Test ($global:PublisherRuntimeMock.publishes -eq 1) 'Expected one successful publication after checks.'
            }
            'denied' {
                Assert-Test ($failure -match 'Cannot write draft release notes') 'Denied write did not produce the expected failure.'
                Assert-Test ($global:PublisherRuntimeMock.noteWrites -eq 1) 'Missing authoritative permission attempt.'
                Assert-Test ($global:PublisherRuntimeMock.uploads -eq 0 -and $global:PublisherRuntimeMock.downloads -eq 0) 'Denied permission must stop before asset transfer.'
                Assert-Test ($global:PublisherRuntimeMock.publishes -eq 0) 'Denied permission must not publish.'
            }
            'published' {
                Assert-Test ($failure -match 'already published') 'Existing public version was not refused.'
                Assert-Test ($global:PublisherRuntimeMock.noteWrites -eq 0 -and $global:PublisherRuntimeMock.uploads -eq 0) 'Existing published version must not be modified.'
                Assert-Test ($global:PublisherRuntimeMock.publishes -eq 0) 'Existing public version must not be republished.'
            }
            'hash-mismatch' {
                Assert-Test ($failure -match 'Draft asset differs') 'Mismatched downloaded bytes were not rejected.'
                Assert-Test ($global:PublisherRuntimeMock.uploads -eq 1 -and $global:PublisherRuntimeMock.downloads -eq 1) 'Expected immediate stop at first mismatched asset.'
                Assert-Test ($global:PublisherRuntimeMock.publishes -eq 0) 'Hash mismatch must leave the release unpublished.'
            }
        }
        Write-Host "Publisher runtime scenario passed: $scenario"
    }
    Write-Host "Publisher runtime tests passed: $global:PublisherRuntimeChecks checks; no network calls."
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}
