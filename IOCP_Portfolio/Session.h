#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <atomic>
#include "CustomOverlapped.h"
#include "PacketHeader.h"

class OneCardRoom;

class Session
{
public:
    Session();
    ~Session();

    void setId(int id) { id_ = id; }
    void setRecvOverlapped(CustomOVERLAPPED *recvOverlapped) { recvOverlapped_ = recvOverlapped; }
    void setSendOverlapped(CustomOVERLAPPED *sendOverlapped) { sendOverlapped_ = sendOverlapped; }

    int getid() { return id_; }
    CustomOVERLAPPED *getRecvOverlapped() { return recvOverlapped_; }
    CustomOVERLAPPED *getSendOverlapped() { return sendOverlapped_; }

    // 닉네임은 처음엔 접속 순번(예: "3")으로 채워지고, 로그인에 성공하면 계정
    // 아이디로 바뀐다. 로그인 처리는 JobQueue 워커 스레드에서, 그 외 읽기는 IO
    // 스레드에서 일어날 수 있어서 락으로 보호한다.
    void setNickname(const std::string &nickname);
    std::string getNickname();

    // 세션 풀에서 이 객체가 재사용될 때마다 증가하는 값. JobQueue처럼 비동기로
    // 나중에 실행되는 작업이 응답을 보내기 직전에, 자신이 캡처해둔 generation과
    // 지금 값을 비교해서 "그 사이에 연결이 끊기고 다른 연결이 이 Session
    // 객체를 재사용하지 않았는지" 확인하는 데 쓴다.
    uint64_t GetGeneration() const { return generation_.load(); }

    bool IsAuthenticated();
    // 로그인 성공 시 호출한다. username을 닉네임에도 반영한다.
    void SetAuthenticated(const std::string &username);

    // 지금 참여 중인 원카드 방 (없으면 nullptr). shared_ptr이라 방이 끝나서
    // 모든 세션이 참조를 놓으면 OneCardRoom이 자동으로 해제된다.
    // 매칭 스레드(StartRoom)와 여러 IO 스레드(EndGame/HandleDisconnect)가
    // 서로 다른 세션을 통해 동시에 건드릴 수 있어서 roomMutex_로 보호한다.
    void SetOneCardRoom(std::shared_ptr<OneCardRoom> room, int seat);
    std::shared_ptr<OneCardRoom> GetOneCardRoom();
    int getOneCardSeat();

    void Send(PacketId id, const char *data, DWORD len);
    void DoSend();
    void Recv();
    void OnRecvComplete(DWORD bytesTransferred);
    void OnSendComplete(DWORD bytesTransferred);

    void Reset();

    bool isReleased_ = false;

private:
    void DispatchPackets();

    int id_;
    bool sending_ = false;

    std::mutex identityMutex_;
    std::string nickname_;
    bool authenticated_ = false;

    std::atomic<uint64_t> generation_{0};

    CustomOVERLAPPED *sendOverlapped_;
    CustomOVERLAPPED *recvOverlapped_;
    std::queue<std::vector<char>> sendQueue_;
    std::mutex sendMutex_;
    // 현재 전송 중인(sendQueue_.front()) 패킷에서 이미 보낸 바이트 수.
    // WSASend는 요청한 바이트를 한 번에 다 못 보내고 일부만 보낼 수 있어서
    // (partial send), 완료 시 이만큼 다 보냈는지 확인하고 남은 만큼 이어 보낸다.
    size_t sendOffset_ = 0;

    // TCP는 스트림이라 recv 한 번이 패킷 하나와 1:1로 대응한다는 보장이 없다.
    // 패킷이 여러 recv에 걸쳐 쪼개지거나(partial), 여러 개가 한 recv에 붙어서
    // (coalesced) 올 수 있어서, 완전한 패킷이 만들어질 때까지 여기에 누적한다.
    std::vector<char> recvBuffer_;

    std::mutex roomMutex_;
    std::shared_ptr<OneCardRoom> oneCardRoom_;
    int oneCardSeat_ = -1;
};
