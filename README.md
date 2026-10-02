# C2

MFS 시연 체계의 C++20 기반 통제소 서버다. 임의 개수의 관측·타격 자산을
동적으로 등록하고, 관측 결과를 전역 트랙으로 관리하며, 안전 조건을 만족하는
타격 자산을 자동 또는 수동으로 할당한다. UDP/Protobuf v3 메시지 계약은
`protocol/mfs.proto`, 설계 결정은 `docs/decisions/`에서 관리한다.

## 좌표계 책임

1. 관측 자산은 센서 로컬 좌표를 `PROJECT_FRAME` 월드좌표로 변환한다.
2. 감시/관측 자산은 변환한 표적 위치와 속도를 측정시각과 함께
   `TargetCoordinate`로 송신한다. 두 용어는 같은 wire role을 뜻한다.
3. 통제소는 관측 자산 `AssetPose`를 표적 좌표에 다시 적용하지 않는다.
4. 통제소는 최신 표적의 위치·속도·측정시각을 보존하고 할당된 타격 자산에
   `TargetTrackUpdate`로 전달한다. 정상 자동 추적 경로에서 Pan/Tilt를 계산하지 않는다.
5. 타격 자산은 자체 `AssetPose`와 현재시각으로 dead reckoning한 뒤 상대좌표와
   Pan/Tilt를 계산하고, START 이후 고주기 제어 루프로 연속 지향한다.

`OBSERVATION_FRAME`은 센서나 관측 터렛에 고정된 로컬 좌표계다. 센서 원시값을
해석하고 보정하는 자산 내부 경계에서만 사용한다. C2 인터페이스의
`TargetCoordinate`와 모든 `AssetPose`는 `PROJECT_FRAME`이어야 하며, 다른 프레임은
검증 단계에서 거부한다.

속도 단위는 m/s이고 위치 단위는 m, 시각 단위는 UTC microseconds다. 정지 표적은
`velocity_valid=true`와 0 속도로 표현한다. 속도 미제공은 `velocity_valid=false`이며
정지로 간주하지 않고 자동 연속 추적 스트림을 만들지 않는다. `duration_ms`는 타격
출력 지속시간일 뿐 추적 종료시간이 아니다. 추적은 STOP/ESTOP, 표적 만료, 예측시간
초과, Pose/통신 상실, 비정상 값 또는 터렛 한계 초과 때 안전 정지한다.

관측 더미의 `--target-x`, `--target-y`, `--target-z`와 `--vx`, `--vy`, `--vz`는
이미 `PROJECT_FRAME`으로 준비된 표적 데이터다. 더미는 이 값을 주기적으로 송신할
뿐이며 LiDAR 로컬 좌표 해석, 센서 장착 오프셋 또는 Pan/Tilt 보정은 수행하지 않는다.
실제 관측 자산은 장착 오프셋을 포함한 좌표변환을 완료한 뒤 같은 메시지를 송신해야 한다.

## 다중 자산 모델

- 자산은 `asset_id`로 식별하고 프로세스가 시작될 때마다 새 `session_id`를 만든다.
- 자산은 공용 UDP 포트 5000으로 `AssetRegistration`을 전송하고 실제 송신 IP와
  광고한 command port를 조합해 명령 Endpoint를 만든다.
- 등록 갱신, lease, Heartbeat, 상태 최신성을 각각 검사한다. 같은 자산의 새 세션이
  등록되면 이전 세션의 pending 명령과 할당을 종료하고 늦게 도착한 패킷을 거부한다.
- 서로 다른 관측 자산의 같은 detection ID는 서로 다른 64비트 전역 track ID가 된다.
- 자동 할당은 연결, lease, Pose, 최신 `EffectorStatus`, fault/attack 상태, capability,
  Pan/Tilt 도달 가능성, 독점 작업 여부를 먼저 검사한 뒤 거리·회전량·부하 점수로
  결정한다. 동점이면 작은 asset ID를 선택한다.
- 공격 시작 전 자산이 사라지면 재할당할 수 있다. 공격 시작 후에는 자동 재할당하지
  않고 `operator_action_required` 상태로 남긴다.

현재 공용 수신 포트와 동적 등록이 기준 경로다. 기존 역할별 포트와 단일 자산 명령
문법은 마이그레이션 시험을 위한 호환 경계로만 유지한다.

## 빌드 및 시험

```powershell
msbuild c2.slnx /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
.\x64\Release\C2.Tests.exe
.\scripts\run_udp_smoke.ps1 -BinDir .\x64\Release
```

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

GitHub Actions는 모든 push와 PR에서 Windows Release 빌드, 전체 테스트와 다중
프로세스 UDP 스모크를 수행한다.

## 서버 실행과 네트워크

```powershell
.\x64\Release\C2.Server.exe --bind 0.0.0.0 --asset-port 5000
```

