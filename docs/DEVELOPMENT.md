# 개발 안내

## 구조와 빌드

- `src/`: 복원한 v1.9.0을 기반으로 유지보수하는 Win32 앱. `main.cpp`는 창·메뉴·트레이·타이머·저장과 각 모듈을 연결합니다.
- `src/cat_behavior.hpp`, `cat_animation.hpp`: 성격, 행동 시퀀스, 애니메이션.
- `src/cat_physics.*`: Box2D 기반 이동·중력·접촉·보간. 창 발판 탐색은 `main.cpp`에서 수행합니다.
- `src/pet_state.hpp`, `cat_nutrition.hpp`, `cat_preferences.hpp`, `cat_audio_model.hpp`: 저장·성장·체형·캐릭터·의상·소리 모델.
- `src/cat_memory.hpp`, `cat_memory_storage.hpp`: 친밀도·돌봄 습관·느린 성격 학습 및 기존 기록에서의 자동 초기화·복구·저장 충돌 검사.
- `src/cat_routine.hpp`, `cat_memory_motion.hpp`: 현지 시간대 생활·장기 육성 시퀀스 및 친밀도에 따른 접근 거리 계산.
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

v1.10.0은 기존 저장 형식과 경로를 유지하고 별도 `memory.ini`를 자동 생성합니다. 기존 생일·밥·츄르 기록만 초기 기억에 반영하며, 과거의 쓰다듬기나 놀이를 추정해서 만들지 않습니다. 실제 사용자 저장 파일을 빌드·검사 입력으로 사용하지 않습니다.

| 기록 | 기존 위치·규칙 |
| --- | --- |
| 생일·성장·돌봄 | `%LOCALAPPDATA%\YeopjariCat\pet.ini`, `pet.bak`, 월별 `pet-YYYY-MM.ini`; schema 1 |
| 체형·식사 | `nutrition.ini`, `nutrition.bak`; 생일 일치 검사, 과거 기록에서 초기화하는 기존 로직 유지 |
| 캐릭터·의상 | `preferences.ini`, `preferences.bak`; schema 2, 기존 schema 1 읽기 유지 |
| 성격 | 저장된 생일에서 결정적으로 파생; 생일과 Trait/Mix/Classify 알고리즘을 보존해야 함 |
| 소리 | `sound-settings.ini`, `sound-settings.bak` |
| 업데이트 설정·알림 | `update-settings.ini`, `update-notice.ini` |
| 친밀도·기억·생활 패턴 | v1.10.0부터 `memory.ini`, `memory.bak`; schema 1, 기존 생일과 일치해야 함 |

원본의 체크섬·손상 파일 보존·백업·임시 파일 교체 로직을 유지합니다. 생일 파생 기본 성격은 변경하지 않고, 그 위에 최대 ±0.15의 학습 보정만 적용합니다. 새로운 기억은 원자적 파일 교체 전에 체크섬과 재읽기 결과를 확인합니다. 다른 세션이 갱신한 파일은 덮어쓰지 않습니다. 두 기억 파일 모두 읽을 수 없거나 생일이 다르면 원본을 보존하고 학습을 중단하며 기존 고양이 기능은 유지합니다.

메뉴의 성장 기록 내보내기는 기존 `pet.ini` 백업입니다. 기억·설정까지 옮기려면 앱을 종료하고 `%LOCALAPPDATA%\YeopjariCat` 전체를 복사해야 합니다. [v1.10.0 설계와 검증](V1_10_0.md)에 정확한 초기화·학습·시간 제한을 기록했습니다. 이후 스키마 변경도 이전 형식 fixture, 실패 시 원본 보존, 백업 복구 검사를 포함해야 합니다.

## 업데이트와 배포

`update-feed.txt`는 계속 EXE 옆에 둡니다. 공식 주소는 `https://github.com/cho-woo/yeopjari-cat/releases/latest/download/latest.txt`입니다. HTTPS, 앱 ID, stable 채널, 버전, 다운로드 크기·SHA256, EXE 제품 정보, 한국어 업데이트 창 검증과 교체·재시작 코드를 유지했습니다.

일반 CI는 읽기 권한으로 검사와 아티팩트 생성만 수행합니다. 새 기능 개발 시 버전 헤더·변경 내용을 함께 갱신하고 `tools/check.sh`를 통과시킵니다. 이 도구의 `release.py --verify`는 ZIP 내용, 실행 파일·매니페스트 해시, 버전, 피드 URL과 파일 구성을 검사합니다. 이미 공개된 버전의 파일은 덮어쓰지 않습니다.

v1.10.0 배포는 수동 실행 전용 `.github/workflows/publish-v1.10.0.yml`로 진행할 수 있습니다. 이 워크플로는 공식 저장소의 main에서만 실행되며, Linux 빌드·전체 테스트·ZIP/피드 검증 → Windows Server 2022에서 패키지 재검증·EXE `--self-test` 실행 → Release 게시 순서입니다. 게시 작업에만 `contents: write`를 부여하고 해당 작업의 `GITHUB_TOKEN`으로 업로드합니다. 초안·태그가 검사한 커밋을 가리키는지 확인하며, 생성된 `publish-github.ps1`이 모든 초안 자산을 다시 내려받아 해시를 검증한 뒤 공개합니다. 실패하면 공개 단계로 넘어가지 않습니다. 공개 후 공식 latest 피드와 모든 공개 자산을 다시 내려받아 검증합니다.

이 배포 워크플로는 v1.10.0으로 고정되어 있습니다. 이후 버전에는 버전과 배포 범위를 명시한 별도 변경이 필요합니다. 일반 코드 푸시가 자동으로 Release를 공개하지 않습니다.

Linux 크로스 빌드와 모델 테스트는 Windows 실기기 검사를 대신하지 않습니다. 실제 Windows 10/11에서 작업표시줄·창 발판·DPI/다중 모니터·소리·트레이·업데이트 중 교체/재시작과 기존 기록 복구를 확인해야 합니다. 아직 코드 서명은 없습니다.
