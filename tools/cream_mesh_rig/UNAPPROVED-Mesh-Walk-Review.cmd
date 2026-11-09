@echo off
chcp 65001 >nul
setlocal
if not exist "%~dp0YeopjariCat.exe" (
  echo 이 검토 실행기는 같은 폴더의 YeopjariCat.exe와 함께 사용하세요.
  pause
  exit /b 1
)
echo 미승인 메시 보행 검토본입니다. 자연스러운 보행 완성본이 아닙니다.
echo 기존 옆자리 고양이가 실행 중이면 고양이를 우클릭하여 종료한 뒤 다시 실행하세요.
start "" "%~dp0YeopjariCat.exe" --mesh-walk-review