| 방향 | 기본 포트 | 기준 용도 |
|---|---:|---|
| 모든 자산 → C2 | 5000 | 등록·해제, Heartbeat, Pose, 상태, 표적, ACK, 오류 |
| C2 → 각 자산 | 등록값 | 등록 시 광고한 자산별 command port |

호환 모드에서는 관측 상태 5001, 표적 5002, 관측 명령 5101, 타격 명령 6001,
타격 상태 6002도 사용한다. 이 포트는 새 자산 배치의 기준이 아니다.

| 옵션 | 기본값 | 의미 |
|---|---:|---|
| `--asset-port` | 5000 | 동적 자산 공용 수신 포트 |
| `--target-validity-ms` | 2000 | 표적/전역 트랙 유효시간 |
| `--max-targets` | 256 | 전체 표적/트랙 상한 |
| `--max-tracks-per-observer` | 64 | 관측 자산 하나의 트랙 상한 |
| `--max-assets` | 256 | 등록 자산 상한 |
| `--heartbeat-interval-ms` | 1000 | C2 Heartbeat 송신 주기 |
| `--heartbeat-timeout-ms` | 3000 | 자산 통신 단절 판단시간 |
| `--retired-retention-ms` | 60000 | 해제·만료 자산 안전명령 Endpoint 보존시간 |
| `--max-registration-lease-ms` | 60000 | 자산이 광고할 수 있는 최대 등록 lease |
| `--status-timeout-ms` | 1000 | 타격 상태 최신성 제한 |
| `--command-validity-ms` | 500 | 생성 명령 유효시간 |
| `--ack-timeout-ms` | 200 | delivery ACK 전 재전송 대기시간 |
| `--completion-timeout-ms` | 2000 | 진행 ACK 후 완료 대기시간 |
| `--command-attempts` | 3 | 최초 송신을 포함한 최대 시도 횟수 |
| `--max-pending-commands` | 1024 | 전체 pending 명령 상한 |
| `--max-pending-per-asset` | 64 | 자산별 pending 명령 상한 |
| `--max-command-outcomes` | 1024 | 명령 종결 이력 상한 |
| `--max-assignments` | 256 | 할당 이력 상한 |
| `--max-error-history` | 128 | 자산 ErrorReport 이력 상한 |
| `--max-inbound-rejections` | 128 | C2 수신 거부 이벤트 이력 상한 |
| `--distance-weight` | 1 | 할당 거리 점수 가중치 |
| `--rotation-weight` | 1 | 할당 회전량 점수 가중치 |
| `--assignment-weight` | 100 | 기존 작업 수 penalty 가중치 |
| `--degraded-penalty` | 500 | degraded 상태 penalty |
| `--failure-weight` | 25 | 최근 명령 실패 penalty 가중치 |
| `--auto-reassign` | true | 공격 시작 전 할당 상실 시 자동 재할당 |
| `--emergency-stop-repetitions` | 3 | 비상정지 즉시 반복 횟수 |

전체 한도보다 큰 자산별 한도, 0, 범위를 벗어난 포트와 시간 변환 오버플로는
서버 시작 전에 거부한다.

## 콘솔 명령

| 명령 | 의미 |
|---|---|
| `assets` | 자산·세션·Endpoint·연결·Pose·capability·상태·할당 조회 |
| `targets` | 전역 track ID, 원 관측 자산/detection ID와 좌표 조회 |
| `dev-pose ASSET_ID X Y Z AZIMUTH_DEG` | 개발용 자산 위치(m)·설치 방위(deg) 설정 |
| `scan OBS_ID PAN TILT` | 지정 관측 자산이 해당 방향에서 탐색 시작 |
| `observe OBS_ID PAN TILT` | 지정 관측 자산을 절대 Pan/Tilt로 지향 |
| `obs-stop OBS_ID` / `obs-home OBS_ID` | 관측 탐색 정지 / 원점 복귀 |
| `assign TRACK_ID [EFFECTOR_ID]` | 자동 또는 수동 타격 자산 할당 |
| `unassign TRACK_ID` | 공격 전 할당 해제 |
| `point TRACK_ID` | 수동/진단 호환 경로로 일회성 지향 명령 송신 |
| `arm TRACK_ID` | 현재 할당·상태·지향 일치 검증 후 공격 준비 |
| `start TRACK_ID DURATION_MS` | 무장·정렬·표적 일치 검증 후 공격 시작 |
| `stop EFFECTOR_ID` | 보존 Endpoint를 포함해 특정 타격 자산 정지 |
| `estop EFFECTOR_ID` / `estop-all` | 특정/전체 알려진 타격 자산 비상정지 |
| `status` | 자산·트랙·할당·pending 명령 요약 |
| `errors` | 자산이 송신한 오류 이력 조회 |
| `outcomes` | 완료·거부·실패·만료·재시도 소진·세션 종료 결과 조회 |
| `events` | 등록·인증·Endpoint/session·상태 갱신 거부 이벤트 조회 |
| `quit` | 서버 정상 종료 |

