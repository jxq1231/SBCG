/**
 * @file        test_board_identity.c
 * @brief       验证纯 C 身份解析的真实 UID、唯一地址和拒绝路径。
 * @details     直接调用生产解析器，覆盖三字精确匹配、无效映射和失败清零。
 * @note        主机测试，不读取 MCU 或修改网络硬件。
 */
#include "BSP/board_identity.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint32_t uid_a[3] = {0x001A002DU, 0x30365105U, 0x36363631U};
static const uint32_t uid_b[3] = {0x00380050U, 0x35385104U, 0x37313532U};
static const uint8_t ip_a[4] = {192U, 168U, 10U, 1U};
static const uint8_t ip_b[4] = {192U, 168U, 10U, 2U};
static const uint8_t mac_a[6] = {2U, 0U, 0U, 0U, 0U, 1U};
static const uint8_t mac_b[6] = {2U, 0U, 0U, 0U, 0U, 2U};

/**
 * @brief   检查成功解析的 profile 是否包含精确角色和网络参数。
 * @param   profile 非 NULL 的只读被测结果，调用期间有效。
 * @param   role 期望的 A 或 B 角色。
 * @return  无返回值；不一致时断言终止。
 * @note    根据角色独立选择期望值，不调用解析器推导期望值。
 */
static void ExpectProfile(const BoardIdentityProfile *profile, BoardRole role)
{
    const bool is_a = role == BOARD_ROLE_A;
    const uint8_t mask[4] = {255U, 255U, 255U, 0U};
    const uint8_t gateway[4] = {0U};
    assert(profile->role == role);
    assert(memcmp(profile->uid_words, is_a ? uid_a : uid_b, sizeof(uid_a)) == 0);
    assert(memcmp(profile->peer_uid_words, is_a ? uid_b : uid_a, sizeof(uid_a)) == 0);
    assert(memcmp(profile->local_ip, is_a ? ip_a : ip_b, sizeof(ip_a)) == 0);
    assert(memcmp(profile->peer_ip, is_a ? ip_b : ip_a, sizeof(ip_a)) == 0);
    assert(memcmp(profile->local_mac, is_a ? mac_a : mac_b, sizeof(mac_a)) == 0);
    assert(memcmp(profile->netmask, mask, sizeof(mask)) == 0);
    assert(memcmp(profile->gateway, gateway, sizeof(gateway)) == 0);
}

/**
 * @brief   检查失败返回并确认所有输出字节清零。
 * @param   map 可为 NULL 的只读映射输入。
 * @param   uid_words 可为 NULL 的只读 UID 输入。
 * @return  无返回值；未拒绝或残留输出时断言终止。
 * @note    先填充非零哨兵，覆盖结构填充字节的清零行为。
 */
static void ExpectRejected(const BoardIdentityMap *map, const uint32_t uid_words[3])
{
    BoardIdentityProfile profile;
    const unsigned char zero[sizeof(profile)] = {0U};
    memset(&profile, 0xA5, sizeof(profile));
    assert(!BoardIdentity_Resolve(map, uid_words, &profile));
    assert(memcmp(&profile, zero, sizeof(profile)) == 0);
}

/**
 * @brief   执行真实 UID、地址唯一性、映射校验和角色名称测试。
 * @return  0 表示所有断言通过。
 * @note    由主机测试 runner 调用，不进行固件或硬件验收。
 */
int main(void)
{
    BoardIdentityProfile a, b;
    assert(BOARD_ROLE_UNCONFIGURED == 0);
    assert(memcmp(BoardIdentity_DefaultMap.a_uid, uid_a, sizeof(uid_a)) == 0);
    assert(memcmp(BoardIdentity_DefaultMap.b_uid, uid_b, sizeof(uid_b)) == 0);
    assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap, uid_a, &a));
    assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap, uid_b, &b));
    ExpectProfile(&a, BOARD_ROLE_A);
    ExpectProfile(&b, BOARD_ROLE_B);
    assert(memcmp(a.local_ip, b.local_ip, sizeof(a.local_ip)) != 0);
    assert(memcmp(a.local_mac, b.local_mac, sizeof(a.local_mac)) != 0);
    assert((a.local_mac[0] & 3U) == 2U && (b.local_mac[0] & 3U) == 2U);
    puts("PASS identity: both real UIDs map to exact distinct IP/MAC and inverse peers");

    for (unsigned int role = 0; role < 2U; ++role)
    {
        for (unsigned int word = 0; word < 3U; ++word)
        {
            uint32_t changed[3];
            memcpy(changed, role == 0U ? uid_a : uid_b, sizeof(changed));
            changed[word] ^= 1U;
            ExpectRejected(&BoardIdentity_DefaultMap, changed);
        }
    }
    const uint32_t zero_uid[3] = {0U};
    ExpectRejected(&BoardIdentity_DefaultMap, zero_uid);
    ExpectRejected(NULL, uid_a);
    ExpectRejected(&BoardIdentity_DefaultMap, NULL);
    ExpectRejected(NULL, NULL);
    assert(!BoardIdentity_Resolve(&BoardIdentity_DefaultMap, uid_a, NULL));
    assert(!BoardIdentity_Resolve(NULL, NULL, NULL));
    puts("PASS identity: every UID word must match; unknown/null inputs fail with cleared output");

    BoardIdentityMap invalid = BoardIdentity_DefaultMap;
    memset(invalid.a_uid, 0, sizeof(invalid.a_uid));
    ExpectRejected(&invalid, uid_b);
    invalid = BoardIdentity_DefaultMap;
    memset(invalid.b_uid, 0, sizeof(invalid.b_uid));
    ExpectRejected(&invalid, uid_a);
    memset(&invalid, 0, sizeof(invalid));
    ExpectRejected(&invalid, zero_uid);
    invalid = BoardIdentity_DefaultMap;
    memcpy(invalid.b_uid, invalid.a_uid, sizeof(invalid.a_uid));
    ExpectRejected(&invalid, uid_a);
    invalid = BoardIdentity_DefaultMap;
    memcpy(invalid.a_uid, invalid.b_uid, sizeof(invalid.b_uid));
    ExpectRejected(&invalid, uid_b);
    puts("PASS identity: zero A/B entries and duplicate UID maps are rejected without fallback");

    assert(strcmp(BoardIdentity_RoleName(BOARD_ROLE_A), "A") == 0);
    assert(strcmp(BoardIdentity_RoleName(BOARD_ROLE_B), "B") == 0);
    assert(strcmp(BoardIdentity_RoleName(BOARD_ROLE_UNCONFIGURED), "UNCONFIGURED") == 0);
    assert(strcmp(BoardIdentity_RoleName((BoardRole)99), "UNCONFIGURED") == 0);
    puts("PASS identity: role names are bounded to A/B/UNCONFIGURED");
    return 0;
}
