# C2

MFS 시연 체계의 C++20 기반 통제소 서버다. UDP/Protobuf 수신, 자산·표적 상태
관리, 관측 터렛 제어, 타격 지향 계산 및 공격 안전조건 검사를 제공한다.

## 좌표계 책임

1. 관측 자산이 센서·터렛 로컬 좌표를 `PROJECT_FRAME` 월드좌표로 변환한다.
2. 관측 자산이 변환된 좌표를 `TargetCoordinate`로 전송한다.
3. 통제소는 관측 자산 `AssetPose`를 표적에 다시 적용하지 않는다.
4. 통제소는 타격 자산 `AssetPose`만 사용해 상대좌표와 Pan/Tilt를 계산한다.
5. 계산 결과를 `EffectorTurretCommand`로 전송한다.

버전 관리되는 메시지 계약은 `protocol/mfs.proto`, 결정 근거는
`docs/decisions/`에서 관리한다.

## 빌드 및 시험

Visual Studio Developer PowerShell에서 다음을 실행한다.

```powershell
msbuild c2.slnx /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
.\x64\Release\C2.Tests.exe
```

CMake를 사용할 수도 있다.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

GitHub Actions는 모든 push와 PR에서 Windows Release 빌드 및 전체 테스트를 수행한다.

## 서버 실행

```powershell
.\x64\Release\C2.Server.exe `
  --bind 0.0.0.0 `
  --observation-ip 127.0.0.1 `
  --effector-ip 127.0.0.1
```

기본 UDP 포트는 ICD 초안과 같다.

| 방향 | 포트 | 용도 |
|---|---:|---|
| 관측 → 통제소 | 5001 | AssetPose, ObservationStatus, Heartbeat |
| 관측 → 통제소 | 5002 | TargetCoordinate |
| 통제소 → 관측 | 5101 | ObservationTurretCommand, Heartbeat |
| 통제소 → 타격 | 6001 | EffectorTurretCommand, AttackCommand, Heartbeat |
| 타격 → 통제소 | 6002 | AssetPose, EffectorStatus, CommandAck, Heartbeat |

모든 주소와 포트는 `--bind`, `--observation-ip`, `--effector-ip`,
`--observation-status-port`, `--target-port`, `--observation-command-port`,
`--effector-command-port`, `--effector-status-port` 옵션으로 변경할 수 있다.

## 콘솔 명령

| 명령 | 의미 |
|---|---|
| `scan PAN TILT` | 지정 방향을 중심으로 관측 탐색 시작 |
| `observe PAN TILT` | 관측 자산을 절대 Pan/Tilt로 지향하되 탐색은 시작하지 않음 |
| `obs-stop` | 관측 터렛 구동과 탐색 중지 |
| `obs-home` | 관측 탐색을 중지하고 원점 복귀 |
| `targets` | 유효한 표적 ID, PROJECT_FRAME 좌표와 신뢰도 표시 |
| `status` | 두 자산 연결 및 관측·타격 상태 표시 |
| `errors` | 수신한 오류 이력 표시 |
| `point TARGET_ID` | 최신 표적 좌표로 타격 자산 지향 |
| `arm TARGET_ID` | 안전조건 확인 후 공격 준비 |
| `start TARGET_ID DURATION_MS` | 무장·정렬·표적 일치 확인 후 공격 시작 |
| `stop` | 공격 중지 |
| `estop` | 연결 상태와 무관하게 비상정지 전송 |
| `quit` | 서버 정상 종료 |

일반 명령은 해당 자산의 Heartbeat 연결과 재연결 이후 `AssetPose` 재동기화가
완료되어야 전송된다. `STOP`과 `EMERGENCY_STOP`은 안전 우선 명령이므로 이 조건을
우회한다. 일반 명령은 200ms 안에 ACK가 없으면 재전송하며 최초 전송을 포함해 최대
3회 시도한다. `EMERGENCY_STOP`은 ACK 대기 없이 동일 명령을 즉시 3회 전송한다.
timeout, 장비 한계 및 주소는 현재 ICD의 TBD 항목이므로 런타임 설정 확장이
필요한 배포 환경에서는 확정값으로 교체해야 한다.

## 더미 자산 통합 시연

빌드 후 PowerShell 창 세 개에서 다음 프로그램을 각각 실행한다.

```powershell
.\x64\Release\Dummy.Observation.exe
.\x64\Release\Dummy.Effector.exe
.\x64\Release\C2.Server.exe
```

서버에서 `scan 10 5`, `point 1`, `arm 1`, `start 1 100`을 차례대로 입력하면
탐색부터 타격 동작 종료까지 로컬 UDP 전체 흐름을 확인할 수 있다. 더미 관측 자산은
1번 표적의 PROJECT_FRAME 좌표를 생성하며, 두 더미 자산은 `quit`으로 종료한다.
시연 기본값과 문서의 TBD를 구현하면서 내린 결정은
`docs/decisions/ADR-002-runtime-defaults-and-command-identity.md`에 정리되어 있다.
요구사항별 구현·시험 근거와 실제 장비에 남은 항목은
`docs/traceability/ICD-RTM.md`에서 확인할 수 있다.
