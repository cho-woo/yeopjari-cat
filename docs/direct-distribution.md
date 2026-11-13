# 공식 웹사이트에서 직접 배포

- 사업자: 가치탐험단 (개인사업자)
- 회사 도메인: `valuewander.com` — 사용자가 Cloudflare에서 구매
- 제품 소개 주소: `cat.valuewander.com`
- 회사 메일: 사용자가 Google Workspace에서 직접 개설 중. 아직 수신 가능한 주소로 공지하지 않음.
- Microsoft Store 등록을 사용하지 않음. Azure 서명 서비스 신청도 진행하지 않음.
- 소개 페이지 소스: 인접한 `yeopjari-cat-site` 디렉터리. Sites project ID는 해당 체크아웃의 `.openai/hosting.json`에서 유지.

## 설치 프로그램

`packaging/windows/installer.nsi`는 NSIS 3.08 이상으로 빌드합니다. Windows 10/11 x64, 현재 사용자 계정 설치이며 관리자 권한을 요청하지 않습니다. 기본 위치는 `%LOCALAPPDATA%\Programs\YeopjariCat`입니다. 기존 앱과 동일한 `%LOCALAPPDATA%\YeopjariCat` 기록을 사용합니다. 시작 메뉴 바로가기와 Windows 설치된 앱 항목을 만들며, 제거할 때 돌봄 기록이나 설치 폴더의 임의 파일을 재귀 삭제하지 않습니다. 실행 중인 고양이를 임의 종료하지 않고 먼저 종료해 달라고 안내합니다. 자동 실행 등록은 추가하지 않습니다.

로컬 검토 빌드는 `--review-unsigned`가 필수이며 파일명·화면·설치 경로·제거 레지스트리 키가 공개용과 구분됩니다. 웹 다운로드로 연결하면 안 됩니다.

```powershell
python tools/build_installer.py --package <검증한-배포폴더> --makensis <makensis.exe> --output build/installer-review --review-unsigned
```

## 공인 코드 서명

HTTPS 도메인 인증, Google Workspace 메일 인증, Windows의 Authenticode 게시자 인증은 별개입니다. 자체 서명 인증서는 일반 사용자 배포용 신뢰를 만들지 않습니다. 인증기관의 조직/개인 코드 서명 인증서를 실제 검증 후 사용해야 합니다. 게시자 표시는 인증기관이 검증한 인증서 Subject에 따라 결정됩니다. 브랜드를 임의로 입력해 대신할 수 없습니다. 개인 실명형 IV 인증서는 회사명이 아닌 검증된 개인 이름을 표시할 수 있으므로 사업자 표시 희망을 확인한 뒤 구매합니다.

앱 EXE를 먼저 서명한 뒤 `tools/release.py`로 ZIP·SHA-256·피드를 생성합니다. NSIS에는 인증기관별 서명 도구를 `--sign-command`로 연결하여 uninstaller와 installer를 모두 서명합니다. 인증서·클라우드 HSM·토큰 비용과 개인사업자의 상호 표시 자격을 구매 전 공식 판매처에서 확인합니다. 기존 v2.0.34의 공개 EXE·ZIP·피드는 덮어쓰지 않습니다.

공개 전에는 새 버전에서 설치/덮어 설치/앱 자체 업데이트/제거/기존 기록 유지 검증을 완료해야 합니다. 특히 설치형 앱에서 받은 이후 업데이트도 동일한 검증된 게시자로 서명하고, 서명 이후 피드 해시를 생성해야 합니다. 검토용 설치 파일만 만든 상태를 정식 배포 완료로 간주하지 않습니다.

`tools/prepare_site_download.py`는 앱·설치 파일·제거 파일 세 개의 실제 Windows 신뢰 체인, 정확한 게시자 Subject, 타임스탬프, 바이트 해시를 검사한 뒤에만 사이트 `release.json`을 `ready`로 만듭니다. 실패하면 사이트 파일을 바꾸지 않습니다. 다운로드 주소는 버전이 고정된 공식 저장소 또는 `downloads.valuewander.com`으로 제한합니다.

공인 서명으로 확인된 게시자를 표시할 수 있어도 새 파일의 SmartScreen 평판 경고가 즉시 사라지는 것은 보장하지 않습니다.

공식 자료:
- https://www.ssl.com/products/software-integrity/code-signing/
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- https://nsis.sourceforge.io/Docs/Chapter5.html
- https://nsis.sourceforge.io/License

## 2026-11-13 준비 상태

소개 페이지를 https://yeopjari-cat.joyden96.chatgpt.site 에 공개했습니다. 로그인 없이 표시되는 것을 브라우저로 확인했습니다. 세 외형 선택, 도움말 펼치기, PC 링크 복사, 390px 모바일 화면, 로컬 파일 연결을 확인했습니다. 사이트 소스 커밋은 `6b555021826bff91fa6babc540c228268e78a963`입니다.

`cat.valuewander.com`은 Cloudflare에 홈페이지용 CNAME 1개와 확인용 TXT 2개를 추가했습니다. 외부 DNS 조회, 호스팅의 도메인·HTTPS 인증서 `active` 상태, 실제 HTTPS 200 응답과 브라우저의 소개 페이지 표시를 확인했습니다. Google Workspace 설정은 사용자가 진행하며 메일 DNS는 변경하지 않았습니다.

NSIS 3.13은 winget의 공식 설치 파일을 SHA-256 `db9a1038bbc0e53fef24f423fd609979bd578e942f1be1198db186d1c42652a3`으로 확인한 뒤 로컬 빌드 폴더에 추출했습니다. 검토용 설치 프로그램 빌드와 앱 원본 바이트 일치, 라이선스 동봉, 실행 중인 고양이가 있을 때 설치 차단을 검사했습니다. 실제 사용자 고양이는 종료하지 않았고, 전체 설치·업데이트·제거는 아직 별도 환경에서 검증하지 않았습니다.

서명되지 않은 현재 EXE를 입력했을 때 웹 다운로드 준비 도구가 `NotSigned`로 거부하고 사이트 `release.json`을 변경하지 않는 것을 확인했습니다. Python 문법 검사와 `git diff --check`도 통과했습니다. 공인 코드 서명 인증서는 아직 발급·결제하지 않았으며, 서명된 정식 설치 파일을 배포한 상태가 아닙니다.
