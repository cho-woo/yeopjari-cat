param([switch]$WithEmulator)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$dev = Join-Path $repo 'build/android-dev'
New-Item -ItemType Directory -Force -Path $dev | Out-Null
function Fetch-Verified($url, $file, $algorithm, $expected) {
    if (-not (Test-Path -LiteralPath $file)) {
        & curl.exe --fail --silent --show-error --location --retry 2 --output $file $url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $url" }
    }
    $actual = (Get-FileHash -LiteralPath $file -Algorithm $algorithm).Hash.ToLowerInvariant()
    if ($actual -ne $expected.ToLowerInvariant()) { throw "Checksum mismatch: $file" }
}
$jdkZip = Join-Path $dev 'microsoft-jdk-17.zip'
$jdkUrl = 'https://aka.ms/download-jdk/microsoft-jdk-17.0.21.0.2-windows-x64.zip'
$jdkHash = (& curl.exe --fail --silent --show-error --location "$jdkUrl.sha256sum.txt").Split(' ')[0].Trim()
if ($jdkHash -notmatch '^[a-fA-F0-9]{64}$') { throw 'Missing official JDK checksum' }
Fetch-Verified $jdkUrl $jdkZip SHA256 $jdkHash
if (-not (Get-ChildItem -LiteralPath $dev -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin/javac.exe') })) { Expand-Archive -LiteralPath $jdkZip -DestinationPath $dev }
$jdk = Get-ChildItem -LiteralPath $dev -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin/javac.exe') } | Select-Object -First 1
$env:JAVA_HOME = $jdk.FullName
$gradleZip = Join-Path $dev 'gradle-8.13-bin.zip'
$gradleHash = (& curl.exe --fail --silent --show-error --location 'https://services.gradle.org/distributions/gradle-8.13-bin.zip.sha256').Trim()
Fetch-Verified 'https://services.gradle.org/distributions/gradle-8.13-bin.zip' $gradleZip SHA256 $gradleHash
if (-not (Test-Path -LiteralPath (Join-Path $dev 'gradle-8.13'))) { Expand-Archive -LiteralPath $gradleZip -DestinationPath $dev }
$sdk = Join-Path $dev 'sdk'
$cli = Join-Path $sdk 'cmdline-tools/latest'
if (-not (Test-Path -LiteralPath (Join-Path $cli 'bin/sdkmanager.bat'))) {
    $cliZip = Join-Path $dev 'cmdline-tools.zip'
    Fetch-Verified 'https://dl.google.com/android/repository/commandlinetools-win-16111833_latest.zip' $cliZip SHA1 '57d04f2d75eb8e8fffc5000a987e5de4b5a63e9d'
    $unpack = Join-Path $dev 'cli-unpack'
    Expand-Archive -LiteralPath $cliZip -DestinationPath $unpack -Force
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $cli) | Out-Null
    Move-Item -LiteralPath (Join-Path $unpack 'cmdline-tools') -Destination $cli
}
$env:ANDROID_HOME = $sdk
$env:GRADLE_USER_HOME = Join-Path $dev 'gradle-cache'
$manager = Join-Path $cli 'bin/sdkmanager.bat'
1..30 | ForEach-Object { 'y' } | & $manager "--sdk_root=$sdk" --licenses | Out-File (Join-Path $dev 'sdk-license-install.log')
$packages = @('platform-tools', 'platforms;android-36', 'build-tools;35.0.0')
if ($WithEmulator) { $packages += @('emulator', 'system-images;android-35;google_apis;x86_64') }
foreach ($package in $packages) {
    & (Join-Path $cli 'bin/android.exe') --no-metrics "--sdk=$sdk" sdk install $package
    if ($LASTEXITCODE -ne 0) { throw "SDK installation failed: $package" }
}
@{ jdk=$jdk.FullName; sdk=$sdk; gradle=(Join-Path $dev 'gradle-8.13/bin/gradle.bat') } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dev 'paths.json') -Encoding utf8
$local = 'sdk.dir=' + ($sdk -replace '\\', '/')
$escapedLocal = -join ($local.ToCharArray() | ForEach-Object { if ([int]$_ -gt 127) { '\u{0:x4}' -f [int]$_ } else { [string]$_ } })
$escapedLocal | Set-Content -LiteralPath (Join-Path $repo 'mobile/android/local.properties') -Encoding ascii
Write-Output 'Local Android development tools ready.'
