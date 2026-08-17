using System.Collections.Generic;
using Onecard;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem.UI;
using UnityEngine.UI;

namespace OneCardGame
{
    // "2D Cards Game Art Pack"(Standard Cards 스타일) 스프라이트로 게임 상태를
    // 보여주는 실제 카드 UI. 씬/프리팹 작업 없이 전부 코드로 Canvas를 만든다.
    // OneCardClient의 이벤트를 구독해서 상태가 바뀔 때마다 다시 그린다.
    public class OneCardView : MonoBehaviour
    {
        private Font _font;

        private Text _statusText;

        private GameObject _connectPanel;
        private InputField _serverHostInput;

        private GameObject _loginPanel;
        private InputField _usernameInput;
        private InputField _passwordInput;
        private Text _authStatusText;

        private GameObject _gameRoot;
        private Transform _opponentsRow;
        private Image _topCardImage;
        private Text _topCardLabel;
        private Text _suitPendingText;
        private Transform _handRow;
        private Button _joinButton;
        private Button _leaveButton;
        private Button _shopButton;
        private Button _drawButton;

        // 상점(HTTP) - 게임 로직과 무관해서 게임 상태와는 별도의 플래그로 열림/닫힘을 관리한다.
        private bool _shopOpen;
        private GameObject _shopPanel;
        private Text _shopStatusText;
        private Transform _shopItemsColumn;
        private Transform _inventoryColumn;

        private void Start()
        {
            _font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");

            BuildUI();

            OneCardClient client = OneCardClient.Instance;
            client.OnConnected += RefreshAll;
            client.OnDisconnected += _ => RefreshAll();
            client.OnSignUpResult += msg => _authStatusText.text = msg.Message;
            client.OnLoginResult += msg =>
            {
                _authStatusText.text = msg.Message;
                RefreshAll();
            };
            client.OnGameStart += _ => RefreshAll();
            client.OnHandUpdate += _ => RefreshAll();
            client.OnGameState += _ => RefreshAll();
            client.OnGameOver += _ => RefreshAll();
            client.OnQueueStatus += _ => RefreshAll();

            RefreshAll();
        }

        // ── 상태 갱신 ────────────────────────────────────────────────────

        private void RefreshAll()
        {
            OneCardClient client = OneCardClient.Instance;
            if (client == null || _statusText == null)
                return;

            ClearChildren(_opponentsRow);
            ClearChildren(_handRow);

            if (!client.IsConnected)
            {
                _statusText.text = "서버에 연결되어 있지 않습니다. 아래에서 서버 IP를 입력하고 접속하세요.";
                _connectPanel.SetActive(true);
                _loginPanel.SetActive(false);
                _gameRoot.SetActive(false);
                _shopPanel.SetActive(false);
                return;
            }

            _connectPanel.SetActive(false);

            if (!client.IsAuthenticated)
            {
                _statusText.text = "로그인이 필요합니다.";
                _loginPanel.SetActive(true);
                _gameRoot.SetActive(false);
                _shopPanel.SetActive(false);
                return;
            }

            _loginPanel.SetActive(false);

            if (_shopOpen)
            {
                _gameRoot.SetActive(false);
                _shopPanel.SetActive(true);
                return; // 상점 화면 내용은 RefreshShopData()가 비동기로 채운다.
            }

            _shopPanel.SetActive(false);
            _gameRoot.SetActive(true);

            OneCardGameState state = client.State;

            if (!state.IsGameActive)
            {
                _statusText.text = $"{client.Username} 님 환영합니다 (골드 {client.Gold}) - 대기열에 참가하면 인원이 모이는 대로 매칭됩니다.";
                SetTopCard(null);
                _suitPendingText.text = "";
                SetLobbyButtonsVisible(true);
                _drawButton.gameObject.SetActive(false);
                return;
            }

            string turnMark = state.IsMyTurn ? "  ← 내 턴!" : "";
            _statusText.text = $"내 좌석: {state.MySeat}   현재 턴: {state.CurrentTurnSeat}번 ({state.FindNickname(state.CurrentTurnSeat)}){turnMark}";

            BuildOpponents(state);
            SetTopCard(state.TopCard);
            _suitPendingText.text = $"지금 낼 수 있는 무늬: {state.CurrentSuit} (또는 {state.TopCard?.Rank})" +
                                     (state.PendingDraw > 0 ? $"   ⚠ 밀린 드로우 {state.PendingDraw}장 (2 없으면 드로우)" : "") +
                                     "\n※ J를 내면 카드 자체 무늬가 아니라 그때 선언한 무늬가 기준이 됩니다.";
            BuildHand(state);

            SetLobbyButtonsVisible(false);
            _drawButton.gameObject.SetActive(true);
            _drawButton.interactable = state.IsMyTurn;
        }

