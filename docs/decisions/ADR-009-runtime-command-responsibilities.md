# ADR-009: Runtime 객체의 책임 분리

## 문제

다중 자산과 단일 자산 호환 동작이 ServerRuntime에 함께 누적되면서 수신 라우팅,
명령 정책, 할당 조율, 순번 발급, 재전송과 이력 보관이 같은 객체의 변경 이유가 되었다.

## 결정

ServerRuntime은 기존 공개 API를 제공하는 facade이며 구성 요소를 합성하고 호출을 조율한다.

| 객체 | 소유하는 책임 |
|---|---|
| AssetMessageRouter | 등록·세션 교체·인증과 다중 자산 메시지 라우팅 |
| LegacyMessageRouter | 역할별 호환 상태·텔레메트리와 ACK 처리 |
| AssignmentCoordinator | 후보 구성, 기존 할당 안전성 재평가, 자동/수동 할당과 스트림 연결 |
| AttackCommandService | 공격 조건 검증과 일반·안전 명령 생성 |
| EffectorCommandService | 명시적 표적/Pose/한계 또는 호환 저장소 기반 지향 명령 생성 |
| CommandIdentity | 명령 번호와 순번의 원자적 발급 및 0을 건너뛰는 wrap |
| RoutedPointStore | 지향 명령의 표적·자산·세션·유효시간 일치와 삭제 |
| TrackUpdatePublisher | 이동 표적 스트림 생성·검증·송신과 동시 실행 시 순번 발급 |
| HeartbeatPublisher | 등록 자산 및 호환 Endpoint Heartbeat 생성·송신과 역할별 순번 |
| InboundRejectionLog | 용량 제한, 이벤트 번호와 독립 조회 snapshot |
| LegacyCommandTracker | 호환 명령의 ACK 종료·재전송·시도 한도 |

공격 서비스의 명시적 AttackCommandContext는 자산별 호출 상태를 전달한다.
호환 경로가 보관하는 상태와 섞이지 않으며 두 경로가 공격 상태 조건을 공유한다.
세션, 연결, 상태 최신성과 할당의 확인은 Runtime 및 라우팅/조율 객체가 담당한다.
단일 자산 지향 저장소 조회와 명시적 64비트 전역 트랙 지향 경로도 생성 로직을 공유한다.

라우터는 이미 디코딩된 Envelope를 받아 재디코딩을 피한다. 자산 세션 변경의
안전 송신은 주입된 callback을 사용하여 라우터가 facade 자체에 의존하지 않는다.
UDP 송신은 DatagramSender callback으로 주입해 실제 소켓 없이 시험할 수 있다.
현재 교체 요구가 없는 저장소와 정책은 구체 객체를 합성하고, 계산 함수와 wire DTO는
그 역할을 유지한다. 상속 계층이나 불필요한 인터페이스는 추가하지 않는다.

## 호환성과 동시 실행

호환 명령은 어떤 ACK든 전달 추적을 종료한다. 세션별 delivery/completion을 구별하는
CommandTracker와 의미가 달라 별도 객체로 유지한다. 재전송 객체는 송신 목록만 반환하고
실제 송신은 잠금 밖에서 실행한다. 스트림 순번은 잠금 아래 발급하고 외부 송신은 잠금
밖에서 수행한다. 기존 자산 수명주기 잠금의 범위와 안전 조건은 유지한다.

## 검증

기존 Runtime·할당·명령·프로토콜·UDP 테스트와 다중 프로세스 스모크에 더해,
명시적 컨텍스트의 상태 격리, 64비트 트랙 지향, 번호 wrap/동시 발급, 지향 세션/만료,
이력 용량, 재전송 시각 역행/시도 한도 및 스트림 동시 순번 발급을 회귀 테스트한다.
