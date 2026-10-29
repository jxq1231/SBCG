/**
 * @file        test_bsp_board_identity.c
 * @brief       验证身份 BSP 的准备、缓存失效和 MAC 复制生命周期。
 * @details     使用真实 BSP 与解析器，只替换控制台 UID 数据来源。
 * @note        主机测试，不调用 HAL 或写入硬件。
 */
#include "BSP/bsp_board_identity.h"
#include "console_info_stub.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief   验证未准备时缓存和 MAC 输出的安全行为。
 * @return  无返回值；缓存有效或输出未清零时断言终止。
 * @note    可在初始状态或任意准备失败后调用。
 */
static void ExpectUnprepared(void)
{
    uint8_t mac[6];
    const uint8_t zero[6] = {0U};
    memset(mac, 0xA5, sizeof(mac));
    assert(BSP_BoardIdentity_Get() == NULL);
    assert(!BSP_BoardIdentity_CopyMac(mac));
    assert(memcmp(mac, zero, sizeof(mac)) == 0);
    assert(!BSP_BoardIdentity_CopyMac(NULL));
}

/**
 * @brief   验证成功准备后各输出与纯解析器的网络身份相同。
 * @param   uid_words 必须非 NULL 的三个只读真实 UID 字；不保存指针。
 * @param   role 期望 A 或 B。
 * @return  无返回值；不一致时断言终止。
 * @note    设置 stub 输入后调用真实 BSP，反复准备也必须更新成功。
 */
static void ExpectPrepared(const uint32_t uid_words[3], BoardRole role)
{
    uint8_t ip[4], mask[4], gw[4], mac[6];
    BoardIdentityProfile expected;
    ConsoleInfoStub_SetUid(uid_words);
    assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap, uid_words, &expected));
    assert(BSP_BoardIdentity_ConfigureIp(ip, mask, gw));
    const BoardIdentityProfile *profile = BSP_BoardIdentity_Get();
    assert(profile != NULL && profile->role == role);
    assert(memcmp(profile, &expected, sizeof(expected)) == 0);
    assert(memcmp(ip, expected.local_ip, sizeof(ip)) == 0);
    assert(memcmp(mask, expected.netmask, sizeof(mask)) == 0);
    assert(memcmp(gw, expected.gateway, sizeof(gw)) == 0);
    assert(BSP_BoardIdentity_CopyMac(mac));
    assert(memcmp(mac, expected.local_mac, sizeof(mac)) == 0);
    assert(!BSP_BoardIdentity_CopyMac(NULL));
    assert(BSP_BoardIdentity_Get() == profile);
}

/**
 * @brief   执行初始状态、重复准备、未知 UID 和 NULL 输出的生命周期测试。
 * @return  0 表示所有断言通过。
 * @note    runner 链接测试 stub，未使用真实控制台驱动。
 */
int main(void)
{
    const uint32_t uid_a[3] = {0x001A002DU, 0x30365105U, 0x36363631U};
    const uint32_t uid_b[3] = {0x00380050U, 0x35385104U, 0x37313532U};
    const uint32_t unknown[3] = {1U, 2U, 3U};
    uint8_t addresses[3][4];
    const uint8_t zero[sizeof(addresses)] = {0U};
    ExpectUnprepared();
    ExpectPrepared(uid_a, BOARD_ROLE_A);
    ExpectPrepared(uid_a, BOARD_ROLE_A);
    ExpectPrepared(uid_b, BOARD_ROLE_B);
    ExpectPrepared(uid_b, BOARD_ROLE_B);
    puts("PASS identity BSP: initial safe state, real A/B, repeated prepare and MAC copy");

    ConsoleInfoStub_SetUid(unknown);
    memset(addresses, 0xA5, sizeof(addresses));
    assert(!BSP_BoardIdentity_ConfigureIp(addresses[0], addresses[1], addresses[2]));
    assert(memcmp(addresses, zero, sizeof(addresses)) == 0);
    ExpectUnprepared();
    puts("PASS identity BSP: unknown UID clears prior prepared state and all address outputs");

    for (unsigned int missing = 0; missing < 3U; ++missing)
    {
        ExpectPrepared(uid_a, BOARD_ROLE_A);
        memset(addresses, 0xA5, sizeof(addresses));
        assert(!BSP_BoardIdentity_ConfigureIp(missing == 0U ? NULL : addresses[0],
                                             missing == 1U ? NULL : addresses[1],
                                             missing == 2U ? NULL : addresses[2]));
        for (unsigned int i = 0; i < 3U; ++i)
        {
            if (i != missing)
            {
                assert(memcmp(addresses[i], zero, sizeof(addresses[i])) == 0);
            }
        }
        ExpectUnprepared();
    }
    assert(!BSP_BoardIdentity_ConfigureIp(NULL, NULL, NULL));
    ExpectUnprepared();
    ExpectPrepared(uid_b, BOARD_ROLE_B);
    puts("PASS identity BSP: any null output invalidates cache, clears other outputs, then permits recovery");
    return 0;
}
