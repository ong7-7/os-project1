# kjnu0522001-pintos
A Pintos project for Kongju National University's Operating Systems course 0522001, configured to build and run on modern Ubuntu/QEMU environments.

80x86 아키텍처용 교육용 운영체제인 Pintos는 커널 스레드, 사용자 프로그램 적재·실행, 파일 시스템 등을 실제 상용 OS보다 단순화해 학습용으로 제공한다. 본 실습 프로젝트에서는 물리 하드웨어 대신 오픈 소스 하드웨어 에뮬레이터인 QEMU(Quick Emulator)를 활용해 가상 머신 환경에서 Pintos를 구동한다.

Original pintos repository: https://web.stanford.edu/class/cs140/projects/pintos/

## 개발 환경 권장 사항
* OS: Ubuntu 24.04 LTS 혹은 WSL2 기반 Ubuntu 
* 컴파일러: GCC 10 이상 (실습 검증은 GCC 11.4 기준)
* 필수 도구: GCC, GNU binutils, GNU make, Perl, QEMU (`qemu-system-x86`), Git

패키지 설치 예시는 다음과 같다.
```bash
sudo apt update
sudo apt install build-essential binutils qemu-system-x86 perl git
```


## 프로젝트 수행 배경
Pintos는 시스템의 물리 메모리를 페이지 크기의 프레임으로 나누어 관리한다. 우리는 챕터 9,10에서 페이징 기법과 가상 메모리 기법에 대해 상세하게 다루었지만, 본 과제의 복잡도를 낮추기 위한 일환으로 범위를 커널 스레드로 한정한다. 따라서, 커널 스레드 간의 주소 공간이 분리되지 않음에 유의한다. 현재 구현상의 Pintos 시스템은 단일 프레임 또는 연속된 다중 프레임을 커널 스레드에 할당할 수 있다. 만약 단일 프레임 할당을 기반으로 가상메모리 시스템을 구현한다고 가정하자. CPU는 가상 주소를 물리 주소로 변환하는 속도를 높이기 위해 TLB(Translation Lookaside Buffer)를 사용하는데, 메모리 사용량이 큰 애플리케이션의 경우 수많은 페이지 매핑 정보가 필요하여 TLB 미스가 자주 발생하게 된다. 이러한 문제를 해결하기 위해 많은 시스템에서 2MB나 1GB와 같은 거대 페이지(Huge Page)를 할당한다. 이러한 거대한 페이지를 단일 TLB 엔트리로 매핑하려면, 해당 크기만큼의 물리 메모리가 반드시 연속적이어야 한다. 따라서 본 프로젝트에서는 시스템의 가용 물리 프레임들을 연속적으로 할당하는 몇가지 방법들을 구현해 볼 것이다. 

Pintos의 페이지 관리 시스템에서는 물리 메모리의 할당 여부를 파악하기 위해 ‘lib/kernel/bitmap.c’에 정의된 비트맵 자료구조를 사용한다. 만약 첫 번째 비트가 0이라면 물리 메모리의 첫 번째 프레임이 커널에 할당되어 있는 상태이다. 이러한 환경에서 하나 이상의 연속된 프레임을 할당하기 위해 필요한 공간을 탐색하고 할당하는 방법으로는 First Fit, Next Fit, Best Fit, Worst Fit, 그리고 Buddy System과 같은 것들이 사용될 수 있다. Pintos 시스템에서 기본적으로 제공하는 연속 프레임 할당은 `First Fit`을 기반으로 한다. 여러분들은 그 외에 `Next Fit`, `Best Fit`, 그리고 `Buddy System` 할당 기법을 직접 구현해 볼 것이다. 


## 프로젝트 수행 순서 
Pintos 프로젝트 디렉토리 내의 ‘threads/palloc.c’와 ‘lib/kernel/bitmap.c’ 소스코드를 수정하여 `Next Fit`, `Best Fit`, 그리고 `Buddy System` 할당 기법을 추가한다. 각 메모리 할당 기법을 구현한 뒤에는 준비된 테스트 케이스를 직접 실행하여 구현의 정확성을 확인해야 한다. 테스트 실행을 지원하기 위해서는, 각 테스트의 시작 시점에 해당 테스트가 요구하는 메모리 할당 방식을 커널에 알릴 수 있어야 한다. 이를 위해 `threads/palloc.h` 및 `threads/palloc.c` 내부에 해당 모드를 enum으로 정의하고 이를 설정할 수 있는 인터페이스를 활용한다. 아래의 코드를 참고하여 구현에 활용하도록 한다. 

