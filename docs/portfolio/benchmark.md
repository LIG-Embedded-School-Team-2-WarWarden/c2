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
