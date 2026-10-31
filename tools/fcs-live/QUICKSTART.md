# 실물 FCS + fake RDR + GitHub 원본 GUI 테스트

학교 실습용 저출력 레이저 포인터 시연 환경의 Windows ECS 실행 가이드입니다.
이 경로는 `tools/fcs-live`의 독립 C++ 프로그램을 빌드하며 Protobuf를 사용하지 않습니다.
저장소 루트의 기존 서버·더미 실행 경로와 구분해서 실행하세요.

## 1. 준비

- Windows 10/11, Git
- Visual Studio 2022/2026의 **C++ 데스크톱 개발**, Windows SDK, CMake 도구
- .NET 10 SDK: GitHub WPF GUI 빌드에 사용
- Python 3.10 이상, Tkinter: `python -c "import tkinter; print('Tk OK')"`
- 실제 FCS에 EF/FE ICD 펌웨어가 실행돼 있어야 합니다.

명령은 저장소 루트의 **Developer PowerShell for Visual Studio**에서 실행합니다.
`cmake`가 없으면 Visual Studio Installer에서 C++ CMake 도구를 설치하세요.
Python 외 추가 pip 패키지는 필요하지 않습니다.

## 2. 소스 및 빌드

```powershell
git clone --branch codex/live-fcs-test-guide https://github.com/LIG-Embedded-School-Team-2-WarWarden/c2.git
cd c2
cmake -S tools/fcs-live -B build/fcs-live-original-gui -A x64
cmake --build build/fcs-live-original-gui --config Release

git clone https://github.com/LIG-Embedded-School-Team-2-WarWarden/c2-GUI.git build/gui-upstream-source
git -C build/gui-upstream-source checkout --detach fe22d07ded6258075c5260f45eb1b7747a131e70
dotnet build build/gui-upstream-source/AadcCommandCenter.csproj -c Release -o build/gui-upstream-bin
```

GUI는 확인된 GitHub 원본 커밋을 수정 없이 빌드합니다. GUI 저장소의 소스를 고치거나
다른 버전의 IPC 구조체를 섞지 마세요. 이후 GUI 버전 변경 시 IPC 규격을 먼저 비교합니다.
이 테스트 경로는 서브모듈 초기화 없이 빌드할 수 있습니다.

## 3. 유선 IP와 포트

| 구간 | 기본 주소 | 역할 |
|---|---|---|
| ECS → 실제 FCS | PC `10.0.0.105/24` → 보드 `10.0.0.3:2000` | UDP, EF/FE Big Endian |
| fake RDR | `127.0.0.1:1001` | 같은 PC의 표적 편집기 |
| ECS의 fake RDR 수신 | `127.0.0.1:11000` | 시뮬레이터 전용 UDP 규격 |
| ECS ↔ GUI | 공유 메모리 / Named Pipe v2 | 같은 PC의 로컬 IPC |

PC 유선 어댑터에 실제로 할당된 IPv4를 사용하세요.

```powershell
Get-NetIPAddress -AddressFamily IPv4 | Select-Object InterfaceAlias,IPAddress,PrefixLength
```

PC와 FCS는 같은 서브넷의 **서로 다른 IP**여야 합니다. 같은 네트워크에서 다른 장비가
이미 쓰는 IP를 지정하지 마세요. 예시 IP를 새 PC에 자동 할당하는 스크립트는 아닙니다.
직접 연결한 테스트망은 보통 기본 게이트웨이 없이 사용하며, 기존 Wi-Fi 기본 경로는 유지합니다.

ECS가 EF01 연결 요청을 먼저 보냅니다. ECS의 FCS용 로컬 UDP 포트는 자동 배정됩니다.
보드의 연결 소유자가 IP와 포트 조합이므로 같은 보드를 제어하는 다른 ECS는 종료하세요.
fake RDR의 두 고정 포트를 쓰는 기존 테스트 인스턴스도 중복 실행하지 않습니다.

## 4. 세 프로그램 실행

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/start_live_fcs_test.ps1 `
  -EcsAddress 10.0.0.105 -FcsAddress 10.0.0.3 -FcsPort 2000
```

fake RDR 편집기, ECS 콘솔, 원본 GUI가 열립니다. 시작 시 ECS는 연결·상태 요청만 보냅니다.
fake RDR에는 기본 표적 1개가 생성됩니다. 실제 보드 없이 실행하면 GUI의 FCS 사용 가능
상태는 NO인 것이 정상입니다.

1. fake RDR의 연결 및 ECS 수신 확인 수치가 증가하는지 확인합니다.
2. GUI에서 표적이 보이고 FCS 연결·사용 가능이 YES인지 확인합니다.
3. 표적 할당을 한 번 누릅니다. ECS가 EF03을 보내고 실제 FE02 위치 피드백으로 완료를 판정합니다.
4. 레이저 데모가 필요하면 할당 완료 후 발사 버튼을 한 번 누릅니다.
5. 취소는 EF04 OFF를 전송합니다. 종료 전 활성 데모는 취소하고 세 창을 닫습니다.

