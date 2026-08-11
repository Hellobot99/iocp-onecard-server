using System.Collections.Generic;
using Onecard;
using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.Controls;

namespace OneCardGame
{
    // 카드 이미지/UI가 아직 없어서 키보드로 기능을 확인할 수 있게 만든 임시
    // 테스트 컨트롤러. 나중에 실제 UI가 생기면 이 클래스는 지우고 UI가 직접
    // OneCardClient의 메서드를 호출하면 된다.
    //
    // 이 프로젝트는 (레거시 UnityEngine.Input이 아니라) Input System 패키지를
    // 쓰도록 설정돼 있어서 Keyboard.current로 읽는다.
    //
    //   Q         : 매칭 대기열 참가
    //   L         : 매칭 대기열 나가기
    //   Space     : 카드 뽑기 (드로우)
    //   1~9, 0    : 손패의 n번째 카드 내기 (J/조커는 손패에서 가장 많은 무늬를
    //               자동으로 선언한다 - 실제 UI에서는 플레이어가 직접 고르게 될 부분)
    public class OneCardDebugController : MonoBehaviour
    {
        private void Update()
        {
            Keyboard keyboard = Keyboard.current;
            OneCardClient client = OneCardClient.Instance;
            if (keyboard == null || client == null || !client.IsConnected)
                return;

            if (keyboard.qKey.wasPressedThisFrame)
            {
                Debug.Log("[OneCardDebug] 대기열 참가 요청");
                client.JoinQueue();
            }

            if (keyboard.lKey.wasPressedThisFrame)
            {
                Debug.Log("[OneCardDebug] 대기열 나가기 요청");
                client.LeaveQueue();
            }

            if (keyboard.spaceKey.wasPressedThisFrame)
                client.DrawCard();

            CheckPlayKey(client, keyboard.digit1Key, 0);
            CheckPlayKey(client, keyboard.digit2Key, 1);
            CheckPlayKey(client, keyboard.digit3Key, 2);
            CheckPlayKey(client, keyboard.digit4Key, 3);
            CheckPlayKey(client, keyboard.digit5Key, 4);
            CheckPlayKey(client, keyboard.digit6Key, 5);
            CheckPlayKey(client, keyboard.digit7Key, 6);
            CheckPlayKey(client, keyboard.digit8Key, 7);
            CheckPlayKey(client, keyboard.digit9Key, 8);
            CheckPlayKey(client, keyboard.digit0Key, 9);
        }

        private void CheckPlayKey(OneCardClient client, KeyControl key, int handIndex)
        {
            if (key.wasPressedThisFrame)
                TryPlayCardAt(client, handIndex);
        }

        private void TryPlayCardAt(OneCardClient client, int index)
        {
            List<Card> hand = client.State.MyHand;

            if (index < 0 || index >= hand.Count)
            {
                Debug.Log($"[OneCardDebug] 손패에 {index + 1}번째 카드가 없습니다 (보유: {hand.Count}장).");
                return;
            }

            Card card = hand[index];
            Debug.Log($"[OneCardDebug] {index + 1}번째 카드({OneCardClient.CardText(card)}) 내기 시도");
            client.PlayCardAuto(card);
        }
    }
}
