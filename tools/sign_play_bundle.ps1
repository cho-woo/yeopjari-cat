param([switch]$Initialize)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$paths=Get-Content -Raw (Join-Path $repo 'build/android-dev/paths.json') | ConvertFrom-Json
$keytool=Join-Path $paths.jdk 'bin/keytool.exe'
$jarsigner=Join-Path $paths.jdk 'bin/jarsigner.exe'
$unsigned=Join-Path $repo 'mobile/android/app/build/outputs/bundle/play/app-play.aab'
if(!(Test-Path -LiteralPath $unsigned -PathType Leaf)){throw 'Build bundlePlay first.'}

$vault=Join-Path $env:LOCALAPPDATA 'ValueWander/Signing/yeopjari-cat-play'
$keyFile=Join-Path $vault 'play-upload.p12'
$secretFile=Join-Path $vault 'play-upload.dpapi'
if(!(Test-Path -LiteralPath $vault)){
    if(!$Initialize){throw 'Restore the upload key or initialize it explicitly.'}
    New-Item -ItemType Directory -Path $vault | Out-Null
    $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    & icacls.exe $vault /inheritance:r /grant:r "*${sid}:(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' | Out-Null
    if($LASTEXITCODE){throw 'Could not secure the local key directory.'}
}
if(!(Test-Path -LiteralPath $secretFile)){
    if(!$Initialize -or (Test-Path -LiteralPath $keyFile)){throw 'The protected upload-key password is missing.'}
    $bytes=New-Object byte[] 48
    $rng=[Security.Cryptography.RandomNumberGenerator]::Create()
    try{$rng.GetBytes($bytes)}finally{$rng.Dispose()}
    $secret=ConvertTo-SecureString ([Convert]::ToBase64String($bytes)) -AsPlainText -Force
    $secret | ConvertFrom-SecureString | Out-File -Encoding ascii -NoClobber -LiteralPath $secretFile
    [Array]::Clear($bytes,0,$bytes.Length)
}else{
    $secret=ConvertTo-SecureString (Get-Content -LiteralPath $secretFile -Raw).Trim()
}
$ptr=[Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
try{
    $env:YEOPJARI_UPLOAD_STORE_PASS=[Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    if(!(Test-Path -LiteralPath $keyFile)){
        if(!$Initialize){throw 'Restore the upload keystore before signing.'}
        & $keytool -genkeypair -alias upload -keystore $keyFile -storetype PKCS12 -keyalg RSA -keysize 3072 -sigalg SHA256withRSA -validity 10000 -storepass:env YEOPJARI_UPLOAD_STORE_PASS -dname 'CN=Value Wander Play Upload, O=Value Wander, C=KR'
        if($LASTEXITCODE){throw 'Upload-key generation failed.'}
    }
    $out=New-Item -Force -ItemType Directory -Path (Join-Path $repo 'build/play-registration')
    $signed=Join-Path $out.FullName 'YeopjariCat-GooglePlay.aab'
    & $jarsigner -keystore $keyFile -storetype PKCS12 -storepass:env YEOPJARI_UPLOAD_STORE_PASS -sigalg SHA256withRSA -digestalg SHA-256 -signedjar $signed $unsigned upload
    if($LASTEXITCODE){throw 'AAB signing failed.'}
    & $jarsigner -verify -strict -keystore $keyFile -storetype PKCS12 -storepass:env YEOPJARI_UPLOAD_STORE_PASS $signed upload
    if($LASTEXITCODE){throw 'AAB signature verification failed.'}
    & $keytool -exportcert -rfc -alias upload -keystore $keyFile -storepass:env YEOPJARI_UPLOAD_STORE_PASS -file (Join-Path $out.FullName 'upload-certificate.pem')
    if($LASTEXITCODE){throw 'Could not export public certificate.'}
    Get-FileHash -LiteralPath $signed -Algorithm SHA256
}finally{
    Remove-Item Env:YEOPJARI_UPLOAD_STORE_PASS -ErrorAction SilentlyContinue
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    $secret.Dispose()
}
