#include "OneCardRoom.h"
#include "Session.h"
#include <algorithm>
#include <random>

namespace
{
    std::vector<onecard::Card> BuildShuffledDeck()
    {
        std::vector<onecard::Card> deck;
        deck.reserve(54);

        const onecard::Suit suits[] = {onecard::SPADE, onecard::HEART, onecard::DIAMOND, onecard::CLUB};
        const onecard::Rank ranks[] = {
            onecard::ACE, onecard::TWO, onecard::THREE, onecard::FOUR, onecard::FIVE,
            onecard::SIX, onecard::SEVEN, onecard::EIGHT, onecard::NINE, onecard::TEN,
            onecard::JACK, onecard::QUEEN, onecard::KING,
        };

        for (onecard::Suit suit : suits)
        {
            for (onecard::Rank rank : ranks)
            {
                onecard::Card card;
                card.set_suit(suit);
                card.set_rank(rank);
                deck.push_back(card);
            }
        }

        for (int i = 0; i < 2; ++i)
        {
            onecard::Card joker;
            joker.set_suit(onecard::SUIT_NONE);
            joker.set_rank(onecard::JOKER);
            deck.push_back(joker);
        }

        static thread_local std::mt19937 rng(std::random_device{}());
        std::shuffle(deck.begin(), deck.end(), rng);
        return deck;
    }

    bool IsSpecialRank(onecard::Rank rank)
    {
        return rank == onecard::TWO || rank == onecard::ACE ||
               rank == onecard::JACK || rank == onecard::JOKER;
    }
}

OneCardRoom::OneCardRoom(std::vector<Session *> players)
{
    players_.reserve(players.size());
    for (size_t i = 0; i < players.size(); ++i)
    {
        Player p;
        p.session = players[i];
        p.seat = static_cast<int>(i);
        p.nickname = players[i]->getNickname();
        players_.push_back(std::move(p));
    }
}

void OneCardRoom::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);

    deck_ = BuildShuffledDeck();
    discard_.clear();

    constexpr int kInitialHandSize = 7;
    for (auto &p : players_)
    {
        p.hand.clear();
        for (int i = 0; i < kInitialHandSize; ++i)
            p.hand.push_back(DrawOneCardFromDeck());
    }

    // 시작 패는 특수 효과가 없는 카드가 나올 때까지 뽑는다. 2/A/J/조커로
    // 바로 시작하면 첫 턴이 시작되기도 전에 효과를 처리해야 해서 번거롭다.
    onecard::Card top;
    do
    {
        top = DrawOneCardFromDeck();
        discard_.push_back(top);
    } while (IsSpecialRank(top.rank()) && !deck_.empty());

    currentSuit_ = top.suit();
    currentTurnSeat_ = 0;
    pendingDraw_ = 0;
    finished_ = false;

    onecard::S_GameStart startMsg;
    for (auto &p : players_)
    {
        onecard::PlayerInfo *info = startMsg.add_players();
        info->set_seat(p.seat);
        info->set_nickname(p.nickname);
    }

    for (auto &p : players_)
    {
        onecard::S_GameStart personal = startMsg;
        personal.set_your_seat(p.seat);
        SendPacket(p.session, PacketId::S_OneCardGameStart, personal);
        SendHand(p);
    }

    BroadcastGameState();
}

void OneCardRoom::HandlePlayCard(Session *session, const onecard::Card &card, onecard::Suit declaredSuit)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (finished_)
        return;

    int seat = FindSeatBySession(session);
    if (seat < 0)
        return;

    Player &player = players_[seat];
    if (!player.connected)
        return;

    if (seat != currentTurnSeat_)
    {
        SendInvalidMove(session, "당신의 턴이 아닙니다.");
        return;
    }

    if (pendingDraw_ > 0 && card.rank() != onecard::TWO)
    {
        SendInvalidMove(session, "쌓인 드로우가 있어 2만 낼 수 있습니다. 없으면 드로우하세요.");
        return;
    }

    if (pendingDraw_ == 0 && !IsCardPlayable(card))
    {
        SendInvalidMove(session, "낼 수 없는 카드입니다.");
        return;
    }

    if ((card.rank() == onecard::JACK || card.rank() == onecard::JOKER) &&
        declaredSuit == onecard::SUIT_NONE)
    {
        SendInvalidMove(session, "J 또는 조커를 낼 때는 무늬를 지정해야 합니다.");
        return;
    }

    if (!RemoveFromHand(player, card))
    {
        SendInvalidMove(session, "보유하지 않은 카드입니다.");
        return;
    }

    onecard::Card playedCard = card;
    if (playedCard.rank() == onecard::JOKER)
        playedCard.set_suit(onecard::SUIT_NONE);

    discard_.push_back(playedCard);

    onecard::S_CardPlayed playedMsg;
    playedMsg.set_seat(seat);
    *playedMsg.mutable_card() = playedCard;
    BroadcastPacket(PacketId::S_OneCardCardPlayed, playedMsg);

    SendHand(player);

    // 손패를 비웠으면 그 자리에서 승리 처리하고 끝낸다. 카드 효과(조커의
    // 강제 드로우 등)는 게임이 끝난 뒤에는 적용하지 않는다.
    if (player.hand.empty())
    {
        EndGame(seat);
        return;
    }

    int advance = ApplyCardEffect(playedCard, declaredSuit);
    AdvanceTurn(advance);
    BroadcastGameState();
}

