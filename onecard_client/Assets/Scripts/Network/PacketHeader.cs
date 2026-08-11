using System.Collections.Generic;

namespace OneCardGame
{
    // 서버(C++ Session)와 동일한 패킷 헤더 포맷.
    //   struct PacketHeader { uint16_t id; uint16_t size; }; (4바이트, 리틀엔디언)
    // size는 헤더를 포함한 패킷 전체 길이다.
    public static class PacketHeader
    {
        public const int Size = 4;

        public static byte[] Encode(ushort id, int bodyLength)
        {
            ushort totalSize = (ushort)(Size + bodyLength);
            byte[] header = new byte[Size];
            header[0] = (byte)(id & 0xFF);
            header[1] = (byte)((id >> 8) & 0xFF);
            header[2] = (byte)(totalSize & 0xFF);
            header[3] = (byte)((totalSize >> 8) & 0xFF);
            return header;
        }

        public static void Decode(IList<byte> buffer, int offset, out ushort id, out ushort totalSize)
        {
            id = (ushort)(buffer[offset] | (buffer[offset + 1] << 8));
            totalSize = (ushort)(buffer[offset + 2] | (buffer[offset + 3] << 8));
        }
    }
}