```c
/* Contiguous allocation mode selector */
enum palloc_mode {
    PAL_FIRST_FIT,
    PAL_NEXT_FIT,
    PAL_BEST_FIT,
    PAL_BUDDY
};
void palloc_set_mode(enum palloc_mode mode);
```

이러한 인터페이스를 갖추면 각 테스트 파일(예: `tests/threads/nextfit.c`) 내부에서 `palloc_set_mode(PAL_NEXT_FIT)`을 호출하여 동적으로 할당 알고리즘을 변경하고 테스트를 수행할 수 있다. 



## Pintos 디렉터리 구조
- `threads/`: 커널의 스레드 서브시스템과 빌드 스크립트, **필요 시 수정 가능**
- `tests/threads/`: 자동 채점을 위한 커널 스레드의 페이지 할당 테스트
- `lib/`: 커널/유저 공용 라이브러리 및 자료구조
- `utils/`: `pintos`, `pintos-gdb` 등 실행 및 디버깅 유틸리티
필요한 자료구조는 `lib/kernel`에서 제공하는 구현을 활용한다. 보다 자세한 설명은 헤더 파일과 원본 핀토스 문서 참고 - https://web.stanford.edu/class/cs140/projects/pintos/pintos.pdf 


## `threads` 디렉터리 구조 
프로젝트를 위해 참고해야 할 코드는 밑줄로 표시
| 파일(threads/) | 역할(설명) |
|---|---|
| loader.S, loader.h | BIOS가 512바이트 부트 섹터로 적재하는 부트 로더. 디스크에서 kernel.bin을 읽어 메모리에 올린 뒤 start.S의 엔트리 라벨 start로 점프. |
| start.S | 초기 부트 코드. 16비트에서 32비트 보호 모드로 전환하고 최소 GDT/세그먼트/스택을 설정한 후 C 레벨의 커널 진입점(main, init.c)을 호출. |
| kernel.lds.S | 커널 링크 스크립트(전처리된 후 kernel.lds로 사용). 커널의 배치/섹션/주소를 제어. |
| <ins>init.c, init.h</ins> | 커널의 C 진입점 main을 제공. 스레드/인터럽트/메모리/디바이스 초기화, 커맨드라인 처리, 테스트/작업 실행을 담당. |
| <ins>thread.c, thread.h</ins> | 커널 스레드 구현. 스레드 생성(thread_create), 준비/대기/종료 상태 관리, 스케줄링(ready 큐 관리, schedule), 타이머와의 연동 등 기본 스레드 기능 제공. |
| <ins>synch.c, synch.h</ins> | 동기화 제공: 세마포어, 락, 조건변수 기본 동작 |
| switch.S, switch.h | 컨텍스트 스위칭용 저수준 코드. switch_threads 등 레지스터/스택을 저장·복구하여 다른 스레드로 전환. |
| <ins>palloc.c, palloc.h</ins> | 4KB 페이지 프레임 할당기(커널/유저 풀). 페이지 단위의 물리 메모리 할당/해제를 관리. |
| malloc.c, malloc.h | 커널 힙용 가변 크기 할당기. palloc 위에서 동작하는 kmalloc/free 구현. |
| interrupt.c, interrupt.h | 인터럽트 서브시스템. IDT 생성/등록, 예외와 하드웨어 인터럽트 디스패치, 핸들러 설치, intr_enable/disable 등 인터럽트 레벨 제어 제공. |
| intr-stubs.S, intr-stubs.h | 각 인터럽트 벡터의 어셈블리 스텁. 레지스터/인터럽트 프레임을 저장하고 C 핸들러로 제어 이동. |
| io.h | x86 포트 I/O 헬퍼(inb/outb/inw/outw/inl/outl 등) 인라인 함수/매크로를 제공하는 헤더. |
| <ins>vaddr.h</ins> | 가상 주소 관련 매크로/상수(PGSIZE, PGROUNDUP/DOWN 등) 및 커널/유저 주소 공간 경계 정의. |
| pte.h | x86 페이지 디렉터리/테이블 엔트리 형식과 비트 플래그(PTE_P, PTE_W, PTE_U 등) 정의. |
| flags.h | x86 EFLAGS 레지스터 비트 정의(IF, DF 등). |


