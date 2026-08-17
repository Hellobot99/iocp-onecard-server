using System;
using System.Linq;
using Google.Protobuf;
using Onecard;
using Auth;
using UnityEngine;

namespace OneCardGame
{
    // C++ 서버(PacketHeader.h)와 값이 반드시 일치해야 하는 패킷 ID.
    // protocol.proto(채팅) 쪽 ID는 이 클라이언트가 쓰지는 않지만, 번호가 겹치지
    // 않는다는 걸 보여주려고 그대로 나열해둔다.
    public enum PacketId : ushort
    {
        S_Enter = 1,
        C_Enter = 2,
        S_Leave = 3,
        C_Leave = 4,
        S_Chat = 5,
        C_Chat = 6,

        C_OneCardJoinQueue = 7,
        C_OneCardLeaveQueue = 8,
        S_OneCardQueueStatus = 9,
        S_OneCardGameStart = 10,

        C_OneCardPlayCard = 11,
        C_OneCardDrawCard = 12,
        S_OneCardHandUpdate = 13,
        S_OneCardGameState = 14,
        S_OneCardCardPlayed = 15,
        S_OneCardCardDrawn = 16,
        S_OneCardInvalidMove = 17,
        S_OneCardGameOver = 18,

        C_SignUp = 19,
        S_SignUpResult = 20,
        C_Login = 21,
        S_LoginResult = 22,
    }

    // 원카드 서버 접속 + 패킷 송수신을 담당하는 클라이언트.
    // 씬에 직접 배치하지 않아도 RuntimeInitializeOnLoadMethod로 자동 생성된다
    // (카드 이미지/UI가 아직 없어서, 우선 코드만으로 테스트할 수 있게 하기 위함).
    public class OneCardClient : MonoBehaviour
    {
        public static OneCardClient Instance { get; private set; }

        [Header("서버 접속 정보")]
        public string serverHost = "127.0.0.1";
        public int serverPort = 9000;
        public bool autoConnectOnStart = true;

        // 상점/인벤토리 REST API 포트. 게임 포트(TCP, serverPort)와 별개로 뜬다
        // (서버 쪽 HttpServer.cpp 참고).
        public int shopPort = 8080;

        public OneCardGameState State { get; } = new OneCardGameState();
        public bool IsConnected => _net != null && _net.IsConnected;

        // 로그인 상태와 골드는 로그인 성공(S_LoginResult) 시점에 갱신된다.
        public bool IsAuthenticated { get; private set; }
        public string Username { get; private set; }
        public int Gold { get; private set; }

        public event Action OnConnected;
        public event Action<Exception> OnDisconnected;
        public event Action<S_SignUpResult> OnSignUpResult;
        public event Action<S_LoginResult> OnLoginResult;
        public event Action<S_QueueStatus> OnQueueStatus;
        public event Action<S_GameStart> OnGameStart;
        public event Action<S_HandUpdate> OnHandUpdate;
        public event Action<S_GameState> OnGameState;
        public event Action<S_CardPlayed> OnCardPlayed;
        public event Action<S_CardDrawn> OnCardDrawn;
        public event Action<S_InvalidMove> OnInvalidMove;
        public event Action<S_GameOver> OnGameOver;

