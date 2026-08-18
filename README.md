# IOCP 원카드 서버

Windows IOCP(I/O Completion Port)를 **프레임워크 없이 raw Win32 API로 직접** 구현한
4인 멀티플레이 카드 게임(원카드/트럼프 카드) 서버 + Unity 클라이언트입니다.

## 왜 이 프로젝트인가

포트폴리오에 있는 다른 두 서버 프로젝트(boost.asio 기반 방치형 게임 서버, C# 기반 MMO
서버)는 각각 boost.asio와 .NET 소켓 API라는 **이미 추상화된 네트워크 레이어** 위에서
동작합니다. 이 프로젝트는 그 추상화를 걷어내고, `CreateIoCompletionPort`,
`GetQueuedCompletionStatus`, `WSARecv`/`WSASend` 같은 **Windows IOCP 원시 API를 직접
다루면서** 세션 관리, 패킷 프레이밍, 동시성 처리를 처음부터 구현해본 프로젝트입니다.

## 기술 스택

| 구분 | 내용 |
|---|---|
| 서버 | C++20, Windows IOCP(Winsock2), Protocol Buffers, vcpkg |
| 클라이언트 | Unity 6, C#, Google.Protobuf, Input System |
| 부하테스트 도구 | C++ 멀티스레드 봇 클라이언트 |
| 배포/검증 | AWS EC2(Windows Server) |

## 아키텍처

```
Server (IOCP 워커 스레드 N개 + 매칭 타이머 스레드 1개)
 ├─ SessionManager   : 전체 세션 registry (mutex로 보호)
 ├─ Session          : 연결 1개 = 소켓 + recv 누적 버퍼 + send 큐
 ├─ PacketHandler     : 패킷 id로 라우팅
 ├─ ObjectPool<T>     : Session/Buffer 재사용 풀
 ├─ OneCardQueueManager : 매칭 대기열 (4명 모이면 즉시, 아니면 15초 후 인원대로 시작)
 ├─ OneCardRoom       : 방 하나의 게임 상태(덱/턴/카드효과), 자체 mutex로 보호
 ├─ JobQueue          : DB I/O를 IOCP 스레드에서 떼어내는 워커 스레드 1개짜리 작업 큐
 ├─ GameDB            : SQLite 래퍼 (계정/골드/상점/인벤토리)
 └─ HttpServer        : 상점/인벤토리 REST API (cpp-httplib, 8080번 - 게임 포트와 별개)
```

### 실시간(TCP) vs 트랜잭션(HTTP) 분리
회원가입/로그인과 게임 진행(매칭, 카드 플레이)은 지연시간에 민감하고 상태가 계속
바뀌는 실시간 통신이라 IOCP 기반 TCP(9000번)로 처리한다. 반대로 상점/인벤토리는
"눌렀을 때 한 번 처리되면 끝"인 요청-응답형 트랜잭션이라, 굳이 커스텀 패킷 프로토콜을
쓸 필요 없이 표준 REST(HTTP, 8080번)로 분리했다. 회원가입/로그인/게임 액션은 DB
조회가 필요한 경우 `JobQueue`에 위임해서 IOCP 스레드가 블로킹되지 않게 하고,
HTTP 핸들러도 `std::promise`/`future`로 같은 `JobQueue`에 작업을 넣고 동기적으로
기다리는 방식으로 DB 접근을 한 곳(JobQueue 워커 스레드 1개)으로 직렬화한다.

### 패킷 프로토콜
- `protocol.proto` : 레거시 채팅(Enter/Leave/Chat) — 초기 IOCP 학습용으로 남겨둠
- `onecard.proto` : 원카드 게임 프로토콜 (매칭 + 카드 플레이/드로우 + 게임 상태 동기화)
- 와이어 포맷: `[PacketHeader(4바이트: id uint16 + size uint16)][protobuf 직렬화 바디]`
- TCP는 스트림이라 한 번의 recv/send가 패킷 하나와 대응한다는 보장이 없어서, 서버·클라
  양쪽 모두 **누적 버퍼 기반으로 패킷 경계를 직접 재조립**한다 (`Session::DispatchPackets`,
  Unity `NetworkClient.DispatchCompletePackets`, 부하테스트 봇의 `RecvLoop` 전부 동일한
  방식).

