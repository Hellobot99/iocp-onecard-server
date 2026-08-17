using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Networking;

namespace OneCardGame
{
    [Serializable]
    public class ShopItemData
    {
        public int id;
        public string name;
        public int price;
    }

    [Serializable]
    public class InventoryItemData
    {
        public int id;
        public string name;
    }

    [Serializable]
    public class PurchaseResultData
    {
        public bool success;
        public string message;
        public int gold;
    }

    // 상점/인벤토리는 TCP 게임 프로토콜(OneCardClient)과 완전히 별개로, 서버의
    // HttpServer(cpp-httplib, 8080번 포트)와 REST(HTTP)로 통신한다. 실시간 동기화가
    // 필요 없는 트랜잭션성 요청이라 TCP보다 HTTP가 자연스럽다는 서버 쪽 설계를
    // 클라이언트에서도 그대로 따른다.
    //
    // 응답이 JSON 배열(top-level array)이라 JsonUtility가 직접 못 읽는다 - 배열을
    // "{"items":...}"로 감싼 뒤 래퍼 클래스로 파싱한다.
    public class ShopClient : MonoBehaviour
    {
        public static ShopClient Instance { get; private set; }

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }
            Instance = this;
        }

        private string BaseUrl
        {
            get
            {
                OneCardClient c = OneCardClient.Instance;
                return $"http://{c.serverHost}:{c.shopPort}";
            }
        }

        public void FetchItems(Action<List<ShopItemData>> onDone, Action<string> onError = null) =>
            StartCoroutine(GetShopItems(onDone, onError));

        public void FetchInventory(string username, Action<List<InventoryItemData>> onDone, Action<string> onError = null) =>
            StartCoroutine(GetInventory(username, onDone, onError));

        public void Purchase(string username, int itemId, Action<PurchaseResultData> onDone, Action<string> onError = null) =>
            StartCoroutine(PostPurchase(username, itemId, onDone, onError));

        private IEnumerator GetShopItems(Action<List<ShopItemData>> onDone, Action<string> onError)
        {
            using UnityWebRequest req = UnityWebRequest.Get($"{BaseUrl}/shop/items");
            yield return req.SendWebRequest();

            if (req.result != UnityWebRequest.Result.Success)
            {
                onError?.Invoke(req.error);
                yield break;
            }

            var wrapper = JsonUtility.FromJson<ShopItemListWrapper>("{\"items\":" + req.downloadHandler.text + "}");
            onDone?.Invoke(wrapper?.items ?? new List<ShopItemData>());
        }

        private IEnumerator GetInventory(string username, Action<List<InventoryItemData>> onDone, Action<string> onError)
        {
            string url = $"{BaseUrl}/inventory/{Uri.EscapeDataString(username)}";
            using UnityWebRequest req = UnityWebRequest.Get(url);
            yield return req.SendWebRequest();

            if (req.result != UnityWebRequest.Result.Success)
            {
                onError?.Invoke(req.error);
                yield break;
            }

            var wrapper = JsonUtility.FromJson<InventoryItemListWrapper>("{\"items\":" + req.downloadHandler.text + "}");
            onDone?.Invoke(wrapper?.items ?? new List<InventoryItemData>());
        }

        private IEnumerator PostPurchase(string username, int itemId, Action<PurchaseResultData> onDone, Action<string> onError)
        {
            WWWForm form = new WWWForm();
            form.AddField("username", username);
            form.AddField("item_id", itemId);

            using UnityWebRequest req = UnityWebRequest.Post($"{BaseUrl}/shop/purchase", form);
            yield return req.SendWebRequest();

            // 골드 부족/중복 구매 같은 "정상적인 실패"도 서버가 400 + JSON 바디로
            // 응답한다 (HttpServer.cpp 참고). 연결 자체가 안 된 경우만 진짜 에러로
            // 취급하고, 그 외에는 바디를 파싱해서 success 필드로 판단한다.
            if (req.result == UnityWebRequest.Result.ConnectionError ||
                req.result == UnityWebRequest.Result.DataProcessingError)
            {
                onError?.Invoke(req.error);
                yield break;
            }

            var result = JsonUtility.FromJson<PurchaseResultData>(req.downloadHandler.text);
            onDone?.Invoke(result);
        }

        [Serializable]
        private class ShopItemListWrapper
        {
            public List<ShopItemData> items;
        }

        [Serializable]
        private class InventoryItemListWrapper
        {
            public List<InventoryItemData> items;
        }
    }
}