        private void SetLobbyButtonsVisible(bool show)
        {
            _joinButton.gameObject.SetActive(show);
            _leaveButton.gameObject.SetActive(show);
            _shopButton.gameObject.SetActive(show);
        }

        private void SetTopCard(Card card)
        {
            if (card == null)
            {
                _topCardImage.sprite = null;
                _topCardImage.color = new Color(0, 0, 0, 0);
                _topCardLabel.text = "";
                return;
            }

            SetCardImage(_topCardImage, card.Suit, card.Rank);
            _topCardLabel.text = $"바닥패: {OneCardClient.CardText(card)}";
        }

        private void BuildOpponents(OneCardGameState state)
        {
            foreach (PlayerInfo p in state.Players)
            {
                if (p.Seat == state.MySeat)
                    continue;

                var panel = new GameObject($"Opponent_{p.Seat}", typeof(RectTransform));
                panel.transform.SetParent(_opponentsRow, false);
                var vlayout = panel.AddComponent<VerticalLayoutGroup>();
                vlayout.childAlignment = TextAnchor.MiddleCenter;
                vlayout.spacing = 2;
                var panelLe = panel.AddComponent<LayoutElement>();
                panelLe.preferredWidth = 90;

                bool isTurn = p.Seat == state.CurrentTurnSeat;
                CreateText(panel.transform, (isTurn ? "▶ " : "") + p.Nickname, 14, TextAnchor.MiddleCenter);

                var backGo = new GameObject("Back", typeof(RectTransform));
                backGo.transform.SetParent(panel.transform, false);
                var backImg = backGo.AddComponent<Image>();
                backImg.sprite = CardSpriteLibrary.GetBackSprite();
                backImg.preserveAspect = true;
                var backLe = backGo.AddComponent<LayoutElement>();
                backLe.preferredWidth = 50;
                backLe.preferredHeight = 72;

                int count = p.Seat < state.HandCounts.Count ? state.HandCounts[p.Seat] : 0;
                CreateText(panel.transform, $"{count}장", 14, TextAnchor.MiddleCenter);
            }
        }

        private void BuildHand(OneCardGameState state)
        {
            foreach (Card card in state.MyHand)
            {
                bool playable = state.IsMyTurn && state.IsCardPlayable(card);
                CreateCardButton(_handRow, card, playable, () => OneCardClient.Instance.PlayCardAuto(card));
            }
        }

        // ── UI 생성 (전부 코드로만, 씬/프리팹 불필요) ───────────────────────

        private void BuildUI()
        {
            var canvasGo = new GameObject("OneCardCanvas");
            canvasGo.transform.SetParent(transform, false);
            var canvas = canvasGo.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            var scaler = canvasGo.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1280, 720);
            canvasGo.AddComponent<GraphicRaycaster>();

            EnsureEventSystem();

            var root = new GameObject("Root", typeof(RectTransform));
            root.transform.SetParent(canvasGo.transform, false);
            var rootRect = root.GetComponent<RectTransform>();
            rootRect.anchorMin = Vector2.zero;
            rootRect.anchorMax = Vector2.one;
            rootRect.offsetMin = Vector2.zero;
            rootRect.offsetMax = Vector2.zero;
            var rootLayout = root.AddComponent<VerticalLayoutGroup>();
            rootLayout.padding = new RectOffset(20, 20, 20, 20);
            rootLayout.spacing = 12;
            rootLayout.childForceExpandWidth = true;
            rootLayout.childForceExpandHeight = false;
            rootLayout.childAlignment = TextAnchor.UpperLeft;

            _statusText = CreateText(root.transform, "연결 중...", 22, TextAnchor.UpperLeft);

