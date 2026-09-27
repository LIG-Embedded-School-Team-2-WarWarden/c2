# ADR-006 — 동적 등록 Endpoint 신뢰 및 라우팅

## 상태

채택

## 결정

C2는 등록 datagram의 실제 송신 IP와 등록 payload의 광고 명령 포트를 조합해 자산의
명령 Endpoint를 만든다. 이후 자산 발신 메시지는 현재 등록 세션의 `asset_id`,
`session_id`, 역할 및 등록 datagram 송신 Endpoint가 모두 일치할 때만 반영한다.

일반 명령은 lease와 Heartbeat가 유효하고 현재 세션 Pose가 동기화된 자산에만 보낸다.
명령 헤더의 `asset_id`와 `session_id`는 registry snapshot에서 채우며 고정 역할별
Endpoint는 다중 자산 API에서 사용하지 않는다. 동일 자산의 새 세션이 등록되거나
unregister되면 이전 세션 pending 명령을 명시적으로 종료한다.

## 제한

현재 정책은 등록 송신 Endpoint가 이후 telemetry 송신 Endpoint와 같다고 가정한다.
NAT port 변환, 비대칭 경로, 다중 NIC 자동 선택은 지원하지 않는다. UDP 메시지는
암호학적으로 인증되지 않으므로 이 검증만으로 위조를 완전히 막을 수 없다. 동적 등록은
격리된 신뢰 네트워크에서 사용하고 실제 배포에는 방화벽, VPN, DTLS 또는 메시지 인증을
추가해야 한다.
