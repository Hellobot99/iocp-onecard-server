#include <cstdint>

struct PacketHeader
{
    uint16_t id;
    uint16_t size;
};

enum class PacketId : uint16_t
{
    S_Enter = 1,
    C_Enter = 2,
    S_Leave = 3,
    C_Leave = 4,
    S_Chat = 5,
    C_Chat = 6,
};