현재 **발사 버튼은 EF05만 전송**합니다. EF04 ON → EF05 연속 전송은 구현돼 있지 않습니다.
확인한 `firmware/icd-fcs`는 EF05 안에서 레이저를 끄고 시작점으로 이동한 뒤 켜는 방식입니다.
보드가 별도 EF04 ON을 요구하는 버전이면 먼저 양쪽 구현을 맞춰야 합니다.
EF03 일반 위치 이동에는 이 ICD의 별도 정지 명령이 없습니다.

ECS 콘솔의 Ctrl+C 또는 해당 세션의 `stop` 파일로 ECS를 종료할 수 있습니다.
ECS 종료만으로 보드의 동작 취소가 확인되는 것은 아닙니다.

```powershell
# 시작 스크립트가 출력한 session_folder를 사용하세요.
Set-Content '<session_folder>/stop' 'stop'
```

## 5. 로그와 거부 사유

매 실행마다 `build/live-fcs-sessions/<세션ID>/`에 다음 파일을 만듭니다.

- `session.json`: IP·포트, 프로세스 ID, 로그 경로
- `status.json`: 최근 연결·준비·flags·표적 수·내부 할당 상태와 사유
- `events.log`: 명령 응답, 할당 전환, 상태 응답 나이, 표적 유효성, 시간 초과
- `rdr.log`, `rdr.stderr.log`: fake RDR 실행 기록

```powershell
Get-Content '<session_folder>/status.json'
Get-Content '<session_folder>/events.log' -Tail 40 -Wait
```

`events.log`의 첫 값은 Unix epoch 밀리초입니다. GUI 로그는
`%LOCALAPPDATA%/AadcCommandCenter/event-log.txt`에 있습니다.

FCS `flags`는 비트 조합입니다: 연결 1, 이동 2, 레이저 ON 4, 데모 8, 오류/준비 미완료 16.
`1`은 연결·대기, `11`은 레이저 OFF인 데모 이동, `15`는 레이저 ON인 데모 이동,
`17`은 연결됐지만 오류/준비 미완료입니다. ON 플래그는 광출력 센서 측정이 아닙니다.

내부 사유 코드: 7 FCS 상태 대기, 8 표적 대기, 9 좌표 변경, 10 장치 오류/준비 미완료,
11 정렬 미확인, 12 동작 중, 13 응답 시간 초과(결과 불명), 14 장치가 명령 값 거부.

일시 유실은 확인 대기로 두고 **새 상태·동일 표적·정렬을 확인한 뒤** 복귀합니다.
복귀 자체가 이동·레이저 명령을 보내지는 않습니다. 표적 좌표 변경(축별 1cm 이상),
장치 오류, 정렬 상실은 명시적인 재할당이 필요합니다. 응답이 없는 데모 요청은 실행
여부를 모르는 상태이므로 취소 확인 전 자동 재전송하지 않습니다.

원본 GUI는 기존 결과 코드 0..6, 할당 상태 0..3만 지원합니다. ECS는 대기 상태를
기존 조정 중 상태로, 상세 오류를 기존 오류 범주로 변환합니다. **구체적 사유는 ECS 로그**를
확인하세요. GUI 원본의 상단 버튼 문구가 하단 할당 상태와 다를 수 있으며, GUI 소스는 수정하지 않았습니다.

## 6. 장비 없이 회귀시험

```powershell
dotnet build tools/fcs-live/tests/GuiProbe.csproj -c Release -o build/fcs-live-original-gui/probe
python tools/fcs-live/test_live_gui.py
python tools/fcs-live/test_live_gui.py --original
```

시험은 실제 보드 대신 루프백 fake FCS·RDR, 별도의 UDP 포트와 IPC 이름을 사용합니다.
연결·표적 일시 유실 복귀, 원본 각도 전달, 좌표 변경·장치 오류 차단, 재할당,
데모 완료·취소, 결과 불명 시 재시도 차단 및 로그 생성을 검증합니다.
GUI 저장소의 수정본이나 로컬 비공개 디렉터리가 필요하지 않습니다.

## 7. 좌표계와 범위

fake RDR 입력은 전방/좌측/위쪽(m)이며, RDR과 FCS의 원점을 같다고 가정한 시험입니다.
명령 각도는 `pan=180+atan2(y,x)`, `tilt=atan2(z,hypot(x,y))`를 정수 도로 변환합니다.
기본 범위는 pan 120~240°, tilt 0~60°입니다. 실제 설치 오프셋·축 보정은 포함하지 않습니다.

FCS가 보낸 방위각은 **원본 그대로 GUI에 전달**합니다. 이전 `315-방위각` 표시 보정은 제거했습니다.
이는 목표 좌표에서 명령 각도를 계산하는 식의 변경과는 별개입니다. 원본 GUI의 방향 그림은
자체 좌표 기준을 사용하므로, 도형 방향과 실제 설치 방향을 동일하다고 가정하지 마세요.
fake RDR은 시험용 draft v1이며 실제 RDR의 ED/DE 규격 구현은 아닙니다.
