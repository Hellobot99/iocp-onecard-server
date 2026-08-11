#include "Session.h"
#include "PacketHandler.h"
#include <cstring>
#include <iostream>

extern Server *g_Server;

namespace
{
    // 세션당 recv/send에 쓰는 스크래치 버퍼(Buffer::data)가 1024바이트 고정이라,
    // 지금 구조에서 다룰 수 있는 패킷(헤더+바디) 최대 크기도 여기 맞춘다.
    // 이보다 큰 페이로드가 필요해지면 Buffer를 가변 크기로 바꾸거나 분할 전송을
    // 도입해야 한다 (지금은 범위 밖).
    constexpr size_t kBufferCapacity = sizeof(Buffer::data);
}

Session::Session() {}

Session::~Session() {}

void Session::Send(PacketId id, const char *data, DWORD len)
{
    const size_t packetSize = sizeof(PacketHeader) + static_cast<size_t>(len);
    if (packetSize > kBufferCapacity)
    {
        std::cerr << "[Session] packet too large to send (" << packetSize << " bytes), dropped." << std::endl;
        return;
    }

    std::vector<char> packet(packetSize);
    PacketHeader header;
    header.id = static_cast<uint16_t>(id);
    header.size = static_cast<uint16_t>(packetSize);

    memcpy(packet.data(), &header, sizeof(PacketHeader));
    memcpy(packet.data() + sizeof(PacketHeader), data, len);

    std::lock_guard<std::mutex> lock(sendMutex_);

    sendQueue_.push(std::move(packet));

    if (!sending_)
        DoSend();
}

void Session::DoSend()
{
    sending_ = true;

    ZeroMemory(&sendOverlapped_->overlapped, sizeof(OVERLAPPED));

    // sendOffset_부터 이어서 보낸다 (partial send로 일부만 나간 경우 그 다음부터).
    const std::vector<char> &packet = sendQueue_.front();
    const char *remainingData = packet.data() + sendOffset_;
    const size_t remainingSize = packet.size() - sendOffset_;

    memcpy(sendOverlapped_->buf.buf, remainingData, remainingSize);
    sendOverlapped_->buf.len = static_cast<ULONG>(remainingSize);
    sendOverlapped_->ioType = IOType::SEND;

    WSASend(sendOverlapped_->socket, &sendOverlapped_->buf, 1, nullptr, 0, (LPWSAOVERLAPPED)sendOverlapped_, NULL);
}

void Session::OnSendComplete(DWORD bytesTransferred)
{
    std::lock_guard<std::mutex> lock(sendMutex_);

    sendOffset_ += bytesTransferred;

    // WSASend가 요청한 만큼을 한 번에 다 못 보냈을 수 있다 (partial send).
    // 이 패킷을 다 보낼 때까지는 큐의 다음 패킷으로 넘어가면 안 된다 - 그러면
    // 남은 바이트 뒤에 다음 패킷이 그대로 붙어버려서 받는 쪽 프레이밍이 깨진다.
    if (sendOffset_ < sendQueue_.front().size())
    {
        DoSend();
        return;
    }

    sendOffset_ = 0;
    sendQueue_.pop();
    if (!sendQueue_.empty())
        DoSend();
    else
        sending_ = false;
}

void Session::Recv()
{
    ZeroMemory(&recvOverlapped_->overlapped, sizeof(OVERLAPPED));
    recvOverlapped_->ioType = IOType::RECV;
    recvOverlapped_->buf.len = static_cast<ULONG>(kBufferCapacity);
    DWORD flags = 0;
    WSARecv(recvOverlapped_->socket, &recvOverlapped_->buf, 1, nullptr, &flags, (LPWSAOVERLAPPED)recvOverlapped_, NULL);
}

void Session::OnRecvComplete(DWORD bytesTransferred)
{
    // 이번 recv로 도착한 만큼만 누적 버퍼에 이어붙인다.
    recvBuffer_.insert(recvBuffer_.end(), recvOverlapped_->buf.buf, recvOverlapped_->buf.buf + bytesTransferred);

    DispatchPackets();

    // 처리 후 다음 recv를 다시 건다 (버퍼는 매번 처음부터 다시 씀 — 실제
    // 스트림 재조립은 recvBuffer_에서 이루어진다).
    Recv();
}

void Session::DispatchPackets()
{
    size_t offset = 0;

    while (recvBuffer_.size() - offset >= sizeof(PacketHeader))
    {
        PacketHeader header;
        memcpy(&header, recvBuffer_.data() + offset, sizeof(PacketHeader));

        if (header.size < sizeof(PacketHeader) || header.size > kBufferCapacity)
        {
            // 프로토콜상 있을 수 없는 크기 — 스트림이 깨졌다고 보고 더 이상
            // 이 버퍼를 패킷으로 해석하지 않는다.
            std::cerr << "[Session] invalid packet size (" << header.size << "), dropping buffered stream." << std::endl;
            recvBuffer_.clear();
            return;
        }

        if (recvBuffer_.size() - offset < header.size)
            break; // 패킷이 아직 다 도착하지 않음 (partial) - 다음 recv를 기다린다.

        const char *payload = recvBuffer_.data() + offset + sizeof(PacketHeader);
        const uint16_t bodySize = header.size - static_cast<uint16_t>(sizeof(PacketHeader));

        PacketHandler::HandlePacket(this, header.id, payload, bodySize);

        offset += header.size;
    }

    if (offset > 0)
        recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + offset);
}

void Session::Reset()
{
    std::lock_guard<std::mutex> lock(sendMutex_);

    isReleased_ = false;
    sending_ = false;
    sendOffset_ = 0;
    while (!sendQueue_.empty())
        sendQueue_.pop();
    recvBuffer_.clear();
    id_ = 0;
    nickname_ = "";

    // 세션 풀에서 재사용되기 전에 반드시 비워야 한다. 안 그러면 새로
    // 붙은 연결이 이전 연결이 하던 원카드 게임의 좌석을 그대로 물려받는다.
    oneCardRoom_.reset();
    oneCardSeat_ = -1;
}
