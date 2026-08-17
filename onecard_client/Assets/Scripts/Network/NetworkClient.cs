using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Threading;

namespace OneCardGame
{
    // 서버와 TCP로 연결하고 [헤더][페이로드] 포맷으로 패킷을 주고받는 범용 클라이언트.
    //
    // 수신은 별도 스레드에서 블로킹 Read로 돌아간다. 유니티 API(Debug.Log 제외)는
    // 메인 스레드에서만 안전하게 호출할 수 있어서, 파싱이 끝난 패킷은 큐에 담아두고
    // OneCardClient.Update()에서 꺼내 처리한다.
    //
    // 서버(Session::OnRecvComplete/DispatchPackets)와 마찬가지로, TCP는 스트림이라
    // 한 번의 Read가 패킷 하나와 1:1 대응하지 않는다 (쪼개지거나 여러 개가 붙어서
    // 올 수 있음). 그래서 recv 버퍼에 누적한 뒤 완전한 패킷만 꺼내 쓴다.
    public class NetworkClient : IDisposable
    {
        public bool IsConnected { get; private set; }
        public readonly ConcurrentQueue<(ushort id, byte[] payload)> IncomingPackets = new ConcurrentQueue<(ushort, byte[])>();
        public event Action<Exception> OnDisconnected;

        private TcpClient _tcpClient;
        private NetworkStream _stream;
        private Thread _receiveThread;
        private volatile bool _running;

        private readonly object _sendLock = new object();
        private readonly List<byte> _recvBuffer = new List<byte>();
        private readonly byte[] _readChunk = new byte[4096];

        public void Connect(string host, int port)
        {
            _tcpClient = new TcpClient();
            _tcpClient.Connect(host, port);

            // Nagle을 끈다. 기본값(켜짐)이면 카드 한 장 정보처럼 작은 패킷을 바로
            // 안 보내고 모아뒀다가 보내서, 서버 부하테스트 봇으로 잰 RTT의 p99가
            // p50보다 몇 배씩 튀는 원인이 됐다 (서버 쪽도 TCP_NODELAY로 맞춰서 끔).
            _tcpClient.NoDelay = true;

            _stream = _tcpClient.GetStream();

            IsConnected = true;
            _running = true;

            _receiveThread = new Thread(ReceiveLoop) { IsBackground = true };
            _receiveThread.Start();
        }

        public void Send(ushort packetId, byte[] body)
        {
            if (!IsConnected)
                return;

            byte[] header = PacketHeader.Encode(packetId, body.Length);

            lock (_sendLock)
            {
                _stream.Write(header, 0, header.Length);
                if (body.Length > 0)
                    _stream.Write(body, 0, body.Length);
            }
        }

        private void ReceiveLoop()
        {
            Exception failure = null;

            try
            {
                while (_running)
                {
                    int n = _stream.Read(_readChunk, 0, _readChunk.Length);
                    if (n <= 0)
                        break; // 서버가 연결을 닫음

                    _recvBuffer.AddRange(new ArraySegment<byte>(_readChunk, 0, n));
                    DispatchCompletePackets();
                }
            }
            catch (Exception e)
            {
                if (_running)
                    failure = e;
            }

            IsConnected = false;
            _running = false;
            OnDisconnected?.Invoke(failure);
        }

        private void DispatchCompletePackets()
        {
            int offset = 0;

            while (_recvBuffer.Count - offset >= PacketHeader.Size)
            {
                PacketHeader.Decode(_recvBuffer, offset, out ushort id, out ushort totalSize);

                if (totalSize < PacketHeader.Size)
                {
                    // 프로토콜이 깨진 상태 - 더 이상 신뢰할 수 없으니 버퍼를 비운다.
                    _recvBuffer.Clear();
                    return;
                }

                if (_recvBuffer.Count - offset < totalSize)
                    break; // 패킷이 아직 다 도착하지 않음 (partial) - 다음 Read를 기다린다.

                int bodyLength = totalSize - PacketHeader.Size;
                byte[] payload = new byte[bodyLength];
                _recvBuffer.CopyTo(offset + PacketHeader.Size, payload, 0, bodyLength);

                IncomingPackets.Enqueue((id, payload));

                offset += totalSize;
            }

            if (offset > 0)
                _recvBuffer.RemoveRange(0, offset);
        }

        public void Dispose()
        {
            _running = false;
            IsConnected = false;

            try { _stream?.Close(); } catch { /* 이미 끊겨있으면 무시 */ }
            try { _tcpClient?.Close(); } catch { /* 이미 끊겨있으면 무시 */ }
        }
    }
}
