# 개발 안내

## 구조와 빌드

- `src/`: 원본 v1.9.0 Win32 앱. `main.cpp`는 창·메뉴·트레이·타이머·저장과 각 모듈을 연결합니다. 이번에는 동작 보존을 위해 분할하지 않았습니다.
- `src/cat_behavior.hpp`, `cat_animation.hpp`: 성격, 행동 시퀀스, 애니메이션.
- `src/cat_physics.*`: Box2D 기반 이동·중력·접촉·보간. 창 발판 탐색은 `main.cpp`에서 수행합니다.
- `src/pet_state.hpp`, `cat_nutrition.hpp`, `cat_preferences.hpp`, `cat_audio_model.hpp`: 저장·성장·체형·캐릭터·의상·소리 모델.
- `src/update_*.hpp`, `cat_update_notice.hpp`: HTTPS 피드, 버전/해시 검증, 교체·복구, 업데이트 말풍선.
- `assets/`: RC 리소스로 실행 파일 안에 포함하는 PNG 6개, WAV 14개. 라이선스·음원 편집 고지는 루트 `THIRD_PARTY_NOTICES.txt`에 보존합니다.
- `tests/`: C++ 모델·물리 및 Python 배포·리소스 회귀 검사.
- `tools/`: 설치·테스트·패키징·리소스 및 공개 v1.9.0 비교 검사. 음원 제작 도구는 원본 그대로이며 일반 빌드에는 필요하지 않습니다.
- `build-inputs/`, `release-bundles/`, `docs/history/workflows/`: v1.3~v1.9 배포 이력. 오래된 자동 배포 작업은 활성 워크플로에서 분리했습니다.

Linux x86_64, Python 3.10 이상, GCC/G++, Git, curl, tar/xz, ar를 준비하고 저장소에서 실행합니다. CI 기준은 Ubuntu 24.04입니다.

```bash
bash tools/setup.sh
bash tools/test.sh  # 모델·물리·Python 검사만
bash build.sh      # Windows x64 EXE 및 실제 한글 리소스 검사
bash tools/check.sh # 위 검사와 ZIP·피드 검증을 한 번에 실행
```

명령은 호출 디렉터리와 무관하게 저장소 루트를 사용합니다. 의존성과 출력은 Git에서 제외합니다. `setup.sh`는 체크섬으로 LLVM-MinGW 20261020을 검증하고 Box2D 3.1.1의 정확한 커밋 `8c661469c9507d3ad6fbd2fea3f1aa71669c2fe3`을 확인합니다. 사용자 소스는 덮어쓰지 않으며 수정된 Box2D 작업 디렉터리도 덮어쓰지 않습니다. 빌드는 SSE2 기준, AVX2 비활성, 정적 링크이며 별도 엔진 런타임 설치가 필요 없습니다. GCC 네이티브 물리 검사에는 POSIX 기능 매크로가 필요합니다.

`tools/release.py`는 원본 도구를 보존했습니다. 이 도구가 읽는 `release/THIRD_PARTY_NOTICES.txt`는 설치 시 루트 고지에서 복사합니다. 매 검사마다 별도 패키지 디렉터리를 생성하므로 기존 결과를 덮어쓰지 않습니다. 릴리스 버전은 `src/app_version.h`에서 읽습니다.

## 저장 기록 호환성

이번 변경은 저장 형식, 파일 경로, 직렬화, 동작 코드에 손대지 않았으므로 마이그레이션이 없습니다. 실제 사용자 저장 파일을 빌드·검사 입력으로 사용하지 않습니다.

| 기록 | 기존 위치·규칙 |
| --- | --- |
| 생일·성장·돌봄 | `%LOCALAPPDATA%\YeopjariCat\pet.ini`, `pet.bak`, 월별 `pet-YYYY-MM.ini`; schema 1 |
| 체형·식사 | `nutrition.ini`, `nutrition.bak`; 생일 일치 검사, 과거 기록에서 초기화하는 기존 로직 유지 |
| 캐릭터·의상 | `preferences.ini`, `preferences.bak`; schema 2, 기존 schema 1 읽기 유지 |
| 성격 | 저장된 생일에서 결정적으로 파생; 생일과 Trait/Mix/Classify 알고리즘을 보존해야 함 |
| 소리 | `sound-settings.ini`, `sound-settings.bak` |
| 업데이트 설정·알림 | `update-settings.ini`, `update-notice.ini` |

원본의 체크섬·손상 파일 보존·백업·임시 파일 교체 로직을 유지합니다. 앞으로 저장 형식 변경 시 과거 파일의 생일·돌봄·설정을 유지하는 자동 마이그레이션과 실제 이전 형식 fixture 검사가 필요합니다. 실패 시 원본 보존과 백업 복구도 검사해야 합니다. 생일 파생 성격을 변경 가능한 성격으로 옮기는 v1.10 작업은 별도 설계·테스트 대상입니다.

## 업데이트와 배포

`update-feed.txt`는 계속 EXE 옆에 둡니다. 공식 주소는 `https://github.com/cho-woo/yeopjari-cat/releases/latest/download/latest.txt`입니다. HTTPS, 앱 ID, stable 채널, 버전, 다운로드 크기·SHA256, EXE 제품 정보, 한국어 업데이트 창 검증과 교체·재시작 코드를 유지했습니다.

현재 CI는 읽기 권한으로 검사와 아티팩트 생성만 수행합니다. v1.9.0은 이미 공개된 버전이므로 다시 업로드하거나 덮어쓰지 않습니다. 새 기능 개발 시 버전 헤더·변경 내용을 함께 갱신하고 `tools/check.sh`를 통과시킵니다. 이 도구의 `release.py --verify`는 ZIP 내용, 실행 파일·매니페스트 해시, 버전, 피드 URL과 파일 구성을 검사합니다. 필요 시 생성된 `publish-github.ps1`의 초안 업로드·원격 해시 검증 절차를 사용하며 검증 실패 시 공개하지 않습니다. 공개 후에는 공식 latest 피드가 해당 버전을 가리키고 실제 다운로드 크기·해시가 일치하는지도 확인해야 합니다.

Linux 크로스 빌드와 모델 테스트는 Windows 실기기 검사를 대신하지 않습니다. 실제 Windows 10/11에서 작업표시줄·창 발판·DPI/다중 모니터·소리·트레이·업데이트 중 교체/재시작과 기존 기록 복구를 확인해야 합니다. 아직 코드 서명은 없습니다.