void OneCardRoom::HandleDrawCard(Session *session)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (finished_)
        return;

    int seat = FindSeatBySession(session);
    if (seat < 0)
        return;

    Player &player = players_[seat];
    if (!player.connected)
        return;

    if (seat != currentTurnSeat_)
    {
        SendInvalidMove(session, "당신의 턴이 아닙니다.");
        return;
    }

    int count = pendingDraw_ > 0 ? pendingDraw_ : 1;
    pendingDraw_ = 0;

    for (int i = 0; i < count; ++i)
        player.hand.push_back(DrawOneCardFromDeck());

    SendHand(player);

    onecard::S_CardDrawn drawnMsg;
    drawnMsg.set_seat(seat);
    drawnMsg.set_count(count);
    BroadcastPacket(PacketId::S_OneCardCardDrawn, drawnMsg);

    AdvanceTurn(1);
    BroadcastGameState();
}

void OneCardRoom::HandleDisconnect(Session *session)
{
    std::lock_guard<std::mutex> lock(mutex_);

    int seat = FindSeatBySession(session);
    if (seat < 0)
        return;

    players_[seat].connected = false;
    players_[seat].session = nullptr;

    if (finished_)
        return;

    int connectedCount = 0;
    int lastConnectedSeat = -1;
    for (auto &p : players_)
    {
        if (p.connected)
        {
            connectedCount++;
            lastConnectedSeat = p.seat;
        }
    }

    if (connectedCount < 2)
    {
        finished_ = true;

        // EndGame과 동일한 이유로 순서 고정: 방 참조를 먼저 지우고 나서 방송한다.
        for (auto &p : players_)
            if (p.session)
                p.session->SetOneCardRoom(nullptr, -1);

        if (lastConnectedSeat >= 0)
        {
            onecard::S_GameOver overMsg;
            overMsg.set_winner_seat(lastConnectedSeat);
            overMsg.set_winner_nickname(players_[lastConnectedSeat].nickname);
            BroadcastPacket(PacketId::S_OneCardGameOver, overMsg);
        }
        return;
    }

    if (seat == currentTurnSeat_)
        AdvanceTurn(1);

    BroadcastGameState();
}

int OneCardRoom::FindSeatBySession(Session *session) const
{
    for (auto &p : players_)
        if (p.session == session)
            return p.seat;
    return -1;
}

bool OneCardRoom::IsCardPlayable(const onecard::Card &card) const
{
    if (card.rank() == onecard::JOKER)
        return true;
    if (discard_.empty())
        return true;

    const onecard::Card &top = discard_.back();
    return card.suit() == currentSuit_ || card.rank() == top.rank();
}

bool OneCardRoom::RemoveFromHand(Player &player, const onecard::Card &card)
{
    for (size_t i = 0; i < player.hand.size(); ++i)
    {
        const onecard::Card &c = player.hand[i];
        bool same = (card.rank() == onecard::JOKER)
                        ? (c.rank() == onecard::JOKER)
                        : (c.suit() == card.suit() && c.rank() == card.rank());
        if (same)
        {
            player.hand.erase(player.hand.begin() + i);
            return true;
        }
    }
    return false;
}

int OneCardRoom::ApplyCardEffect(const onecard::Card &card, onecard::Suit declaredSuit)
{
    switch (card.rank())
    {
    case onecard::TWO:
        currentSuit_ = card.suit();
        pendingDraw_ += 2;
        return 1;

    case onecard::ACE:
        currentSuit_ = card.suit();
        return 2; // 다음 사람 턴 스킵

    case onecard::JACK:
        currentSuit_ = declaredSuit;
        return 1;

    case onecard::JOKER:
    {
        currentSuit_ = declaredSuit;
        int victimSeat = NextConnectedSeatIndex(currentTurnSeat_);
        ForceDraw(victimSeat, 3);
        return 2; // 강제로 먹은 사람의 턴은 건너뛴다
    }

    default:
        currentSuit_ = card.suit();
        return 1;
    }
}

