# Runtime 측정 재현

```powershell
cmake -S . -B build -A x64 -DC2_BUILD_BENCHMARKS=ON
cmake --build build --config Release --parallel
./scripts/run_runtime_benchmark.ps1 -Samples 50000 -Runs 3
```

결과는 기본 `build/runtime-benchmark.json`에 저장한다. `-OutputPath`로 위치를 지정할 수
있으며 상위 폴더는 먼저 생성한다. JSON은 측정 시각, commit, 작업 폴더 변경 여부,
OS/CPU/논리 프로세서 수, workload와 각 실행의 결과를 포함한다.
CI도 소규모 실행 결과와 JUnit을 `c2-validation-evidence` artifact에 보관한다.

workload: 관측 16대 등록 및 Heartbeat 후 각 32개 트랙을 반복 갱신한다. 2000회 warmup
이후 Protobuf encode + Runtime decode + endpoint/session 검증 + TrackStore 갱신의
호출 지연을 steady clock으로 기록한다. 총 512 트랙과 거부 0을 검사하고 실패하면
측정 도구는 nonzero exit로 종료한다. 셋업은 측정에 포함하지 않는다.

p50/p95/p99는 정렬된 표본의 `floor(p*(n-1))` 위치다. throughput은 샘플 생성과
clock/sample 기록 비용을 포함한 전체 측정 loop 시간으로 나눈 messages/s다.
단일 프로세스·단일 호출 스레드이며 실제 socket, 수신 큐 대기, 할당 점수 계산,
타격 자산 송신, 실제 장비 제어 및 End-to-End latency는 포함하지 않는다.

환경 간 수치를 직접 성능 우열로 해석하지 않는다. 같은 환경과 workload에서 반복하고
CPU 점유와 전원 상태, 빌드 옵션, 변경 여부를 함께 기록한다. CI shared runner에는
절대 성능 threshold를 두지 않는다. 기존 결과를 개선 성과로 쓰려면 동일 workload의
변경 전/후 측정이 필요하다. 현재 도구는 변경 전 수치를 추정하지 않는다.

수신 큐 포화는 `DatagramProcessorTest.BoundsQueueBytesAndCountAndDrainsInOrder`로
결정적으로 재현한다. 실제 UDP 시나리오는 별도 smoke이며 이 측정의 성능 수치와 합치지 않는다.

## 보관한 실제 표본

[2026-10-02 결과 JSON](measurements/2026-10-02-runtime.json)은 코드 커밋
`5e4a05cfd93d633b31b244f1ae887bdc4fab72ec`의 clean working tree에서 측정했다.
Windows Release, 논리 프로세서 8개, 16 observers / 512 tracks / 50000 samples를
3회 실행했고 각 실행의 protocol 거부는 0이다. 환경의 상세 정보는 JSON에 있다.

| 실행 | messages/s | p50 µs | p95 µs | p99 µs |
|---|---:|---:|---:|---:|
| 1 | 180,516 | 5.2 | 6.3 | 7.4 |
| 2 | 167,801 | 5.5 | 7.2 | 8.0 |
| 3 | 175,540 | 5.5 | 6.3 | 7.5 |

이 표본은 로컬 in-process 측정이다. 단일 최고값을 전체 시스템 처리량으로 표현하지
않고 반복 실행의 변동과 측정 범위를 함께 제시한다.


## 실제 UDP 신뢰성 probe

`-DC2_BUILD_BENCHMARKS=ON`으로 빌드한 뒤
`scripts/run_udp_reliability_probe.ps1 -BinDir ./build/Release`를 실행한다.
`c2_udp_probe burst`와 `c2_udp_probe faults`를 각각 독립 프로세스로 실행한다.
loopback UDP downlink/uplink relay, bounded ingress(64 packets), Runtime tracker와
idempotent STOP 시뮬레이터를 통과한다. burst는 200개 명령을 연속 발행한다.
faults는 command_id modulo 기준으로 첫 명령 1/4과 첫 ACK 1/5을 버리고,
일부 명령을 복제하거나 다음 전달 뒤로 지연시킨다. 동일 ID 재시도는 ACK를 다시 보내며
시뮬레이터 execution set은 중복 ID를 한 번만 집계한다. 이것은 실장비 멱등성의 증명이 아니다.

ACK p50/p95/p99는 서버 API 호출 직전 steady clock부터 completed outcome까지이며
완료한 명령만 표본에 포함한다. JSON의 completed/commands, unique execution, injected fault
counters와 handler_errors를 함께 읽어야 한다. 모두 완료되지 않거나 fault 주입이 누락되면
도구는 실패한다. 20ms ACK timeout, 최대 100 attempts, 10초 delivery validity를 사용한다.
OS socket buffer 손실과 application queue drop은 다르며 queue_dropped_full은 후자만 센다.
물리 장비·네트워크 구간·실제 동작 완료 시간·JSONL 로그 I/O는 측정하지 않는다.
CI는 신뢰성 invariant만 검사하며 공유 runner에 절대 지연 threshold를 두지 않는다.

실측 결과는 [2026-10-05 UDP JSON](measurements/2026-10-05-udp.json)에 보관한다.
코드 커밋·tracked 변경 여부·별도 untracked 파일 수·실행 환경을 함께 기록한다.