            BuildConnectPanel(root.transform);
            BuildLoginPanel(root.transform);

            _gameRoot = new GameObject("GameRoot", typeof(RectTransform));
            _gameRoot.transform.SetParent(root.transform, false);
            var gameLayout = _gameRoot.AddComponent<VerticalLayoutGroup>();
            gameLayout.spacing = 12;
            gameLayout.childForceExpandWidth = true;
            gameLayout.childForceExpandHeight = false;
            gameLayout.childAlignment = TextAnchor.UpperLeft;
            _gameRoot.AddComponent<LayoutElement>();

            CreateText(_gameRoot.transform, "다른 플레이어", 14, TextAnchor.UpperLeft);
            _opponentsRow = CreateRow(_gameRoot.transform, "Opponents");

            var topCardRow = CreateRow(_gameRoot.transform, "TopCardRow");
            var topCardGo = new GameObject("TopCardImage", typeof(RectTransform));
            topCardGo.transform.SetParent(topCardRow, false);
            _topCardImage = topCardGo.AddComponent<Image>();
            _topCardImage.preserveAspect = true;
            var topLe = topCardGo.AddComponent<LayoutElement>();
            topLe.preferredWidth = 80;
            topLe.preferredHeight = 115;
            _topCardLabel = CreateText(topCardRow, "", 16, TextAnchor.MiddleLeft);

            _suitPendingText = CreateText(_gameRoot.transform, "", 16, TextAnchor.UpperLeft);
            _suitPendingText.GetComponent<LayoutElement>().preferredHeight = 48; // 두 줄까지 표시

            CreateText(_gameRoot.transform, "내 손패 (본인 턴일 때만 클릭 가능)", 14, TextAnchor.UpperLeft);
            _handRow = CreateRow(_gameRoot.transform, "Hand");

            var buttonRow = CreateRow(_gameRoot.transform, "Buttons");
            _joinButton = CreateButton(buttonRow, "대기열 참가", () => OneCardClient.Instance.JoinQueue());
            _leaveButton = CreateButton(buttonRow, "대기열 나가기", () => OneCardClient.Instance.LeaveQueue());
            _shopButton = CreateButton(buttonRow, "상점", OpenShop);
            _drawButton = CreateButton(buttonRow, "드로우", () => OneCardClient.Instance.DrawCard());

            BuildShopPanel(root.transform);
        }

        // 서버 IP 입력 패널. 씬에 배치하지 않고 코드로 자동 생성되는 구조라
        // Inspector로 서버 주소를 바꿀 방법이 없어서, 연결 전 화면에서 직접
        // 입력받는다 (로컬 개발 중엔 기본값 127.0.0.1로 자동 접속 시도하고,
        // 실패하면 이 패널이 뜬다).
        private void BuildConnectPanel(Transform parent)
        {
            _connectPanel = new GameObject("ConnectPanel", typeof(RectTransform));
            _connectPanel.transform.SetParent(parent, false);
            var layout = _connectPanel.AddComponent<VerticalLayoutGroup>();
            layout.spacing = 8;
            layout.childAlignment = TextAnchor.UpperLeft;
            layout.childForceExpandWidth = false;
            _connectPanel.AddComponent<LayoutElement>();

            CreateText(_connectPanel.transform, "서버 접속", 18, TextAnchor.UpperLeft);
            _serverHostInput = CreateInputField(_connectPanel.transform, "서버 IP (기본 127.0.0.1)", false);
            _serverHostInput.text = OneCardClient.Instance.serverHost;

            CreateButton(_connectPanel.transform, "접속", () => OneCardClient.Instance.ConnectTo(_serverHostInput.text));
        }

