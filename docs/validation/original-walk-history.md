# 원래 보행의 출처와 복원 범위

검증일: 2026-11-05. 기존 공개 자료를 읽기만 했으며 과거 릴리스는 수정하지 않았다.

## 확인할 수 있는 최초 자료

- 최초 Git 커밋 `85aa427`에는 `README.md`만 있다. v1.0.0의 빌드 소스를 복원했다고 주장할 근거는 없다.
- GitHub 공개 릴리스 목록에서 가장 오래된 버전은 **v1.3.0**이다. v1.3–v1.7에는 배포 EXE/ZIP이 있지만 전체 소스가 없고, 현재 남아 있는 최초 전체 소스는 v1.8 아카이브다.
- `build-inputs/v1.8.0/yeopjari-src-1.8.0.tar.gz` SHA256: `ae2cd97554d040778dea89ff5efb93099923dd14d0a8eb45814e8cbc00f4e270`.
- `build-inputs/v1.9.0/yeopjari-src-1.9.0.tar.gz` SHA256: `9ae05050d4cf9222cfa05832d254e76f940a2d89a7464b4343c97669a1e9d78b`.
- v1.8 소스는 `3fd25c4`에서 최초 추적되었고 아카이브 교정 후 `2cfc62d`에 현재 파일이 남았다. v1.9 소스는 `d9dff73`에서 추가되었다.

v1.9 공개 실행 파일과 복원 빌드의 일치 범위는 [기존 복원 보고서](../V1_9_RESTORATION.md)에 별도로 기록되어 있다.

## 최초 공개 그림은 그대로 남아 있다

