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
    & "$env:JAVA_HOME/bin/javac.exe" -d $clockClasses (Join-Path $repo 'mobile/android/app/src/main/java/com/valuewander/yeopjaricat/PointerGesture.java') (Join-Path $repo 'mobile/tests/PointerGestureSpec.java')
    if($LASTEXITCODE -ne 0){throw 'Pointer gesture test compilation failed.'}
    & "$env:JAVA_HOME/bin/java.exe" -cp $clockClasses com.valuewander.yeopjaricat.PointerGestureSpec
    if($LASTEXITCODE -ne 0){throw 'Pointer gesture tests failed.'}
    & "$env:JAVA_HOME/bin/javac.exe" -d $clockClasses (Join-Path $repo 'mobile/android/app/src/direct/java/com/valuewander/yeopjaricat/UpdatePolicy.java') (Join-Path $repo 'mobile/tests/UpdatePolicySpec.java')
    if($LASTEXITCODE -ne 0){throw 'Update policy test compilation failed.'}
    & "$env:JAVA_HOME/bin/java.exe" -cp $clockClasses com.valuewander.yeopjaricat.UpdatePolicySpec
    if($LASTEXITCODE -ne 0){throw 'Update policy tests failed.'}
    $property='sdk.dir='+($env:ANDROID_HOME -replace '\\','/' -replace ':','\:')
    [IO.File]::WriteAllText((Join-Path $repo 'mobile/android/local.properties'),$property+"`n",[Text.Encoding]::ASCII)
    & (Join-Path $repo '.venv/Scripts/python.exe') (Join-Path $repo 'tools/export_android_motions.py')
    if($LASTEXITCODE -ne 0){throw 'Motion export failed.'}
    $tasks=@(':app:assembleDebug',':app:lintDebug')
    if($Tests){$tasks+=':app:assembleDebugAndroidTest'}
    & ($paths.gradle.Replace($repo,$drive)) -p "$drive/mobile/android" @tasks --console=plain
    if($LASTEXITCODE -ne 0){throw 'Android build failed.'}
    $metadata=Get-Content -LiteralPath (Join-Path $repo 'mobile/android/app/build/outputs/apk/debug/output-metadata.json') -Raw | ConvertFrom-Json
    $version=$metadata.elements[0].versionName
    if(-not $version){throw 'Android versionName missing.'}
    $out=Join-Path $repo "build/android-$version"
    New-Item -ItemType Directory -Force $out | Out-Null
    $apk=Join-Path $out "YeopjariCat-Android-$version.apk"
    Copy-Item -LiteralPath (Join-Path $repo 'mobile/android/app/build/outputs/apk/debug/app-debug.apk') -Destination $apk
    Get-FileHash -LiteralPath $apk -Algorithm SHA256
} finally {
    # Only remove the mapping created by this invocation. No files are removed.
    & subst.exe $drive /D
}
