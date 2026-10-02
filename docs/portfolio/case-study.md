# 설계 사례: 통제소 서버의 상태와 장애 경계

## 문제와 선택

단일 관측/타격 자산의 역할별 포트 모델을 다중 자산으로 확장하면, 같은 detection ID의
충돌, 같은 asset ID 재시작 후 오래된 패킷, 전달 ACK를 완료로 오해하는 문제가 생긴다.
자산에는 session ID를, 표적에는 관측 자산/세션을 포함한 전역 track ID를 부여하고
명령은 asset/session/command ID로 추적한다. 등록 packet의 실제 IP와 광고 command
port를 조합하여 endpoint를 결정한다.

UDP는 메시지 유실·중복·순서 역전을 전제로 한다. CommandTracker는 전달을 기다리는
상태와 실행 완료를 기다리는 상태를 구별한다. timeout, 최대 시도, 명령 유효시간,
전체/자산별 pending 한도가 각각 다른 실패를 제한한다. 더미 자산은 중복 명령 결과를
캐시해 같은 요청이 반복될 때 상태전이를 다시 실행하지 않는다.

## 객체지향 설계의 역할

ServerRuntime의 수신/할당/명령/송신/이력 책임을 여러 객체로 분리했다.
명령 정책은 서비스에, 변경 가능한 상태와 잠금은 저장 객체에 둔다. 실제 UDP 대신
송신 callback과 명시적 시각을 전달해 테스트에서 관찰·재현한다.
단일 자산 호환 동작과 다중 자산 동작은 서로 다른 상태 의미를 유지하면서 명령 생성
규칙을 공유한다. DTO와 좌표 계산은 순수 데이터/함수로 유지한다.
상속 수 자체를 설계 품질로 보지 않고, 상태 소유권과 변경 이유를 경계로 삼았다.

## 과부하와 예외

수신 스레드에서 업무 처리를 직접 실행하면 느린 handler가 다음 recv를 늦춘다.
공용 ingress는 소켓 수신과 업무 처리를 bounded FIFO로 분리했다. 수신 시각과 source
Endpoint를 보존하고 단일 worker가 입장된 packet 순서대로 처리한다. 큐는 packet 수와
payload 바이트 양을 함께 제한한다. 포화 시 새 packet을 버려 이미 입장된 순서를 유지한다.
이것은 load shedding이며 송신자를 늦추는 end-to-end flow control은 아니다.

큐 한도는 대기 중 payload에 적용된다. 처리 중 packet 하나, recv buffer, deque/vector와
endpoint 문자열 등의 관리 메모리는 별도다. UDP/OS buffer에도 손실이 생길 수 있으며
application drop 카운터가 전체 네트워크 손실을 대표하지 않는다.
ACK나 Heartbeat도 포화 시 손실될 수 있다. 우선순위가 없으므로 새 부하를 줄이고
큐 대기시간·drop·통신/ACK timeout을 함께 확인해야 한다. 우선순위 채널과 fair scheduling은
실측 부하 및 요구사항이 확보된 뒤 검토할 설계 과제다.

handler 예외는 worker를 종료하지 않고 실패 카운터에 기록한다. UDP receive의
ICMP port-unreachable/oversized packet은 packet 단위 오류로 처리하고 수신을 계속한다.
그 밖의 치명적 receive 오류는 running=false와 오류 지표로 드러낸다. 주기 worker도
예외 카운터를 남기고 반복 실행을 지속한다.

## 종료와 trade-off

ingress stop은 UDP 입장을 먼저 닫고, 처리 큐를 drain한 뒤 worker를 join한다.
그래서 Runtime이나 송신 transport가 살아 있는 동안 남은 작업이 처리된다.
handler가 반환하지 않으면 join도 완료되지 않는다. 이는 유한시간 종료 보장이 아니며
production handler에 무한 대기나 무제한 외부 I/O를 넣지 않아야 한다.

단일 worker는 순서와 동시성 분석을 단순하게 만들지만 처리량 확장에는 한계가 있다.
현재 microbenchmark와 큐 지표로 병목을 관찰할 근거를 마련했다. 복수 worker나 IOCP,
Linux epoll, lock-free queue 도입은 측정과 운용 요구로 정당화해야 한다.