## 저장소 Clone 및 개인 저장소 복제 
```bash
git clone -b proj2 https://github.com/jhnleeknu/kjnu0522001-pintos.git
cd kjnu0522001-pintos
```
수강생은 각자 clone 받은 코드를 기반으로 개발을 진행하며, 소스코드 수정사항을 관리하고자 할 경우 아래와 같이 코드베이스를 자신의 저장소로 복제하는 것을 권장한다. 
1. GitHub에서 본인 계정에 새로운 빈 저장소 생성
2. 원본 저장소 bare clone 수행
   ```bash
   git clone --bare https://github.com/jhnleeknu/kjnu0522001-pintos.git
   cd kjnu0522001-pintos.git
   ```
3. 새로 만든 본인 저장소로 mirror-push
   ```bash
   git push --mirror https://github.com/<your-username>/<your-repository-name>.git
   ```
5. 임시 디렉터리 정리
   ```bash
   cd ..
   rm -rf kjnu0522001-pintos.git
   ```
5. 본인 계정의 저장소를 clone 하여 자유롭게 개발 및 백업
   ```bash
   git clone -b proj2 https://github.com/<your-username>/<your-repository-name>.git
   ```
**주의사항: 본 저장소를 FORK하지 마시기 바랍니다!** 

## 빌드 및 실행 방법
```bash
git clone https://github.com/jhnleeknu/kjnu0522001-pintos.git
cd kjnu0522001-pintos/threads
```
또는
```bash
git clone -b proj2 https://github.com/<your-username>/<your-repository-name>.git
cd <your-repository-name>
```

```bash
../utils/pintos -- run firstfit 
../utils/pintos -- run nextfit
../utils/pintos -- run bestfit 
../utils/pintos -- run buddy
```

전체 테스트는 `make check`로 실행 가능 



## 프로젝트 요구 사항
각 테스트 케이스를 통과하기 위해서는 다음과 같은 알고리즘의 특성을 정확히 구현해야 한다. First Fit의 경우, 메모리의 시작 지점부터 검색하여 필요한 크기 이상의 빈 공간이 나오는 최초 위치를 할당한다. First Fit은 기본으로 제공되는 구현이므로, 다른 구현들과 분리하기만 하면 된다. Next Fit의 경우, 직전에 할당한 위치에서부터 검색하여 필요한 크기 이상의 빈 공간이 나오는 최초 위치를 할당한다. Best Fit의 경우, 메모리 전체를 검색하여 필요한 크기 보다 큰 공간 중에서 가장 적은 공간을 할당한다. 마지막으로, Buddy System은 메모리를 $2^k$ 크기의 메모리 블록으로 할당하게 되는데, 요청 크기 $s$에 가장 가까운 빈 공간($2^{k-1} < s \le 2^k$)이 생성될 때까지 메모리 공간을 절반으로 계속 분할한다. 소스 코드 분석을 통해 할당 가능한 커널 페이지 수를 파악하고, 동적으로 $k$값을 계산해 Buddy System에서 활용하도록 한다. 

**※주의 사항: 테스트케이스 소스코드를 제외하고, 프레임 할당 시 커널 로그에 출력을 하게 될 경우 채점 시 오류가 발생하게 됩니다. 따라서 반드시 제출 시에는 threads/ 내 개발용으로 추가한 로깅 코드를 제거해야 합니다.**

## 제출물 
1. pintos 프로젝트 파일 (zip, 7z, tar.gz 등)
2. 프로젝트 요구사항을 구현한 내용과 결과를 설명하는 프로젝트 보고서 (pdf 파일)

## 제출 방법 
`kjnu0522001-pintos/` 프로젝트 루트 디렉토리에서 `make clean` 으로 컴파일 결과물을 정리
 pdf 보고서를 report.pdf로 프로젝트 루트에 포함 후 압축하여 제출 

## 프로젝트 채점
제출물의 `tests/threads/` 테스트케이스를 채점용으로 교체하여 `make check` 수행

