param([switch]$Tests)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$pathsFile=Join-Path $repo 'build/android-dev/paths.json'
if(-not(Test-Path -LiteralPath $pathsFile)){throw 'Run tools/setup_android_dev.ps1 first.'}
$paths=Get-Content -LiteralPath $pathsFile -Raw | ConvertFrom-Json
# Android's native Windows tools reject Unicode project paths. Use a temporary
# drive mapping, retaining all source/artifacts in the original repository.
$drive= @('Q:','R:','S:','T:','U:','V:','W:','X:','Y:','Z:') | Where-Object { -not(Test-Path -LiteralPath "$_/") } | Select-Object -First 1
if(-not $drive){throw 'No free temporary drive letter for the Android build.'}
& subst.exe $drive $repo
if($LASTEXITCODE -ne 0){throw 'Temporary build mapping failed.'}
try {
    $env:JAVA_HOME=$paths.jdk.Replace($repo,$drive)
    $env:ANDROID_HOME=$paths.sdk.Replace($repo,$drive)
    $clockClasses=Join-Path $repo 'build/android-clock-tests'
    New-Item -ItemType Directory -Force $clockClasses | Out-Null
    & "$env:JAVA_HOME/bin/javac.exe" -d $clockClasses (Join-Path $repo 'mobile/android/app/src/main/java/com/valuewander/yeopjaricat/MotionClock.java') (Join-Path $repo 'mobile/tests/MotionClockSpec.java')
    if($LASTEXITCODE -ne 0){throw 'Motion clock test compilation failed.'}
    & "$env:JAVA_HOME/bin/java.exe" -cp $clockClasses com.valuewander.yeopjaricat.MotionClockSpec
    if($LASTEXITCODE -ne 0){throw 'Motion clock tests failed.'}
    $property='sdk.dir='+($env:ANDROID_HOME -replace '\\','/' -replace ':','\:')
    [IO.File]::WriteAllText((Join-Path $repo 'mobile/android/local.properties'),$property+"`n",[Text.Encoding]::ASCII)
    & (Join-Path $repo '.venv/Scripts/python.exe') (Join-Path $repo 'tools/export_android_motions.py')
    if($LASTEXITCODE -ne 0){throw 'Motion export failed.'}
    $tasks=@(':app:assembleDebug',':app:lintDebug')
    if($Tests){$tasks+=':app:assembleDebugAndroidTest'}
    & ($paths.gradle.Replace($repo,$drive)) -p "$drive/mobile/android" @tasks --console=plain
    if($LASTEXITCODE -ne 0){throw 'Android build failed.'}
    $out=Join-Path $repo 'build/android-preview'
    New-Item -ItemType Directory -Force $out | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo 'mobile/android/app/build/outputs/apk/debug/app-debug.apk') -Destination (Join-Path $out 'YeopjariCat-Android-0.1.0-preview.apk')
    Get-FileHash -LiteralPath (Join-Path $out 'YeopjariCat-Android-0.1.0-preview.apk') -Algorithm SHA256
} finally {
    # Only remove the mapping created by this invocation. No files are removed.
    & subst.exe $drive /D
}