        // 로그인/회원가입 패널. 연결은 됐지만 아직 인증 전일 때만 보인다.
        private void BuildLoginPanel(Transform parent)
        {
            _loginPanel = new GameObject("LoginPanel", typeof(RectTransform));
            _loginPanel.transform.SetParent(parent, false);
            var layout = _loginPanel.AddComponent<VerticalLayoutGroup>();
            layout.spacing = 8;
            layout.childAlignment = TextAnchor.UpperLeft;
            layout.childForceExpandWidth = false;
            _loginPanel.AddComponent<LayoutElement>();

            CreateText(_loginPanel.transform, "로그인 / 회원가입", 18, TextAnchor.UpperLeft);
            _usernameInput = CreateInputField(_loginPanel.transform, "아이디 (3~20자)", false);
            _passwordInput = CreateInputField(_loginPanel.transform, "비밀번호 (4자 이상)", true);
            _authStatusText = CreateText(_loginPanel.transform, "", 14, TextAnchor.UpperLeft);

            var buttonRow = CreateRow(_loginPanel.transform, "AuthButtons");
            CreateButton(buttonRow, "로그인", () => OneCardClient.Instance.Login(_usernameInput.text, _passwordInput.text));
            CreateButton(buttonRow, "회원가입", () => OneCardClient.Instance.SignUp(_usernameInput.text, _passwordInput.text));
        }

        // ── 상점 (HTTP, TCP 게임 프로토콜과 무관) ───────────────────────────

        private void BuildShopPanel(Transform parent)
        {
            _shopPanel = new GameObject("ShopPanel", typeof(RectTransform));
            _shopPanel.transform.SetParent(parent, false);
            var layout = _shopPanel.AddComponent<VerticalLayoutGroup>();
            layout.spacing = 8;
            layout.childAlignment = TextAnchor.UpperLeft;
            layout.childForceExpandWidth = true;
            layout.childForceExpandHeight = false;
            _shopPanel.AddComponent<LayoutElement>();

            CreateText(_shopPanel.transform, "상점", 18, TextAnchor.UpperLeft);
            _shopStatusText = CreateText(_shopPanel.transform, "", 15, TextAnchor.UpperLeft);

            CreateText(_shopPanel.transform, "판매 아이템", 14, TextAnchor.UpperLeft);
            _shopItemsColumn = CreateColumn(_shopPanel.transform, "ShopItems");

            CreateText(_shopPanel.transform, "보유 아이템", 14, TextAnchor.UpperLeft);
            _inventoryColumn = CreateColumn(_shopPanel.transform, "Inventory");

            var buttonRow = CreateRow(_shopPanel.transform, "ShopButtons");
            CreateButton(buttonRow, "새로고침", RefreshShopData);
            CreateButton(buttonRow, "닫기", CloseShop);

            _shopPanel.SetActive(false);
        }

        private void OpenShop()
        {
            _shopOpen = true;
            RefreshAll();
            RefreshShopData();
        }

        private void CloseShop()
        {
            _shopOpen = false;
            RefreshAll();
        }

        // 상점 목록/인벤토리를 서버에서 다시 받아와 화면을 갱신한다. 둘 다
        // 비동기(HTTP)라 요청을 걸어두고 콜백에서 그려 넣는 방식이다.
        private void RefreshShopData()
        {
            int gold = OneCardClient.Instance.Gold;
            _shopStatusText.text = $"보유 골드: {gold}   불러오는 중...";
            ClearChildren(_shopItemsColumn);
            ClearChildren(_inventoryColumn);

            ShopClient.Instance.FetchItems(
                items =>
                {
                    _shopStatusText.text = $"보유 골드: {OneCardClient.Instance.Gold}";
                    BuildShopItems(items);
                },
                err => _shopStatusText.text = $"상점 목록을 불러오지 못했습니다: {err}");

            ShopClient.Instance.FetchInventory(
                OneCardClient.Instance.Username,
                BuildInventory,
                err => Debug.LogWarning($"[OneCardView] 인벤토리 조회 실패: {err}"));
        }

        private void BuildShopItems(List<ShopItemData> items)
        {
            ClearChildren(_shopItemsColumn);
            foreach (ShopItemData item in items)
            {
                var row = CreateRow(_shopItemsColumn, $"ShopItem_{item.id}");
                Text label = CreateText(row, $"{item.name}  ({item.price}G)", 15, TextAnchor.MiddleLeft);
                label.GetComponent<LayoutElement>().minWidth = 240;
                int itemId = item.id; // 클로저 캡처용 로컬 변수
                CreateButton(row, "구매", () => PurchaseItem(itemId));
            }
        }

