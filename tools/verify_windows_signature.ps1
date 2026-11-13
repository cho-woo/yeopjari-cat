param(
    [Parameter(Mandatory=$true)][string]$Path,
    [Parameter(Mandatory=$true)][string]$ExpectedSubject
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$resolvedFile = (Resolve-Path -LiteralPath $Path).Path
$signature = Get-AuthenticodeSignature -LiteralPath $resolvedFile
if ($signature.Status -ne 'Valid' -or $signature.SignatureType -ne 'Authenticode') {
    throw "공인 Authenticode 서명이 유효하지 않습니다: $($signature.Status)"
}
if ($signature.SignerCertificate.Subject -cne $ExpectedSubject) {
    throw '인증된 게시자와 인증서 Subject가 일치하지 않습니다.'
}
if (-not $signature.TimeStamperCertificate) {
    throw '장기 검증을 위한 타임스탬프가 없습니다.'
}
[ordered]@{
    status = 'valid'
    subject = $signature.SignerCertificate.Subject
    publisher_name = $signature.SignerCertificate.GetNameInfo([System.Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false)
    certificate_thumbprint = $signature.SignerCertificate.Thumbprint
    timestamp_certificate_thumbprint = $signature.TimeStamperCertificate.Thumbprint
    sha256 = (Get-FileHash -LiteralPath $resolvedFile -Algorithm SHA256).Hash.ToLowerInvariant()
    bytes = (Get-Item -LiteralPath $resolvedFile).Length
} | ConvertTo-Json -Compress