void OneCardRoom::AdvanceTurn(int steps)
{
    for (int i = 0; i < steps; ++i)
        currentTurnSeat_ = NextConnectedSeatIndex(currentTurnSeat_);
}

int OneCardRoom::NextConnectedSeatIndex(int fromSeat) const
{
    int n = static_cast<int>(players_.size());
    int idx = fromSeat;
    for (int i = 0; i < n; ++i)
    {
        idx = (idx + 1) % n;
        if (players_[idx].connected)
            return idx;
    }
    return fromSeat; // 전원 접속 종료 등 예외 상황 - 원래 자리 유지
}

onecard::Card OneCardRoom::DrawOneCardFromDeck()
{
    if (deck_.empty())
        ReshuffleDiscardIntoDeck();

    if (deck_.empty())
    {
        // 54장이 전부 손패에 있는 극단적인 경우 (사실상 발생하기 어렵다).
        // 크래시 대신 빈 카드를 반환해서 방어한다.
        return onecard::Card();
    }

    onecard::Card card = deck_.back();
    deck_.pop_back();
    return card;
}

void OneCardRoom::ForceDraw(int seat, int count)
{
    Player &p = players_[seat];
    for (int i = 0; i < count; ++i)
        p.hand.push_back(DrawOneCardFromDeck());

    SendHand(p);

    onecard::S_CardDrawn msg;
    msg.set_seat(seat);
    msg.set_count(count);
    BroadcastPacket(PacketId::S_OneCardCardDrawn, msg);
}

void OneCardRoom::ReshuffleDiscardIntoDeck()
{
    if (discard_.size() <= 1)
        return; // 섞을 카드가 없음

    onecard::Card top = discard_.back();
    deck_.assign(discard_.begin(), discard_.end() - 1);
    discard_.clear();
    discard_.push_back(top);

    static thread_local std::mt19937 rng(std::random_device{}());
    std::shuffle(deck_.begin(), deck_.end(), rng);
}

void OneCardRoom::EndGame(int winnerSeat)
{
    finished_ = true;

    // 방 참조를 먼저 지운 뒤에 브로드캐스트해야 한다. 순서가 바뀌면, 클라가
    // GameOver를 받자마자 곧바로 재입장을 시도했을 때(RTT가 거의 0인
    // 로컬 환경에서 실제로 재현됨) 그 세션이 아직 지워지지 않은 옛 방
    // 참조를 들고 있어서 "이미 방에 있음"으로 재입장이 거부된다. 이 거부는
    // 클라에게 응답 패킷으로 알려주지 않기 때문에, 거부당한 봇은 그 사실을
    // 알 방법이 없어 영영 대기열에서 빠진 채로 남는다 (매칭이 4명을 다시
    // 못 채워서 전체 매칭이 멈춰버림).
    for (auto &p : players_)
        if (p.session)
            p.session->SetOneCardRoom(nullptr, -1);

    onecard::S_GameOver overMsg;
    overMsg.set_winner_seat(winnerSeat);
    overMsg.set_winner_nickname(players_[winnerSeat].nickname);
    BroadcastPacket(PacketId::S_OneCardGameOver, overMsg);
}

void OneCardRoom::SendHand(const Player &player)
{
    if (!player.connected || !player.session)
        return;

    onecard::S_HandUpdate msg;
    for (auto &c : player.hand)
        *msg.add_hand() = c;

    SendPacket(player.session, PacketId::S_OneCardHandUpdate, msg);
}

void OneCardRoom::BroadcastGameState()
{
    onecard::S_GameState state;
    for (auto &p : players_)
        state.add_hand_counts(static_cast<int32_t>(p.hand.size()));
    if (!discard_.empty())
        *state.mutable_top_card() = discard_.back();
    state.set_current_suit(currentSuit_);
    state.set_current_turn_seat(currentTurnSeat_);
    state.set_pending_draw(pendingDraw_);

    BroadcastPacket(PacketId::S_OneCardGameState, state);
}

void OneCardRoom::SendInvalidMove(Session *session, const std::string &reason)
{
    onecard::S_InvalidMove msg;
    msg.set_reason(reason);
    SendPacket(session, PacketId::S_OneCardInvalidMove, msg);
}

void OneCardRoom::SendPacket(Session *session, PacketId id, const google::protobuf::Message &msg)
{
    if (!session)
        return;

    std::string buf;
    msg.SerializeToString(&buf);
    session->Send(id, buf.data(), static_cast<DWORD>(buf.size()));
}

void OneCardRoom::BroadcastPacket(PacketId id, const google::protobuf::Message &msg)
{
    std::string buf;
    msg.SerializeToString(&buf);
    for (auto &p : players_)
    {
        if (!p.connected || !p.session)
            continue;
        p.session->Send(id, buf.data(), static_cast<DWORD>(buf.size()));
    }
}