        private NetworkClient _net;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        private static void Bootstrap()
        {
            if (Instance != null)
                return;

            var go = new GameObject("OneCardClient");
            DontDestroyOnLoad(go);
            go.AddComponent<OneCardClient>();
            go.AddComponent<ShopClient>();
            go.AddComponent<OneCardDebugController>();
            go.AddComponent<OneCardView>();
        }

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }
            Instance = this;
        }

        private void Start()
        {
            if (autoConnectOnStart)
                Connect();
        }

        public void Connect()
        {
            // 재접속(다른 서버로 다시 접속 등) 시 이전 연결이 남아있으면 정리한다.
            _net?.Dispose();

            _net = new NetworkClient();
            _net.OnDisconnected += OnNetDisconnected;

            try
            {
                _net.Connect(serverHost, serverPort);
                Debug.Log($"[OneCardClient] {serverHost}:{serverPort} 접속 성공");
                OnConnected?.Invoke();
            }
            catch (Exception e)
            {
                Debug.LogError($"[OneCardClient] 접속 실패: {e.Message}");
            }
        }

        // 씬에 배치하지 않고 런타임에 자동 생성되는 구조라 Inspector로 서버 주소를
        // 바꿀 방법이 없다. 대신 접속 화면(OneCardView)에서 IP를 입력받아 이걸
        // 호출한다 - 게임 포트/상점 포트는 프로토콜상 고정이라 host만 받는다.
        public void ConnectTo(string host)
        {
            serverHost = string.IsNullOrWhiteSpace(host) ? "127.0.0.1" : host.Trim();
            IsAuthenticated = false;
            Username = null;
            Gold = 0;
            Connect();
        }

        // NetworkClient의 수신 스레드에서 호출된다. OnDisconnected 구독자(예:
        // OneCardView)가 UnityEngine API를 건드릴 수 있어서, 여기서 직접 이벤트를
        // 쏘지 않고 큐에 담아뒀다가 메인 스레드(Update)에서 처리한다.
        private readonly System.Collections.Concurrent.ConcurrentQueue<Exception> _pendingDisconnects = new System.Collections.Concurrent.ConcurrentQueue<Exception>();

        private void OnNetDisconnected(Exception e)
        {
            _pendingDisconnects.Enqueue(e);
        }

        private void Update()
        {
            if (_net == null)
                return;

            while (_pendingDisconnects.TryDequeue(out Exception ex))
            {
                Debug.Log($"[OneCardClient] 서버 연결 끊김: {ex?.Message ?? "정상 종료"}");
                IsAuthenticated = false;
                Username = null;
                Gold = 0;
                OnDisconnected?.Invoke(ex);
            }

            while (_net.IncomingPackets.TryDequeue(out var packet))
                Dispatch(packet.id, packet.payload);
        }

        private void Dispatch(ushort id, byte[] payload)
        {
            switch ((PacketId)id)
            {
                case PacketId.S_SignUpResult:
                {
                    var msg = S_SignUpResult.Parser.ParseFrom(payload);
                    Debug.Log($"[OneCardClient] 회원가입 결과: {msg.Success} - {msg.Message}");
                    OnSignUpResult?.Invoke(msg);
                    break;
                }
                case PacketId.S_LoginResult:
                {
                    var msg = S_LoginResult.Parser.ParseFrom(payload);
                    Debug.Log($"[OneCardClient] 로그인 결과: {msg.Success} - {msg.Message}");
                    if (msg.Success)
                    {
                        IsAuthenticated = true;
                        Gold = msg.Gold;
                    }
                    OnLoginResult?.Invoke(msg);
                    break;
                }
                case PacketId.S_OneCardQueueStatus:
                {
                    var msg = S_QueueStatus.Parser.ParseFrom(payload);
                    Debug.Log($"[OneCardClient] 대기열 인원: {msg.WaitingCount}명");
                    OnQueueStatus?.Invoke(msg);
                    break;
                }
                case PacketId.S_OneCardGameStart:
                {
                    var msg = S_GameStart.Parser.ParseFrom(payload);
                    State.ApplyGameStart(msg);
                    Debug.Log($"[OneCardClient] 게임 시작! 내 좌석: {msg.YourSeat}, 참가자: {string.Join(", ", msg.Players.Select(p => $"{p.Seat}:{p.Nickname}"))}");
                    OnGameStart?.Invoke(msg);
                    break;
                }
                case PacketId.S_OneCardHandUpdate:
                {
                    var msg = S_HandUpdate.Parser.ParseFrom(payload);
                    State.ApplyHandUpdate(msg);
                    Debug.Log($"[OneCardClient] 내 손패({State.MyHand.Count}장): {string.Join(", ", State.MyHand.Select(CardText))}");
                    OnHandUpdate?.Invoke(msg);
                    break;
                }
                case PacketId.S_OneCardGameState:
                {
                    var msg = S_GameState.Parser.ParseFrom(payload);
                    State.ApplyGameState(msg);
                    Debug.Log($"[OneCardClient] 턴: {msg.CurrentTurnSeat}번, 바닥패: {CardText(msg.TopCard)}, 기준무늬: {msg.CurrentSuit}, 밀린 드로우: {msg.PendingDraw}");
                    OnGameState?.Invoke(msg);
                    break;
                }
                case PacketId.S_OneCardCardPlayed:
                {
                    var msg = S_CardPlayed.Parser.ParseFrom(payload);
                    Debug.Log($"[OneCardClient] {State.FindNickname(msg.Seat)}가 {CardText(msg.Card)} 냄");
                    OnCardPlayed?.Invoke(msg);
                    break;
                }
                case PacketId.S_OneCardCardDrawn:
                {
                    var msg = S_CardDrawn.Parser.ParseFrom(payload);
                    Debug.Log($"[OneCardClient] {State.FindNickname(msg.Seat)}가 {msg.Count}장 드로우함");
                    OnCardDrawn?.Invoke(msg);
                    break;
                }
                case PacketId.S_OneCardInvalidMove:
                {
                    var msg = S_InvalidMove.Parser.ParseFrom(payload);
                    Debug.LogWarning($"[OneCardClient] 서버가 거부한 행동: {msg.Reason}");
                    OnInvalidMove?.Invoke(msg);
                    break;
                }
                case PacketId.S_OneCardGameOver:
                {
                    var msg = S_GameOver.Parser.ParseFrom(payload);
                    State.ApplyGameOver();
                    Debug.Log($"[OneCardClient] 게임 종료! 승자: {msg.WinnerNickname} (좌석 {msg.WinnerSeat})");
                    OnGameOver?.Invoke(msg);
                    break;
                }
                default:
                    // 이 클라이언트가 모르는 패킷(예: 채팅)은 무시한다.
                    break;
            }
        }

        // ── 서버로 보내는 요청 ───────────────────────────────────────────

        public void SignUp(string username, string password) =>
            Send(PacketId.C_SignUp, new C_SignUp { Username = username, Password = password });

        public void Login(string username, string password)
        {
            Username = username;
            Send(PacketId.C_Login, new C_Login { Username = username, Password = password });
        }

        // 골드는 로그인(TCP) 시점에만 서버가 알려준다. 상점 구매(HTTP)는 별도
        // 통신이라 서버가 알아서 밀어주지 않으므로, 구매 응답을 받은 뒤 여기로
        // 반영해줘야 화면에 보이는 골드가 실제 값과 어긋나지 않는다.
        public void SetGold(int gold) => Gold = gold;

        public void JoinQueue() => Send(PacketId.C_OneCardJoinQueue, new C_JoinQueue());

        public void LeaveQueue() => Send(PacketId.C_OneCardLeaveQueue, new C_LeaveQueue());

        public void PlayCard(Card card, Suit declaredSuit = Suit.None) =>
            Send(PacketId.C_OneCardPlayCard, new C_PlayCard { Card = card, DeclaredSuit = declaredSuit });

        public void DrawCard() => Send(PacketId.C_OneCardDrawCard, new C_DrawCard());

        // J/조커처럼 무늬 선언이 필요한 카드는 손패에서 가장 많은 무늬를 자동으로
        // 골라준다. UI/디버그 컨트롤러가 공통으로 쓰는 헬퍼.
        public void PlayCardAuto(Card card)
        {
            Suit declaredSuit = Suit.None;
            if (card.Rank == Rank.Jack || card.Rank == Rank.Joker)
                declaredSuit = GuessBestSuit(State.MyHand, card);

            PlayCard(card, declaredSuit);
        }

        public static Suit GuessBestSuit(System.Collections.Generic.List<Card> hand, Card excluding)
        {
            var counts = new System.Collections.Generic.Dictionary<Suit, int>();
            foreach (Card c in hand)
            {
                if (c.Equals(excluding) || c.Suit == Suit.None)
                    continue;

                counts.TryGetValue(c.Suit, out int n);
                counts[c.Suit] = n + 1;
            }

            if (counts.Count == 0)
                return Suit.Spade; // 손패가 전부 조커뿐인 극단적인 경우의 기본값

            return counts.OrderByDescending(kv => kv.Value).First().Key;
        }

        private void Send(PacketId id, IMessage message)
        {
            if (!IsConnected)
            {
                Debug.LogWarning("[OneCardClient] 서버에 연결되어 있지 않습니다.");
                return;
            }

            _net.Send((ushort)id, message.ToByteArray());
        }

        public static string CardText(Card card)
        {
            if (card == null)
                return "(없음)";
            return card.Rank == Rank.Joker ? "조커" : $"{card.Suit} {card.Rank}";
        }

        private void OnDestroy()
        {
            _net?.Dispose();
        }
    }
}
