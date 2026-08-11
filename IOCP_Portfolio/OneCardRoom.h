#pragma once

#include <vector>
#include <mutex>
#include <string>
#include <google/protobuf/message.h>
#include "onecard.pb.h"
#include "PacketHeader.h"

class Session;

// 원카드(트럼프 카드) 게임 방 하나의 상태와 규칙.
//
// 규칙은 onecard.proto 상단 주석 참고 (2 강제 드로우 스택, A 스킵,
// J/조커 무늬 지정, 조커 3장 강제 드로우, 손패 먼저 비우면 승리).
//
// 여러 플레이어의 패킷이 서로 다른 IO 스레드에서 동시에 들어올 수 있어서
// 공개 메서드는 전부 mutex_로 이 방의 게임 상태를 보호한다.
class OneCardRoom
{
public:
    explicit OneCardRoom(std::vector<Session *> players);

    void Start();
    void HandlePlayCard(Session *session, const onecard::Card &card, onecard::Suit declaredSuit);
    void HandleDrawCard(Session *session);
    void HandleDisconnect(Session *session);

private:
    struct Player
    {
        Session *session = nullptr;
        int seat = 0;
        std::string nickname;
        std::vector<onecard::Card> hand;
        bool connected = true;
    };

    int FindSeatBySession(Session *session) const;
    bool IsCardPlayable(const onecard::Card &card) const;
    bool RemoveFromHand(Player &player, const onecard::Card &card);
    // 카드 효과를 적용하고, 다음 턴으로 몇 칸 넘어갈지 반환한다 (스킵류는 2, 나머지는 1).
    int ApplyCardEffect(const onecard::Card &card, onecard::Suit declaredSuit);
    void AdvanceTurn(int steps);
    int NextConnectedSeatIndex(int fromSeat) const;
    onecard::Card DrawOneCardFromDeck();
    void ForceDraw(int seat, int count);
    void ReshuffleDiscardIntoDeck();
    void EndGame(int winnerSeat);

    void SendHand(const Player &player);
    void BroadcastGameState();
    void SendInvalidMove(Session *session, const std::string &reason);
    void SendPacket(Session *session, PacketId id, const google::protobuf::Message &msg);
    void BroadcastPacket(PacketId id, const google::protobuf::Message &msg);

    std::mutex mutex_;
    std::vector<Player> players_;
    std::vector<onecard::Card> deck_;
    std::vector<onecard::Card> discard_;
    onecard::Suit currentSuit_ = onecard::SUIT_NONE;
    int currentTurnSeat_ = 0;
    int pendingDraw_ = 0; // 2 카드 스택으로 다음 사람이 먹어야 할 장수
    bool finished_ = false;
};
