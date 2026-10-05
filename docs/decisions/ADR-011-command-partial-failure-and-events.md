# ADR-011: 명령 부분 실패, 수명주기 직렬화와 이벤트 기록

## 문제

track 이후 송신 예외는 상대가 명령을 받았는지 확정할 수 없다. START 준비 실패를 pending으로
남기면 다음 retry가 의도하지 않은 START를 보낼 수 있다. 세션 교체와 assign/unassign/retry가
동시에 실행되면 퇴역 세션의 명령이나 할당이 남을 수 있고, 한 send 예외가 전체 retry loop를
중단할 수 있다. 메모리 이력만으로는 프로세스 종료 뒤 원인을 추적하기 어렵다.

## 결정

준비, ACK 단계, 전달 상태를 구분한다. 필수 stream 준비는 START 등록보다 먼저 실행하고,
등록 후 latch 실패는 아직 보내지 않은 명령만 취소한다. send 예외는 uncertain으로 기록하고
bounded retry를 유지한다. UDP가 성공 반환해도 완료 판정은 ACK에 맡긴다.
START latch는 전송 단계에서 보수적으로 유지하며 실제 장비 상태를 추정해 rollback하지 않는다.
할당·해제·동적 송신·재시도·세션 퇴역·표적 스트림 갱신은 공통 lifecycle mutex로 조율한다.
ACK 처리는 별도 tracker 잠금으로 처리하여 동기 sender의 ACK callback이 교착하지 않게 한다.
세션 교체는 먼저 기존 pending과 할당을 퇴역시키고 이전 endpoint에 best-effort ESTOP을 보낸다.
각 send 예외는 개별 집계하여 후속 retry와 ESTOP 반복이 계속되게 한다.

관측은 EventSink 포트와 파일 EventLog 어댑터로 분리한다. 이벤트 호출은 tracker 잠금 밖에서
실행하고 sink 예외를 격리한다. JSONL append/flush, 파일 크기와 회전 개수 제한, write error
metrics로 운영자가 명령 key와 세션별 사건을 연결할 수 있게 한다.

## 검증과 한계

실패 송신, stream 실패 후 START retry 방지, 세션 퇴역 중 안전 송신 실패, 자산별 retry 격리,
START/unassign·assign/session·retry/session 경쟁, 로그 escape/재기동 append/rotation을 테스트한다.
실제 UDP relay probe로 손실·중복·순서 변경과 ACK 완료율을 별도로 검증한다.
공통 잠금과 동기 로그는 처리량을 제한할 수 있다. 느린 callback은 금지하고 프로파일링 근거가
생기면 분할 잠금/비동기 logger를 검토한다. 로그는 영속 상태 복원이나 fsync 내구성, 상대방
멱등성, 장비 정지를 보장하지 않는다. lifecycle EventSink callback 재진입은 지원하지 않는다.
