using Onecard;
using UnityEngine;

namespace OneCardGame
{
    // "2D Cards Game Art Pack"의 Standard Cards 스타일을 Suit/Rank로 찾아주는
    // 매퍼. 실제 스프라이트는 Assets/Resources/Cards/ 밑에 복사해뒀다
    // (원본 팩 경로에 있는 것을 그대로 쓰면 Resources.Load로 못 찾아서 복사함).
    //
    // 주의: 이 팩에는 조커 그림이 없다. GetFaceSprite(Joker)는 null을 반환하니
    // 호출부에서 null이면 텍스트("JOKER")로 대체해서 그려야 한다.
    public static class CardSpriteLibrary
    {
        public static Sprite GetFaceSprite(Suit suit, Rank rank)
        {
            if (rank == Rank.Joker || rank == Rank.Unspecified || suit == Suit.None)
                return null;

            string path = $"Cards/{SuitFolder(suit)}/{RankNumber(rank)}_{SuitFileSuffix(suit)}";
            Sprite sprite = Resources.Load<Sprite>(path);

            if (sprite == null)
                Debug.LogWarning($"[CardSpriteLibrary] 스프라이트를 못 찾음: {path}");

            return sprite;
        }

        public static Sprite GetBackSprite() => Resources.Load<Sprite>("Cards/CardBack/card_back");

        private static string SuitFolder(Suit suit) => suit switch
        {
            Suit.Spade => "Spades",
            Suit.Heart => "Hearts",
            Suit.Diamond => "Diamonds",
            Suit.Club => "Clubs",
            _ => "",
        };

        private static string SuitFileSuffix(Suit suit) => suit switch
        {
            Suit.Spade => "spade",
            Suit.Heart => "heart",
            Suit.Diamond => "diamond",
            Suit.Club => "club",
            _ => "",
        };

        // 팩의 파일명은 A=1, 2~10은 숫자 그대로, J=11, Q=12, K=13.
        private static int RankNumber(Rank rank) => rank switch
        {
            Rank.Ace => 1,
            Rank.Two => 2,
            Rank.Three => 3,
            Rank.Four => 4,
            Rank.Five => 5,
            Rank.Six => 6,
            Rank.Seven => 7,
            Rank.Eight => 8,
            Rank.Nine => 9,
            Rank.Ten => 10,
            Rank.Jack => 11,
            Rank.Queen => 12,
            Rank.King => 13,
            _ => 0,
        };
    }
}
