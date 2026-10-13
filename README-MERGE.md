# Pintos 우선순위 통합본

상민 구현을 기반으로 선우의 중첩 기부·조건변수 처리 방식과 형광의 준비 큐 재정렬 방식을 반영하고, 인터럽트 선점·락 인계·기부 재시도에서 확인된 결함을 수정했다.

2026-10-13, Ubuntu 22.04 x86_64 개발 컨테이너에서 GCC 빌드와 QEMU 실행으로 기본 알람·우선순위 18개 및 별도 추가 검증 9개를 모두 통과했다. 자세한 비교와 증거는 함께 제공한 `priority-review/comparison.md`에 있다.

Linux 개발 컨테이너에서 이 프로젝트 폴더로 이동한 뒤 실행한다.

```sh
./run-priority-tests.sh
```

이 스크립트는 프로젝트 자체의 Pintos 실행 도구를 경로에 추가하고, 기본 알람·우선순위 테스트 18개를 빌드·실행한다.

**MLFQS는 미구현이다.** 기본 `make check`에는 MLFQS 9개도 포함되므로 전체 과제 통과로 해석하면 안 된다. 추가 9개 검증을 재현하는 방법은 비교 보고서에 설명되어 있다.

변경한 핵심 파일: `pintos/threads/thread.c`, `pintos/threads/synch.c`, `pintos/include/threads/thread.h`, `pintos/include/threads/synch.h`.

기존 `dev` 브랜치 기록 위에 통합 코드를 새 커밋으로 반영했다. 현재 열려 있던 로컬 프로젝트의 진행 중인 수정은 유지했다.