일반 명령은 등록 lease, Heartbeat 연결, 새 세션의 `AssetPose` 재동기화와 상태
최신성을 요구한다. STOP과 ESTOP은 안전 우선 명령이므로 연결이 끊긴 보존 자산에도
전송한다. ESTOP 반복 패킷은 동일 command ID를 사용한다.

## 개발용 위치·방위 설정

실행 중인 통제소 콘솔에서 `dev-pose 201 10 20 1.5 90`을 입력하면 자산 201의
설치 위치와 방위를 변경한다. 두 종류의 더미 자산 모두 지원하며 `PROJECT_FRAME`
위치(m), +X에서 +Y 방향으로 증가하는 설치 방위(deg, 0 이상 360 미만)를 사용한다.
설치 방위는 현재 터렛 Pan/Tilt와 구분되며 이 명령은 모터를 이동시키지 않는다.

등록과 Heartbeat 연결, `development_pose` capability(bit 3, 값 8)가 필요하다.
초기 Pose가 없어도 설정할 수 있지만 미등록·단절 자산에는 보내지 않는다.
관측 자산은 STANDBY이며 탐색·축 이동이 중단되어야 하고, 포인터 자산은
STANDBY이며 출력·무장·자동 추적이 중단되어야 한다. C2의 활성 할당이 있으면
`unassign`으로 해제해야 한다. 필요하면 먼저 `obs-stop` 또는 `stop`을 실행한다.

`sent`는 송신 결과다. 적용 성공은 `outcomes`의 완료 ACK와 `assets`에 재보고된
Pose로 확인한다. C2는 송신만으로 자체 Pose를 덮어쓰지 않는다. 값은 자산 내부에
원자적으로 적용되어 이후 주기 보고에도 유지되고, 자산 재시작 시 실행 옵션값으로
돌아간다. 재시작 후 새 세션에는 자동 재적용하지 않는다. 실제 GPS 등의 자동 추정과
전환하는 계약은 이번 기능에 포함하지 않는다. 상세 ICD는
`docs/icd/development-pose-command.md`에서 관리한다.

## 다중 더미 자산 시연

동적 모드는 `--asset-id`를 지정한다. `--listen-port 0`이면 OS가 자산별 command
port를 배정하고 등록 메시지로 광고한다.

```powershell
.\x64\Release\C2.Server.exe --asset-port 5000
.\x64\Release\Dummy.Observation.exe --asset-id 101 --listen-port 0 --c2-port 5000 --x 0 --y 0
.\x64\Release\Dummy.Observation.exe --asset-id 102 --listen-port 0 --c2-port 5000 --x 100 --y 0
.\x64\Release\Dummy.Effector.exe --asset-id 201 --listen-port 0 --c2-port 5000 --x 10 --y 0
.\x64\Release\Dummy.Effector.exe --asset-id 202 --listen-port 0 --c2-port 5000 --x 50 --y 0
```

더미는 capability, 등록 주기/lease, 위치, 설치 방위각, Pan/Tilt 한계, 상태·Heartbeat
주기와 watchdog을 옵션으로 받는다. 관측 더미는 `--vx`, `--vy`, `--vz`,
`--velocity-valid`와 표적 송신 주기를 설정할 수 있다. 타격 더미의 내부 제어 루프는
약 10 ms, 최대 dead-reckoning 구간은 2 s, Pose 최신성 한계는 3 s다.
`--asset-id`를 생략한 실행은 기존 고정 포트 호환 모드다.

자동 시험은 관측 2대와 타격 3대를 실행해 동일 detection ID의 전역 트랙 분리,
자동 할당, 이동표적 스트림, POINT/ARM/START, 출력시간 이후 연속 추적, 전체 ESTOP,
동일 asset ID 재시작에 따른 session 교체를
검증한다. 고정 시간만 기다리지 않고 제한시간 내 서버 상태와 명령 결과를 반복 확인해
준비 완료·세션 교체·pending 종료 조건을 판정한다.

## 안전·보안 경계와 남은 실제 장비 작업

현재 Endpoint 신뢰는 등록 패킷의 실제 source IP와 이후 패킷의 IP/asset/session
일치 검사에 기반한다. 암호학적 자산 인증, DTLS/IPsec, 키 배포, anti-spoofing,
권한별 공격 승인과 감사 로그는 실제 배치 전 별도 보안 설계가 필요하다.

더미 자산은 ICD와 C2 안전 상태전이를 검증하는 시험 대역이다. 실제 LiDAR SDK,
calibration/탐지 알고리즘, 모터·encoder·limit·레이저 출력, GUI, 실제 IP와 시간 동기화,
지향 오차와 End-to-End 성능 승인 기준은 완료 근거에 포함하지 않는다. GUI는 C2 코어와
별도 프로세스로 구성할 수 있지만, 그 프로세스 간 API/IPC 계약은 아직 확정하지 않았다.
