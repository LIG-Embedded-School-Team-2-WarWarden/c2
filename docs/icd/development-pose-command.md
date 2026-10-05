# ICD 개발용 자산 위치·방위 설정

- 기준: MFS-ICD-001 / Protocol v3 선택적 확장
- 갱신일: 2026-10-02
- 요구사항: SW-C2-014 / 인터페이스: IF-C2-ASSET-DEV-001
- 추적 연결: `C2-IMPL-014` (`docs/traceability/ICD-RTM.md`). `SW-C2-014`는 ICD 보충 식별자이며 SRS 정식 표의 추가·승인 여부는 별도 결정이다.
- 목적: GPS 등의 자체 위치·방위 추정을 대신하여 통제소에서 개발용 설치 Pose를 설정한다.

## 입력 및 방향

통제소 콘솔: `dev-pose ASSET_ID X Y Z AZIMUTH_DEG`

예: `dev-pose 201 10 20 1.5 90`

관측·포인터 자산 모두 대상으로 한다. 설치 위치는 기존 PROJECT_FRAME의 x/y/z(m),
설치 방위는 +X를 0도로 하고 +Y 방향으로 증가하는 degree 값이다. GPS 위경도나
군용 표시 단위를 직접 입력하지 않는다. 유효 범위는 유한한 위치와 [0, 360) 방위다.
360, 음의 방위, NaN/Inf, 누락·추가 인수, asset_id=0을 거부한다.
이 값은 자산 전체의 설치 Pose이며 터렛 현재 Pan/Tilt나 모터 지향 명령이 아니다.

## 메시지 계약

기존 `AssetPose`(자산 → C2)는 변경하지 않는다. 설정 요청은 C2 → 자산 command
Endpoint의 `DevelopmentPoseCommand`를 사용하며 `Envelope` oneof 번호는 14다.

| 필드 번호 | 필드 | 형식 | 규칙 |
|---|---|---|---|
| 1 | header | MessageHeader | source=C2, destination=자산 역할, 현재 asset/session |
| 2 | command_id | uint32 | 0 금지, 해당 역할의 기존 명령 ID 공간 공유 |
| 3 | coordinate_frame | CoordinateFrame | PROJECT_FRAME만 허용 |
| 4 | x_m | float | 유한한 값, m |
| 5 | y_m | float | 유한한 값, m |
| 6 | z_m | float | 유한한 값, m |
| 7 | azimuth_deg | float | 유한한 값, 0 이상 360 미만 |
| 8 | valid_until_us | uint64 | 생성 시각보다 뒤, UTC microseconds |

`AssetRegistration.capabilities` bit 3(값 8, `development_pose`)로 지원을 광고한다.
기존 protocol_version=3과 필드 번호는 유지하는 선택적 확장이다. 미지원 자산에는
C2가 송신하지 않으며 기존 자산 구현은 자동으로 지원하지 않는다.
기본 더미 capability는 관측 9, 포인터 14다. `--capabilities`를 별도로 지정하면
해당 비트의 포함 여부도 명시적으로 설정한다.

## 적용과 확인

1. C2는 등록 lease·Heartbeat 연결·capability와 입력을 검사하고 현재 세션으로 보낸다.
   초기 Pose 없이도 설정할 수 있다. 포인터 자산에 활성 할당이 있으면 거부한다.
2. 자산은 header·자산 ID·세션·유효시간·값과 내부 상태를 검사한다.
   관측은 STANDBY, 탐색·축 이동 중단 상태만 허용한다. 포인터는 STANDBY이며
   출력·무장·자동 추적이 모두 중단되어야 한다. 조건 불충족 시 기존 Pose를 유지한다.
3. 자산은 x/y/z/방위를 하나의 임계구역에서 적용하고 `CommandAck(COMPLETED)`와
   갱신된 `AssetPose`를 보고한다. 거부는 REJECTED와 오류 코드로 회신한다.
4. C2는 설정 요청만으로 Pose를 변경하지 않고 자산의 보고를 기존 검증 경로로 수신한다.
   `sent`와 적용 완료를 구분하며 `outcomes`와 `assets`로 결과를 확인한다.
5. 중복 명령은 최초 ACK를 재회신하며 재적용하지 않는다. Pose 보고가 유실되면
   이후 주기 보고에서 복구한다. 캐시에서 퇴출된 과거 ID도 다시 실행하지 않는다.

명령 유효시간은 수신·수락 기한이며 기존 역할별 설정을 따른다(기본 500 ms).
첫 진행 ACK 이후에는 이 기한 대신 완료 대기시간(기본 2 s)을 적용한다.
반복 진행 ACK는 완료 기한을 연장하지 않는다. ACK timeout·재전송·종결
상태·세션 교체는 기존 CommandTracker 계약을 사용한다. 새 설정 명령은 관측 명령
또는 포인터 명령과 command_id/sequence 공간을 공유한다.

## 유지 및 초기화

수동값은 자산 프로세스의 현재 세션 동안 이후 Pose 보고에도 유지한다. 자산 재시작은
실행 옵션의 초기 Pose로 복원하며 C2는 이전 세션의 설정을 자동 재적용하지 않는다.
자동 센서 추정과 전환하는 source/mode 필드, 영구 저장, reset 명령은 추가하지 않는다.
관측 더미가 송신하는 표적은 기존 PROJECT_FRAME 시나리오 좌표다. 설치 Pose 변경으로
이미 월드 좌표인 표적에 좌표변환을 중복 적용하지 않는다.

## 검증

ICD-TC-020: 두 역할의 codec round-trip, 정상 설정 및 Pose 재보고, 원자적 거부,
방위 경계/NaN, 만료, 세션 불일치, 중복 미재적용, 자산 Endpoint 라우팅, 초기 Pose 없이
설정, capability 미지원 및 미등록 거부, 기존 명령 ID와 충돌하지 않음을 검증한다.
실제 장비는 같은 command 수신 및 내부 Pose 적용 구현을 별도로 갖추어야 한다.
