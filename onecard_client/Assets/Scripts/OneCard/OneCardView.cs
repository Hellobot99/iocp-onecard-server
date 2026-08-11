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
        private Transform _opponentsRow;
        private Image _topCardImage;
        private Text _topCardLabel;
        private Text _suitPendingText;
        private Transform _handRow;
        private Button _joinButton;
        private Button _leaveButton;
        private Button _drawButton;

        private void Start()
        {
            _font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");

            BuildUI();

            OneCardClient client = OneCardClient.Instance;
            client.OnConnected += RefreshAll;
            client.OnDisconnected += _ => RefreshAll();
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
                _statusText.text = "서버에 연결되어 있지 않습니다.";
                SetTopCard(null);
                _suitPendingText.text = "";
                SetLobbyButtonsVisible(false);
                _drawButton.gameObject.SetActive(false);
                return;
            }

            OneCardGameState state = client.State;

            if (!state.IsGameActive)
            {
                _statusText.text = "대기 중 - 대기열에 참가하면 인원이 모이는 대로 매칭됩니다.";
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

            CreateText(root.transform, "다른 플레이어", 14, TextAnchor.UpperLeft);
            _opponentsRow = CreateRow(root.transform, "Opponents");

            var topCardRow = CreateRow(root.transform, "TopCardRow");
            var topCardGo = new GameObject("TopCardImage", typeof(RectTransform));
            topCardGo.transform.SetParent(topCardRow, false);
            _topCardImage = topCardGo.AddComponent<Image>();
            _topCardImage.preserveAspect = true;
            var topLe = topCardGo.AddComponent<LayoutElement>();
            topLe.preferredWidth = 80;
            topLe.preferredHeight = 115;
            _topCardLabel = CreateText(topCardRow, "", 16, TextAnchor.MiddleLeft);

            _suitPendingText = CreateText(root.transform, "", 16, TextAnchor.UpperLeft);
            _suitPendingText.GetComponent<LayoutElement>().preferredHeight = 48; // 두 줄까지 표시

            CreateText(root.transform, "내 손패 (본인 턴일 때만 클릭 가능)", 14, TextAnchor.UpperLeft);
            _handRow = CreateRow(root.transform, "Hand");

            var buttonRow = CreateRow(root.transform, "Buttons");
            _joinButton = CreateButton(buttonRow, "대기열 참가", () => OneCardClient.Instance.JoinQueue());
            _leaveButton = CreateButton(buttonRow, "대기열 나가기", () => OneCardClient.Instance.LeaveQueue());
            _drawButton = CreateButton(buttonRow, "드로우", () => OneCardClient.Instance.DrawCard());
        }

        private static void EnsureEventSystem()
        {
            if (FindFirstObjectByType<EventSystem>() != null)
                return;

            // 이 프로젝트는 (레거시 StandaloneInputModule이 아니라) Input System
            // 패키지를 쓰도록 설정돼 있어서 InputSystemUIInputModule이 필요하다.
            var go = new GameObject("EventSystem");
            go.AddComponent<EventSystem>();
            go.AddComponent<InputSystemUIInputModule>();
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
