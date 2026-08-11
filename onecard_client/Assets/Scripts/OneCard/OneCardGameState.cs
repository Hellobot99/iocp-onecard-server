using System.Collections.Generic;
using Onecard;

namespace OneCardGame
{
    // 서버가 보내주는 패킷으로 갱신되는 원카드 게임 상태를 들고 있는 순수 데이터
    // 클래스. 아직 카드 이미지 등 표현 계층은 없고, 나중에 UI가 이 값을 그대로
    // 읽어서 그리면 된다.
    public class OneCardGameState
    {
        public int MySeat { get; private set; } = -1;
        public List<PlayerInfo> Players { get; } = new List<PlayerInfo>();

        public List<Card> MyHand { get; } = new List<Card>();
        public List<int> HandCounts { get; } = new List<int>();

        public Card TopCard { get; private set; }
        public Suit CurrentSuit { get; private set; } = Suit.None;
        public int CurrentTurnSeat { get; private set; } = -1;
        public int PendingDraw { get; private set; }
        public bool IsGameActive { get; private set; }

        public bool IsMyTurn => IsGameActive && MySeat >= 0 && MySeat == CurrentTurnSeat;

        public void ApplyGameStart(S_GameStart msg)
        {
            MySeat = msg.YourSeat;
            Players.Clear();
            Players.AddRange(msg.Players);
            MyHand.Clear();
            HandCounts.Clear();
            PendingDraw = 0;
            IsGameActive = true;
        }

        public void ApplyHandUpdate(S_HandUpdate msg)
        {
            MyHand.Clear();
            MyHand.AddRange(msg.Hand);
        }

        public void ApplyGameState(S_GameState msg)
        {
            HandCounts.Clear();
            HandCounts.AddRange(msg.HandCounts);
            TopCard = msg.TopCard;
            CurrentSuit = msg.CurrentSuit;
            CurrentTurnSeat = msg.CurrentTurnSeat;
            PendingDraw = msg.PendingDraw;
        }

        public void ApplyGameOver()
        {
            IsGameActive = false;
        }

        public string FindNickname(int seat)
        {
            foreach (var p in Players)
                if (p.Seat == seat)
                    return p.Nickname;
            return $"seat{seat}";
        }

        // 서버(OneCardRoom::IsCardPlayable + HandlePlayCard의 pendingDraw 체크)와
        // 동일한 규칙. UI에서 "낼 수 있는 카드"만 미리 활성화해서 보여주는 데 쓴다.
        // 실제 판정은 항상 서버가 최종 권한을 가지며, 이건 클라이언트 미리보기일 뿐이다.
        public bool IsCardPlayable(Card card)
        {
            if (PendingDraw > 0)
                return card.Rank == Rank.Two;

            if (card.Rank == Rank.Joker)
                return true;

            if (TopCard == null)
                return true;

            return card.Suit == CurrentSuit || card.Rank == TopCard.Rank;
        }
    }
}
