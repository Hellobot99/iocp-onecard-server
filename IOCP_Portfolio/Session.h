#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include "CustomOverlapped.h"
#include "PacketHeader.h"

class OneCardRoom;

class Session
{
public:
    Session();
    ~Session();

    void setId(int id) { id_ = id; }
    void setNickname(std::string nickname) { nickname_ = nickname; }
    void setRecvOverlapped(CustomOVERLAPPED *recvOverlapped) { recvOverlapped_ = recvOverlapped; }
    void setSendOverlapped(CustomOVERLAPPED *sendOverlapped) { sendOverlapped_ = sendOverlapped; }

    int getid() { return id_; }
    std::string getNickname() { return nickname_; }
    CustomOVERLAPPED *getRecvOverlapped() { return recvOverlapped_; }
    CustomOVERLAPPED *getSendOverlapped() { return sendOverlapped_; }

    // 지금 참여 중인 원카드 방 (없으면 nullptr). shared_ptr이라 방이 끝나서
    // 모든 세션이 참조를 놓으면 OneCardRoom이 자동으로 해제된다.
    void SetOneCardRoom(std::shared_ptr<OneCardRoom> room, int seat)
    {
        oneCardRoom_ = std::move(room);
        oneCardSeat_ = seat;
    }
    std::shared_ptr<OneCardRoom> GetOneCardRoom() { return oneCardRoom_; }
    int getOneCardSeat() { return oneCardSeat_; }

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

    std::string nickname_;
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

    std::shared_ptr<OneCardRoom> oneCardRoom_;
    int oneCardSeat_ = -1;
};