        private void BuildInventory(List<InventoryItemData> items)
        {
            ClearChildren(_inventoryColumn);
            if (items.Count == 0)
            {
                CreateText(_inventoryColumn, "(보유한 아이템이 없습니다)", 14, TextAnchor.UpperLeft);
                return;
            }

            foreach (InventoryItemData item in items)
                CreateText(_inventoryColumn, "- " + item.name, 14, TextAnchor.UpperLeft);
        }

        private void PurchaseItem(int itemId)
        {
            _shopStatusText.text = "구매 처리 중...";
            ShopClient.Instance.Purchase(
                OneCardClient.Instance.Username,
                itemId,
                result =>
                {
                    _shopStatusText.text = $"{result.message}   (보유 골드: {result.gold})";
                    if (result.success)
                    {
                        // 골드는 로그인(TCP) 때만 서버가 보내주므로, 구매(HTTP) 이후에는
                        // 이 응답값으로 클라이언트 쪽 표시를 직접 맞춰줘야 한다.
                        OneCardClient.Instance.SetGold(result.gold);
                        ShopClient.Instance.FetchInventory(OneCardClient.Instance.Username, BuildInventory);
                    }
                },
                err => _shopStatusText.text = $"구매 요청 실패: {err}");
        }

        private InputField CreateInputField(Transform parent, string placeholder, bool isPassword)
        {
            var go = new GameObject(placeholder + "Input", typeof(RectTransform));
            go.transform.SetParent(parent, false);
            var image = go.AddComponent<Image>();
            image.color = Color.white;

            var inputField = go.AddComponent<InputField>();
            inputField.targetGraphic = image;
            if (isPassword)
                inputField.contentType = InputField.ContentType.Password;

            var textGo = new GameObject("Text", typeof(RectTransform));
            textGo.transform.SetParent(go.transform, false);
            var text = textGo.AddComponent<Text>();
            text.font = _font;
            text.fontSize = 16;
            text.color = Color.black;
            text.supportRichText = false;
            SetStretch(textGo.GetComponent<RectTransform>(), 8, 4);
            inputField.textComponent = text;

            var placeholderGo = new GameObject("Placeholder", typeof(RectTransform));
            placeholderGo.transform.SetParent(go.transform, false);
            var placeholderText = placeholderGo.AddComponent<Text>();
            placeholderText.font = _font;
            placeholderText.fontSize = 16;
            placeholderText.color = new Color(0f, 0f, 0f, 0.5f);
            placeholderText.text = placeholder;
            placeholderText.fontStyle = FontStyle.Italic;
            SetStretch(placeholderGo.GetComponent<RectTransform>(), 8, 4);
            inputField.placeholder = placeholderText;

            var le = go.AddComponent<LayoutElement>();
            le.preferredWidth = 240;
            le.preferredHeight = 36;

            return inputField;
        }

        private static void SetStretch(RectTransform rect, float horizontalPadding, float verticalPadding)
        {
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = new Vector2(horizontalPadding, verticalPadding);
            rect.offsetMax = new Vector2(-horizontalPadding, -verticalPadding);
        }

        private static void EnsureEventSystem()
        {
            if (FindFirstObjectByType<EventSystem>() != null)
                return;

            // 이 프로젝트는 (레거시 StandaloneInputModule이 아니라) Input System
            // 패키지를 쓰도록 설정돼 있어서 InputSystemUIInputModule이 필요하다.
            var go = new GameObject("EventSystem");
            go.AddComponent<EventSystem>();
            var uiModule = go.AddComponent<InputSystemUIInputModule>();

            // AddComponent만으로 만들면 OnEnable에서 기본 액션이 자동 할당되긴 하지만,
            // 런타임에 코드로 생성하는 경우를 위해 문서에서 권장하는 대로 명시적으로도
            // 호출해준다 (직접 만들 때 클릭이 하나도 안 먹었던 적이 있어서 방어적으로 유지).
            uiModule.AssignDefaultActions();
        }

        private Transform CreateRow(Transform parent, string name)
        {
            var go = new GameObject(name, typeof(RectTransform));
            go.transform.SetParent(parent, false);
            var layout = go.AddComponent<HorizontalLayoutGroup>();
            layout.spacing = 8;
            layout.childForceExpandWidth = false;
            layout.childForceExpandHeight = false;
            layout.childAlignment = TextAnchor.MiddleLeft;
            go.AddComponent<LayoutElement>();
            return go.transform;
        }

