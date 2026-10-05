# C2 설계 결정의 식별자

이 디렉터리의 ADR은 C2 서버 구현 결정이다. Confluence 회의·의사결정 로그의
ADR과 같은 번호가 존재하므로 인용할 때 출처를 포함한다.

- `C2-ADR-005`: [명령 ACK 상태기계](ADR-005-command-ack-state-machine.md)
- `CF-ADR-005`: [Confluence 책임 배분 기록](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/557060)
- `C2-ADR-009`: [Runtime 책임 분리](ADR-009-runtime-command-responsibilities.md)
- `C2-ADR-010`: [bounded ingress와 운영 지표](ADR-010-bounded-ingress-and-operability.md)
- `C2-ADR-011`: [부분 실패·수명주기·운영 기록](ADR-011-command-partial-failure-and-events.md)
- `C2-ADR-012`: [수동 지향의 표적 ID 제거](ADR-012-manual-pointing-without-target.md)

이 표시는 인용 별칭이다. 기존 파일명·문서 ID·승인 상태를 변경하지 않는다.
후속 ADR-011에서 운영 JSONL 로그가 구현되었으므로 ADR-010의 로그 후속 과제는
현재 보안 감사·내구성 요구와 구분해서 읽는다.
