# ADR-005 — 자산·세션별 명령 키와 ACK 상태기계

## 상태

채택

## 결정

pending 명령은 `(destination asset_id, destination session_id, command_id)`로
식별한다. 따라서 서로 다른 자산이나 재시작 세션이 같은 `command_id`를 사용해도
충돌하지 않으며, 이전 세션 ACK는 현재 세션 명령을 종료할 수 없다.

명령은 최초 송신 직후 `awaiting_delivery` 상태로 시작한다. `RECEIVED`, `ACCEPTED`,
`IN_PROGRESS`는 전달/진행 ACK로 취급해 `awaiting_completion`으로 전환하며 pending을
제거하지 않는다. `COMPLETED`, `REJECTED`, `FAILED`만 ACK 기반 종결 상태다.

`awaiting_delivery`에서는 설정된 timeout마다 최초와 동일한 datagram을 동일 ID로
재전송한다. 최대 시도 횟수를 채우면 `delivery_exhausted`로 종료한다. 첫 진행 ACK 이후
재전송을 멈추고 첫 진행 ACK 수신 시각부터 별도 completion timeout을 적용한다.
명령 `valid_until`은 전달 대기 중에만 적용하며 만료 시 `expired`로 종료한다.
진행 ACK 이후에는 이 기한을 적용하지 않는다. 반복 진행 ACK는 완료 기한을 연장하지
않는다. 완료 대기시간은 모터 동작 시간을 고려해 설정하며, timeout은 C2 결과 추적을
종료할 뿐 장비에 정지 명령을 보내지 않는다. 세션 교체·해제 시에는 `session_ended`로 종료한다.

pending 수는 전체 및 자산별로 제한하고, 종결 이력도 설정된 개수만 보존한다. 네트워크
송신은 tracker mutex 밖에서 수행할 수 있도록 `poll`이 송신 snapshot을 반환한다.