[v1.3.0 공개 EXE](https://github.com/cho-woo/yeopjari-cat/releases/download/v1.3.0/YeopjariCat-1.3.0.exe)를 다시 내려받았다. 크기 **10,158,592 bytes**, SHA256 **`15cee0930fda02bcad06941f210f8f35c73fd831a906121f3ca7ce4326bb5e83`**가 당시 배포 워크플로의 고정값과 일치한다. 그 안의 PNG 6개는 현재 에셋과 바이트 단위로 일치한다.

| 현재 파일 | v1.3.0 내 PNG와 현재 파일의 SHA256 |
| --- | --- |
| `assets/cat.png` | `f92c5b30167b8978252c812af65ac99ec5be1e7c79fffa1bf3d94810b25d9803` |
| `assets/cat-actions.png` | `01d3cb47c255b2e6394c154419c1a18719815135856eb7585e60153aab437bc5` |
| `assets/cat-silver.png` | `dda2643da439a790ee820c6a4b764eb56807a76f8c59292cc613342a80fd7db0` |
| `assets/cat-silver-actions.png` | `23b7932ab631a278fb0fc708d19f49256934d34015a2e91f20498dc2d6f8ceaf` |
| `assets/cat-jump-churu.png` | `ce44c6f4bc0e39c8c9f48e835d36537a2f097c1642df5f40e4edbf8c2f484507` |
| `assets/cat-gestures.png` | `70dd2d5bbeb8d9163dbf5b73563011646a655ab2f22a7df08a0b8f60f92332c6` |

크림/실버 보행 그림은 각각 기본 시트 첫 줄의 **0, 1, 2, 3번**이다. 현재의 얼굴/털 그림 자체가 새 그림으로 교체된 것이 원인은 아니다.

## v1.3.0 실행 파일에서 확인한 시간 기반 보행

소스는 없지만 공개 EXE에 함수 심볼이 남아 있다. `Render()`의 VA `0x140009871`은 `cat::animation::Evaluate(Motion, uint64, uint64, int)`의 VA `0x140015890`을 호출한다. 그 함수의 분기 표 VA `0x1400d1eb0`에서 `Motion::Walk` 인덱스 1은 VA `0x140015c00`으로 이동한다.

보행 구간 VA `0x140015c00`–`0x140015c9c`의 정수 나눗셈, 145 상수, 프레임 저장, 사인 및 보간 연산은 다음과 일치한다. 이는 **실행 파일에서 확인한 동작**이며 최초 버전 전체 소스를 확보했다는 뜻은 아니다.

```cpp
frame = (elapsed / 145) % 4;
blendFrame = (frame + 1) % 4;
blend = SmoothStep((elapsed % 145) / 145.0);
lift = (sin(ticks / 700.0) + 1) * .38
     + abs(sin(elapsed / 145.0 * pi)) * .8;
scaleX = scaleY = 1;
angle = 0;
```

보간은 전체 145 ms에 걸쳐 진행되며 한 주기는 580 ms다. 상수 VA `0x1400bd630`의 두 double은 `(145,145)`, `0x1400bd640`은 `(-2,.8)`이다. 공통 초기화에 사용하는 `0x1400bd330`, `0x1400bd338`, `0x1400bd340`은 각각 `700`, `1`, `.38`이다. v1.9에 남아 있는 4인자 시간 기반 `Evaluate`의 Walk 분기와 동일하다.

## v1.8/v1.9 실제 앱에서 사용한 거리 기반 보행

아카이브의 `src/cat_animation.hpp`에는 시간 기반 함수와 `Locomotion`을 받는 함수가 함께 있다. **실제 v1.9 앱은 후자를 호출한다**(`src/main.cpp:1203`). v1.8과 v1.9의 해당 오버로드 본문은 바이트 단위로 동일하다.

v1.9 `src/cat_animation.hpp:56`–`90`:

```cpp
distance = abs(postCollisionDistance / growth);
observedSpeed = min(500.0, distance / dt);
speed += (observedSpeed - speed) * -expm1(-dt / .075);
phase = fmod(phase + distance / 14.0, 1.0); // ordinary Walk
```

`src/main.cpp:1801`–`1811`은 Box2D의 업데이트 전후 X 차이를 성장 배율로 나눠 전달한다. 실제 이동이 없으면 보행 주기도 진행되지 않는다.

v1.9 `src/cat_animation.hpp:311`–`354`의 일반 걷기:

```cpp
activity = SmoothStep(speed / 22.0);
swing = sin(phase * 2 * pi);
bob = swing * swing;
framePhase = phase * 4;
frame = int(framePhase);
fraction = framePhase - frame;
blendFrame = -1;
blend = 0;
if (fraction > .6) {
    blendFrame = (frame + 1) % 4;
    blend = SmoothStep((fraction - .6) / .4);
}
lift = (sin(ticks / 700.0) + 1) * .38 + .8 * bob * activity;
scaleX = 1;
scaleY = 1 - .009 * bob * activity;
angle = direction * .6 * swing * activity;
```

한 주기는 14 body px이며 각 그림의 앞 60% 구간을 유지하고 뒤 40%만 다음 그림으로 보간한다. v1.3의 145 ms 전체 보간과 혼동하면 안 된다.

## 원래 그림 배치

v1.9 아카이브 `src/main.cpp:727`–`785`, `1066`–`1132`, `1196`, `1240`–`1252`가 기준이다.

- 알파가 16보다 큰 픽셀을 기준으로 각 셀을 자르고 주변 2 px를 포함한다. 기본 시트의 첫 6개 프레임 중 가장 큰 높이를 `standingHeight`로 삼는다.
- 배율은 `110 * DPI * growth / standingHeight`. **각 걷기 프레임을 동일 크기로 강제로 정규화하지 않는다.** 각 원본 사각형의 폭/높이에 공통 배율과 저장된 체형 배율을 곱한다.
- 각 프레임은 중심 X와 발 아래 기준점에 맞춰 그린다. `bottom = surface.height - 1 - pose.lift * DPI * growth - physical.lift * DPI`.
- 회전이 있을 때 `abs(sin(angle)) * renderedWidth / 2`만큼 발 아래 기준점을 보정하고 전체 이미지를 회전한다. 다리만 늘리거나 분리해 합성하지 않는다.
- 원본 두 프레임의 premultiplied BGRA를 직접 선형 보간한다. 두 이미지를 각각 반투명으로 겹치는 방식이 아니다.

## 이후 변경 순서

| 커밋/버전 | 일반 걷기에 대한 변경 |
| --- | --- |
| `bfd204d`, 소스 복원/v1.9.1 준비 | 일반 Walk의 프레임/주기/보간은 아카이브와 동일하게 유지 |
| `ee3e6c4`, v1.10.1 작업 | 전체 이미지의 호흡 높이와 `.8*bob` 높이를 제거. 4개 그림, 14 px 주기, 작은 세로 배율/회전은 유지 |
| `e1f7d51`, v2.0.1 | 일반 Walk 주기를 14→44 body px로 변경. 전체 이미지 배율/회전을 제거 |
| `da08a7e`, v2.0.2 | 4개 그림 순환을 제거하고 **1번 그림 한 장의 다리를 변형하는 `walkingRig`**로 교체. 주기는 28 body px |
| `50c3231`, v2.0.3 | 같은 합성 보행 계열의 접지/커서 동작 수정 |
| `30737e9`, v2.0.4 | 합성 보행의 네 발/접지 처리 변경 |
| `87ec4ec`, v2.0.5 | 같은 합성 보행의 발 순서/형상/기준점 변경 |

이 기록은 변경의 출처다. 사용자가 느낀 모든 어색함을 단일 커밋의 결함으로 단정하거나 실제 사용자 PC에서 시각 검증한 것으로 취급하지 않는다.

## 이번 복원의 명시적인 예외

일반 걷기는 크림/실버의 원본 4개 그림과 v1.8/v1.9 거리 기반 주기/보간을 복원한다. 단, **현재 Box2D 지지면 기준을 유지하기 위해 원본의 전체 이미지 bob, 회전, 세로 크기 변동은 되살리지 않는다.** 따라서 과거 버전의 픽셀 단위 완전 재현이 아니라 원본 보행 그림/주기 복원이다. 원본 4개 보행 그림이 없는 나중에 추가된 동물은 별도 경로를 유지한다. 일반 Walk 외 행동이나 저장 형식/업데이트 기능을 과거 버전으로 되돌리는 작업이 아니다.

## 재현 방법

```bash
git ls-tree -r --name-only 85aa427
sha256sum build-inputs/v1.8.0/yeopjari-src-1.8.0.tar.gz build-inputs/v1.9.0/yeopjari-src-1.9.0.tar.gz
tar -xOzf build-inputs/v1.9.0/yeopjari-src-1.9.0.tar.gz src/cat_animation.hpp
tar -xOzf build-inputs/v1.9.0/yeopjari-src-1.9.0.tar.gz src/main.cpp
git diff 4e020ad e1f7d51 -- src/cat_animation.hpp
git diff e1f7d51 da08a7e -- src/cat_animation.hpp
curl -fL -o /tmp/YeopjariCat-1.3.0.exe https://github.com/cho-woo/yeopjari-cat/releases/download/v1.3.0/YeopjariCat-1.3.0.exe
sha256sum /tmp/YeopjariCat-1.3.0.exe
objdump -d --start-address=0x140015c00 --stop-address=0x140015ca1 /tmp/YeopjariCat-1.3.0.exe
```

PNG 재비교는 아래 코드로 수행할 수 있다. PNG signature부터 IEND까지의 바이트열을 비교하므로 이미지를 다시 인코딩하지 않는다.

```python
from pathlib import Path
import hashlib

exe = Path('/tmp/YeopjariCat-1.3.0.exe').read_bytes()
assets = {hashlib.sha256(p.read_bytes()).hexdigest(): p.name
          for p in Path('assets').glob('*.png')}
signature = b'\x89PNG\r\n\x1a\n'
position = 0
while (start := exe.find(signature, position)) >= 0:
    end = start + len(signature)
    while end + 12 <= len(exe):
        size = int.from_bytes(exe[end:end + 4], 'big')
        kind = exe[end + 4:end + 8]
        end += size + 12
        if end > len(exe):
            break
        if kind == b'IEND':
            digest = hashlib.sha256(exe[start:end]).hexdigest()
            print(digest, assets.get(digest, 'unmatched'))
            break
    position = start + len(signature)
```