        private Transform CreateColumn(Transform parent, string name)
        {
            var go = new GameObject(name, typeof(RectTransform));
            go.transform.SetParent(parent, false);
            var layout = go.AddComponent<VerticalLayoutGroup>();
            layout.spacing = 4;
            layout.childForceExpandWidth = true;
            layout.childForceExpandHeight = false;
            layout.childAlignment = TextAnchor.UpperLeft;
            go.AddComponent<LayoutElement>();
            return go.transform;
        }

        private Text CreateText(Transform parent, string content, int fontSize, TextAnchor anchor)
        {
            var go = new GameObject("Text", typeof(RectTransform));
            go.transform.SetParent(parent, false);
            var text = go.AddComponent<Text>();
            text.font = _font;
            text.fontSize = fontSize;
            text.alignment = anchor;
            text.color = Color.white;
            text.text = content;
            var le = go.AddComponent<LayoutElement>();
            le.preferredHeight = fontSize + 8;
            le.minWidth = 40;
            return text;
        }

        private Button CreateButton(Transform parent, string label, UnityEngine.Events.UnityAction onClick)
        {
            var go = new GameObject(label + "Button", typeof(RectTransform));
            go.transform.SetParent(parent, false);
            var image = go.AddComponent<Image>();
            image.color = new Color(0.2f, 0.3f, 0.5f);
            var button = go.AddComponent<Button>();
            button.targetGraphic = image;
            button.onClick.AddListener(onClick);

            var textGo = new GameObject("Text", typeof(RectTransform));
            textGo.transform.SetParent(go.transform, false);
            var text = textGo.AddComponent<Text>();
            text.font = _font;
            text.fontSize = 16;
            text.text = label;
            text.alignment = TextAnchor.MiddleCenter;
            text.color = Color.white;
            var textRect = textGo.GetComponent<RectTransform>();
            textRect.anchorMin = Vector2.zero;
            textRect.anchorMax = Vector2.one;
            textRect.offsetMin = Vector2.zero;
            textRect.offsetMax = Vector2.zero;

            var le = go.AddComponent<LayoutElement>();
            le.preferredWidth = 130;
            le.preferredHeight = 40;
            return button;
        }

        // 카드 이미지가 있으면 스프라이트를 쓰고, 없으면(조커 등) 어두운 박스로 대체한다.
        private void SetCardImage(Image image, Suit suit, Rank rank)
        {
            Sprite sprite = CardSpriteLibrary.GetFaceSprite(suit, rank);
            if (sprite != null)
            {
                image.sprite = sprite;
                image.color = Color.white;
            }
            else
            {
                image.sprite = null;
                image.color = new Color(0.15f, 0.15f, 0.15f, 1f);
            }
        }

        private Button CreateCardButton(Transform parent, Card card, bool interactable, UnityEngine.Events.UnityAction onClick)
        {
            var go = new GameObject("Card_" + OneCardClient.CardText(card), typeof(RectTransform));
            go.transform.SetParent(parent, false);

            var image = go.AddComponent<Image>();
            SetCardImage(image, card.Suit, card.Rank);
            image.preserveAspect = true;

            if (image.sprite == null)
            {
                // "2D Cards Game Art Pack"에는 조커 그림이 없어서 텍스트로 대신 표시한다.
                var label = CreateText(go.transform, "JOKER", 14, TextAnchor.MiddleCenter);
                var labelRect = label.GetComponent<RectTransform>();
                labelRect.anchorMin = Vector2.zero;
                labelRect.anchorMax = Vector2.one;
                labelRect.offsetMin = Vector2.zero;
                labelRect.offsetMax = Vector2.zero;
            }

            var button = go.AddComponent<Button>();
            button.targetGraphic = image;
            button.interactable = interactable;
            button.onClick.AddListener(onClick);

            var le = go.AddComponent<LayoutElement>();
            le.preferredWidth = 70;
            le.preferredHeight = 100;

            return button;
        }

        private static void ClearChildren(Transform parent)
        {
            if (parent == null)
                return;

            for (int i = parent.childCount - 1; i >= 0; i--)
                Destroy(parent.GetChild(i).gameObject);
        }
    }
}