### 원카드 게임 규칙 (하우스룰 고정)
표준 트럼프 카드 52장 + 조커 2장.
- 낼 수 있는 카드: 바닥 패와 무늬 또는 숫자가 같은 카드. 조커는 항상 가능.
- **2** : 다음 사람이 2장 강제로 먹고 턴이 넘어간다. 다음 사람도 2를 내면 누적된다(스택).
- **A** : 다음 사람 턴을 스킵한다.
- **J** : 무늬 지정 카드. 내면서 원하는 무늬를 선언한다 (자기 무늬 그대로 선언도 가능).
- **조커** : 완전 와일드. 내면서 무늬 선언 + 다음 사람이 3장 강제로 먹는다.
- 그 외(3~10, Q, K): 특수 효과 없음.
- 한 턴에 카드 한 장만 낼 수 있다 (동일 숫자 동시 내기는 지원 안 함).
- 손패를 가장 먼저 비우면 승리.
- 서버가 낼 카드의 보유 여부·룰 위반 여부·내 턴 여부를 전부 검증한다 (클라이언트를 신뢰하지 않음).

## 빌드 및 실행

### 필수 준비물
- Visual Studio 2022 (Desktop C++ 워크로드)
- [vcpkg](https://github.com/microsoft/vcpkg) — protobuf 설치 및 `vcpkg integrate install`
- Unity 6000.2.8f1 (클라이언트)

### 서버
```
cd IOCP_Portfolio
msbuild IOCP_Portfolio.vcxproj /p:Configuration=Release /p:Platform=x64
```
`x64\Release\IOCP_Portfolio.exe` 실행 (같은 폴더의 protobuf/abseil DLL은 vcpkg가 빌드 시
자동으로 복사해준다). 기본 포트 9000. Ctrl+C로 정상 종료(소켓 정리 + 스레드 join)된다.

### 클라이언트 (Unity)
카드 스프라이트로 쓰는 "2D Cards Game Art Pack"은 유료 에셋(재배포 금지)이라
레포에 포함하지 않는다 — Unity Asset Store에서 구매 후 `onecard_client/Assets/`에
직접 넣어야 한다 (`.gitignore` 참고). 없어도 실행은 되고, 카드가 스프라이트 대신
빈 박스로만 보인다.

`onecard_client` 폴더를 Unity Hub에서 열고 Play. 씬에 아무것도 배치할 필요 없이
`RuntimeInitializeOnLoadMethod`로 자동 부트스트랩된다. 기본값(127.0.0.1:9000, 상점
127.0.0.1:8080)으로 자동 접속을 시도하고, 실패하면 뜨는 화면에서 서버 IP를 직접
입력해 원격 서버(AWS 등)에 접속할 수 있다 (게임 포트/상점 포트는 프로토콜상
고정이라 IP만 입력하면 됨). 로그인 후 로비 화면의 "상점" 버튼으로 아이템 조회/구매/
인벤토리 확인이 가능하다 (`ShopClient`가 `UnityWebRequest`로 8080번 HTTP API를 호출).

### 부하테스트 도구
```
cd Client
msbuild Client.vcxproj /p:Configuration=Release /p:Platform=x64
x64\Release\Client.exe [봇 수] [지속시간(초)] [서버IP] [포트]
```
접속 → 매칭 대기열 참가 → 서버와 동일한 규칙으로 카드 자동 플레이/드로우 → 게임 종료 시
재참가, 를 반복하는 봇을 N개 띄워서 부하를 준다. 로그인 성공 시 봇마다 별도로 RTT
측정용 `C_Ping`/`S_Pong`(게임 로직과 무관, payload 없이 즉시 echo)을 봇당 최대 20개
표본까지 주고받아서, 종료 시 평균/최소/최대/p50/p95/p99를 같이 출력한다.

## 개발하며 발견하고 고친 문제들

단순히 기능을 구현한 게 아니라, 만드는 과정에서 실제로 부딪힌 버그들을 원인까지 추적해서
고친 기록입니다.

### 1. SessionManager 동시성 레이스
IO 스레드가 여러 개인데 `SessionManager`의 세션 맵에 락이 전혀 없었다. 동시에 여러
스레드가 `add`/`remove`/`broadcast`를 호출하면 data race. → mutex로 보호.

### 2. 세션 반환 순서 버그
연결 종료 시 `sessionPool_.Release()`(재사용 가능 상태로 전환)를
`sessionManager_.remove()`보다 먼저 호출하고 있었다. 그 사이 짧은 순간에 accept 루프가
같은 Session 포인터를 새 연결에 재사용하면, `broadcast()`가 여전히 옛 연결로 착각해
`Send()`를 호출하는 race가 생긴다. → SessionManager에서 먼저 제거한 뒤 풀에 반환하도록
순서 변경.

### 3. TCP 패킷 프레이밍 부재
처음 구현은 "recv 한 번 = 패킷 하나"라고 가정하고 있었다. 실제 TCP는 스트림이라 패킷이
여러 recv에 걸쳐 쪼개지거나(partial), 여러 개가 붙어서(coalesced) 올 수 있다. →
세션마다 누적 버퍼를 두고, 헤더의 `size`만큼 다 모였을 때만 패킷으로 처리하도록 재구현.

### 4. `windows.h`의 `min`/`max` 매크로 충돌
`std::min` 호출이 `windows.h`가 정의하는 매크로와 충돌해서 이상한 파싱 에러(`C2589`)가
났다. → `windows.h` include 전에 `NOMINMAX` 정의.

### 5. MSVC 소스 인코딩(UTF-8) 버그
한글 문자열 리터럴("당신의 턴이 아닙니다" 등)을 `/utf-8` 옵션 없이 컴파일하니, MSVC가
소스 파일을 시스템 코드페이지(949)로 해석해서 문자열이 깨진 채로 바이너리에 들어갔다.
protobuf가 `SerializeToString` 시점에 "invalid UTF-8 data" 경고를 뱉으면서 발견. →
프로젝트 전체에 `/utf-8` 컴파일 옵션 추가.

### 6. WSASend partial send 미처리 (실제로 스트림을 깨뜨린 원인)
`OnSendComplete()`가 `WSASend`의 실제 전송 바이트 수(`dwBytesTransferred`)를 완전히
무시하고 있었다. TCP send는 요청한 바이트를 한 번에 다 못 보내고 일부만 보낼 수 있는데,
그걸 확인 안 하고 바로 큐의 다음 패킷으로 넘어가버리면 못 보낸 나머지 바이트 뒤에 다음
패킷이 그대로 이어붙어서 받는 쪽 프레이밍이 통째로 깨진다. 부하가 걸릴 때만 간헐적으로
`invalid packet size` 에러가 재현된 원인이었다. → 전송된 바이트 수를 추적해서, 패킷을
다 보낼 때까지 이어서 전송하도록 수정.

### 7. 소켓 핸들 누수
연결이 끊길 때 세션/버퍼 풀은 반환하면서 정작 `closesocket()`은 한 번도 호출하지 않고
있었다. 접속이 쌓일수록 소켓 핸들이 계속 샜다.

### 8. Graceful shutdown 스레드 레이스
Ctrl+C 핸들러 스레드와 `main()`(소멸자 경로)이 동시에 `Stop()`을 호출할 수 있는데, 락
없이 `running_` 플래그만 보고 있어서 한쪽이 스레드 join을 끝내기 전에 다른 쪽이 먼저
통과해 `Server` 객체(스레드 멤버 포함)가 소멸돼버리는 race가 있었다. 아직 join 안 된
`std::thread`가 소멸되면 `std::terminate()`가 호출된다 — 실제로 "abort() has been
called" 크래시로 재현됨. → `Stop()`에 mutex를 걸어서, 한쪽이 완전히 끝날 때까지 다른
쪽은 반드시 대기하도록 수정.

### 9. Unity Input System에서 버튼 클릭이 안 먹던 문제
`InputSystemUIInputModule`을 코드로 `AddComponent`만 하면 마우스 클릭/포인터 액션이
비어있어서 UI 버튼이 하나도 반응하지 않았다 (키보드 입력은 `Keyboard.current`로 직접
읽어서 문제없이 동작해서 더 헷갈렸음). → `AssignDefaultActions()`를 명시적으로 호출.

### 10. GameOver 브로드캐스트 순서 때문에 재입장이 영구히 막히던 레이스
부하테스트 봇을 **localhost**(RTT 0.03ms 수준)로 돌렸더니, 몇 라운드 지나면 매칭이
완전히 멈춰버리는 문제가 있었다 (같은 서버를 실제 AWS 네트워크로 때린 부하테스트에서는
1000봇까지도 전혀 재현되지 않던 문제 — 원인이 바로 이 지점).

`OneCardRoom::EndGame()`이 `GameOver`를 먼저 브로드캐스트하고 *그 다음에* 각 세션의
방 참조(`Session::oneCardRoom_`)를 지우는 순서로 되어 있었다. RTT가 거의 0인 환경에서는
클라이언트가 `GameOver`를 받자마자 곧바로 재입장(`C_OneCardJoinQueue`)을 보낼 수 있는데,
서버가 그 세션의 방 참조를 미처 다 지우기도 전에 도착하면 `HandleOneCardJoinQueue`가
"이미 방에 있음"으로 잘못 거부한다. 이 거부는 클라에게 응답 패킷을 보내주지 않기 때문에
거부당한 봇은 그 사실을 알 방법이 없어 재시도도 안 하고, 대기열은 영원히 그 인원만큼
모자란 채로 4명을 못 채워 전체 매칭이 멈춘다. 실제 네트워크(AWS, RTT 20~50ms대)에서는
이 타이밍 창이 거의 열리지 않아 한 번도 재현되지 않았던 것.

추가로 `Session::oneCardRoom_`이 매칭 스레드(`StartRoom`)와 여러 IO 스레드
(`EndGame`/`HandleDisconnect`)에서 락 없이 읽고 쓰이던 데이터 레이스이기도 했다.

→ (1) `EndGame`/`HandleDisconnect`에서 방 참조를 **먼저 지우고 나서** 브로드캐스트하도록
순서 변경, (2) `Session::oneCardRoom_`/`oneCardSeat_`에 `nickname_`처럼 전용 뮤텍스 추가.

## 프로젝트 구조
```
IOCP_Portfolio/       서버 (C++, IOCP)
Client/                부하테스트용 봇 클라이언트 (C++)
onecard_client/         클라이언트 (Unity)
protocol.proto          레거시 채팅 프로토콜
onecard.proto           원카드 게임 프로토콜
```

## 부하테스트

AWS EC2(t2.micro, 1 vCPU/1GB) 위에서 50~4000봇까지 단계적으로 돌린 결과, RTT
전후 비교(TCP_NODELAY 적용으로 p99 59ms → 11ms대), CloudWatch 리소스 사용량까지
전부 [LOADTEST.md](LOADTEST.md)에 정리했다.

## 알려진 제약사항 / 향후 개선 방향
- 패킷 최대 크기가 세션 스크래치 버퍼(1024바이트)로 고정돼 있음 — 더 큰 페이로드가
  필요해지면 가변 크기 버퍼나 분할 전송 도입 필요.
- 사용 중인 "2D Cards Game Art Pack" 에셋에는 조커 그림이 없어서, 조커는 클라이언트에서
  텍스트("JOKER")로 대체 표시됨.
- 연결이 끊긴 클라이언트의 소켓만 정리하고, `Stop()` 시 활성 연결들을 명시적으로 끊지는
  않음 (프로세스 종료 시 OS가 정리).
- 상점에서 산 아이템(카드 뒷면 스킨 등)이 인벤토리에는 정상 반영되지만, 실제 게임
  화면에 스킨을 적용하는 로직은 아직 없음 — 지금은 순수 트랜잭션(구매/보유 확인)까지만
  구현됨.
