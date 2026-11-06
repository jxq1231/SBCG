/**
 * @file        test_business_wire.c
 * @brief       在主机上验证MDB1外头的固定布局、字段校验与内层逐字节保留。
 * @details     覆盖大端偏移、角色/长度/代次组合、容量边界、截断拒绝和1048/1088字节内层。
 * @note        仅验证纯编解码，不涉及UDP、模式资格或板上运行。
 */
#include "Network/business_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t inner[BUSINESS_WIRE_MAX_INNER];
static uint8_t payload[BUSINESS_WIRE_MAX_BYTES];

/** @brief 构造一条合法CPU消息。 @param inner_length 内层长度。 @return 消息。 */
static BusinessWireMessage Message(uint16_t inner_length)
{
    const BusinessWireMessage message = {
        .role = BOARD_ROLE_B,
        .inner_length = inner_length,
        .a_boot = UINT64_C(0x0123456789ABCDEF),
        .b_boot = UINT64_C(0x1032547698BADCFE),
        .mode_seq = UINT64_C(0x0000000700000000),
        .inner = inner
    };
    return message;
}

/** @brief 验证外头偏移与大端字段。 @return 无返回值。 */
static void TestHeaderLayout(void)
{
    for (uint16_t i = 0U; i < sizeof inner; ++i)
    {
        inner[i] = (uint8_t)(i * 7U);
    }
    const BusinessWireMessage message = Message(1048U);
    uint8_t header[BUSINESS_WIRE_HEADER_BYTES];
    memset(header, 0xAA, sizeof header);
    assert(BusinessWire_EncodeHeader(&message, header));
    assert(memcmp(header, "MDB1", 4U) == 0);
    assert(header[4] == 1U && header[5] == (uint8_t)BOARD_ROLE_B);
    assert(header[6] == 0x04U && header[7] == 0x18U); /* 1048 大端。 */
    assert(header[8] == 0x01U && header[15] == 0xEFU);
    assert(header[16] == 0x10U && header[23] == 0xFEU);
    assert(header[24] == 0x00U && header[27] == 0x07U && header[31] == 0x00U);

    BusinessWireMessage decoded;
    assert(BusinessWire_DecodeHeader(header, sizeof header, &decoded));
    assert(decoded.role == BOARD_ROLE_B && decoded.inner_length == 1048U &&
           decoded.a_boot == message.a_boot && decoded.b_boot == message.b_boot &&
           decoded.mode_seq == message.mode_seq && decoded.inner == NULL);
    puts("PASS header: magic/version/role/length/boots/sequence big-endian layout");
}

/** @brief 验证外头字段非法组合被拒绝。 @return 无返回值。 */
static void TestHeaderReject(void)
{
    BusinessWireMessage message = Message(64U);
    uint8_t header[BUSINESS_WIRE_HEADER_BYTES];
    BusinessWireMessage decoded;
    assert(BusinessWire_EncodeHeader(&message, header));

    uint8_t bad[sizeof header];
    memcpy(bad, header, sizeof bad);
    bad[0] = 'X';
    assert(!BusinessWire_DecodeHeader(bad, sizeof bad, &decoded));
    memcpy(bad, header, sizeof bad);
    bad[4] = 2U;
    assert(!BusinessWire_DecodeHeader(bad, sizeof bad, &decoded));
    memcpy(bad, header, sizeof bad);
    bad[5] = 0U;
    assert(!BusinessWire_DecodeHeader(bad, sizeof bad, &decoded));
    memcpy(bad, header, sizeof bad);
    bad[5] = 3U;
    assert(!BusinessWire_DecodeHeader(bad, sizeof bad, &decoded));
    memcpy(bad, header, sizeof bad);
    bad[6] = 0U;
    bad[7] = 0U;
    assert(!BusinessWire_DecodeHeader(bad, sizeof bad, &decoded));
    assert(!BusinessWire_DecodeHeader(header, BUSINESS_WIRE_HEADER_BYTES - 1U, &decoded));

    BusinessWireMessage invalid = message;
    invalid.inner_length = 0U;
    assert(!BusinessWire_EncodeHeader(&invalid, header));
    invalid = message;
    invalid.inner_length = BUSINESS_WIRE_MAX_INNER + 1U;
    assert(!BusinessWire_EncodeHeader(&invalid, header));
    invalid = message;
    invalid.a_boot = 0U;
    assert(!BusinessWire_EncodeHeader(&invalid, header));
    invalid = message;
    invalid.b_boot = 0U;
    assert(!BusinessWire_EncodeHeader(&invalid, header));
    invalid = message;
    invalid.mode_seq = 0U;
    assert(!BusinessWire_EncodeHeader(&invalid, header));
    puts("PASS header: invalid magic/version/role/length/boot/sequence rejected");
}

/** @brief 验证完整封装与内层逐字节保留。 @return 无返回值。 */
static void TestPayload(void)
{
    for (uint16_t i = 0U; i < sizeof inner; ++i)
    {
        inner[i] = (uint8_t)(i ^ 0x5AU);
    }
    BusinessWireMessage message = Message(1048U);
    assert(BusinessWire_Encode(&message, payload, sizeof payload));
    assert(memcmp(payload, "MDB1", 4U) == 0);
    assert(memcmp(payload + BUSINESS_WIRE_HEADER_BYTES, inner, 1048U) == 0);

    BusinessWireMessage decoded;
    assert(BusinessWire_Decode(payload, BUSINESS_WIRE_HEADER_BYTES + 1048U, &decoded));
    assert(decoded.inner_length == 1048U);
    assert(decoded.inner == payload + BUSINESS_WIRE_HEADER_BYTES);
    assert(memcmp(decoded.inner, inner, 1048U) == 0);

    /* 容量不足与长度不一致必须拒绝。 */
    assert(!BusinessWire_Encode(&message, payload, BUSINESS_WIRE_HEADER_BYTES + 1047U));
    assert(!BusinessWire_Decode(payload, BUSINESS_WIRE_HEADER_BYTES + 1047U, &decoded));
    assert(!BusinessWire_Decode(payload, BUSINESS_WIRE_HEADER_BYTES, &decoded));
    assert(!BusinessWire_Decode(payload, sizeof payload + 1U, &decoded));
    puts("PASS payload: inner bytes preserved, capacity and length mismatch rejected");
}

/** @brief 验证最大内层与WVP2/88字节边界。 @return 无返回值。 */
static void TestBoundaries(void)
{
    BusinessWireMessage message = Message(BUSINESS_WIRE_MAX_INNER);
    assert(BusinessWire_Encode(&message, payload, sizeof payload));
    assert(payload[BUSINESS_WIRE_HEADER_BYTES + BUSINESS_WIRE_MAX_INNER - 1U] == inner[BUSINESS_WIRE_MAX_INNER - 1U]);

    message = Message(88U);
    assert(BusinessWire_Encode(&message, payload, sizeof payload));
    BusinessWireMessage decoded;
    assert(BusinessWire_Decode(payload, BUSINESS_WIRE_HEADER_BYTES + 88U, &decoded));
    assert(decoded.inner_length == 88U);
    assert(BusinessWire_Encode(&message, payload, sizeof payload) &&
           (BUSINESS_WIRE_HEADER_BYTES + 88U) == 88U + 32U);
    puts("PASS boundaries: max inner 1088 and WVP2 88-byte payload size");
}

/** @brief 运行全部MDB1编解码用例。 @return 全部断言通过返回0。 */
int main(void)
{
    TestHeaderLayout();
    TestHeaderReject();
    TestPayload();
    TestBoundaries();
    puts("BusinessWire: 4 scenarios passed");
    return 0;
}
