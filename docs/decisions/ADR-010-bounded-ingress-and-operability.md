# ADR-010: 용량 제한 ingress와 운영 지표

## 문제

UDP receive thread가 Runtime 처리를 직접 호출하면 consumer의 지연이 recv를 막는다.
consumer 예외를 흡수하는 경계와 예기치 않은 receive 종료를 지표로 확인할 수 없었다.

## 결정

공용 ServerUdpIngress는 DatagramProcessor를 합성한다. receive callback은 실제 source
Endpoint와 UTC 수신 시각을 보존한 packet을 bounded FIFO에 입장시킨다. 단일 worker가
입장 순서대로 Runtime을 호출한다. packet 수와 payload bytes 한도 중 하나라도 넘으면
새 packet을 버린다. 기존 자산 한도, pending 한도 및 프로토콜 검증은 계속 적용한다.

용량 기본값은 1024 packets / 4 MiB queued payload다. 처리 중 packet, 수신 buffer,
컨테이너/endpoint 메타데이터 및 OS socket buffer 메모리는 이 한도 밖에 있다.
수신 시각은 wire/상태 검증에 쓰고, 큐 대기시간은 steady clock으로 별도 기록한다.

입장/처리/drop/handler error, 현재 큐 깊이·바이트, high-water와 최대 대기시간을
잠금 아래 snapshot으로 제공한다. UDP 송수신/byte/error는 누적 atomic counters다.
metrics 콘솔에서 이 지표와 송신 transport 및 periodic worker 실패를 확인한다.
packet 단위 ICMP port-unreachable 및 message-size receive 오류는 집계 후 수신을 계속한다.
치명적인 receive 오류는 running=false로 드러낸다. restart 전에 stop/join이 필요하다.

종료는 UDP stop → admission close → queue drain → worker join 순서다.
ServerUdpIngress의 소멸자도 같은 순서를 사용한다. Runtime과 sender는 그보다 오래 살아야
하며 서버 main의 소유/종료 순서가 이를 보장한다. handler가 무한 대기하면 drain도 끝나지
않으므로 유한시간 종료 보장으로 설명하지 않는다. handler가 자신의 stop을 호출하는
재진입 lifecycle은 지원하지 않는다.

## Trade-off와 검증

single consumer는 분석 가능한 순서를 제공하지만 수평 병렬화를 제공하지 않는다.
FIFO에는 안전명령/ACK 우선순위가 없고, drop은 UDP 재송신자 제어가 아닌 load shedding이다.
적체 시 delay와 ACK/Heartbeat timeout을 함께 분석해야 한다. 외부 metric endpoint,
영속 감사 로그, fair scheduling과 우선순위는 후속 배포 요구로 결정한다.

unit test는 gated handler를 이용해 count/byte 포화, drop, FIFO와 drain을 결정적으로
검증하고, 예외 후 복구, restart 및 concurrent producers의 accounting/상한을 확인한다.
UDP consumer failure 후 수신 지속을 실제 loopback으로 확인한다. 다중 프로세스 smoke는
metrics 조회와 기존 등록/공격/session 교체 동작을 검사한다.
