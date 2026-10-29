/**
 * @file        board_identity.c
 * @brief       将已登记的三字 UID 解析为角色和固定且互异的 IPv4/MAC。
 * @details     校验映射非零且不重复，再比较完整 UID，填充本机与反向对端资料；失败输出全零。
 * @note        纯 C 逻辑，不配置 ETH 或修改时钟，不把身份匹配当作同步完成。
 */
#include "board_identity.h"

#include <stddef.h>
#include <string.h>

const BoardIdentityMap BoardIdentity_DefaultMap = {
    .a_uid = {0x001A002DU, 0x30365105U, 0x36363631U},
    .b_uid = {0x00380050U, 0x35385104U, 0x37313532U}
};

static const uint8_t ip_a[4] = {192U, 168U, 10U, 1U};
static const uint8_t ip_b[4] = {192U, 168U, 10U, 2U};
static const uint8_t mac_a[6] = {0x02U, 0U, 0U, 0U, 0U, 1U};
static const uint8_t mac_b[6] = {0x02U, 0U, 0U, 0U, 0U, 2U};
static const uint8_t netmask[4] = {255U, 255U, 255U, 0U};

/**
 * @brief   检查一个完整 UID 是否为未配置的全零值。
 * @param   uid_words 必须非 NULL 的三个只读 UID 字；仅在调用期间读取。
 * @return  true 表示三个字均为零，false 表示至少一个字非零。
 * @note    允许合法 UID 中的个别字为零，不对 UID 作散列或截断。
 */
static bool IsZeroUid(const uint32_t uid_words[3])
{
    return (uid_words[0] | uid_words[1] | uid_words[2]) == 0U;
}

/**
 * @brief   比较两个 UID 的全部三个原始字。
 * @param   left 必须非 NULL 的只读三字 UID，调用期间有效。
 * @param   right 必须非 NULL 的只读三字 UID，调用期间有效。
 * @return  true 表示每个字相同，false 表示至少一个字不同。
 * @note    不保存指针，不采用字节序相关的整数拼接。
 */
static bool UidsEqual(const uint32_t left[3], const uint32_t right[3])
{
    return left[0] == right[0] && left[1] == right[1] && left[2] == right[2];
}

/**
 * @brief   根据完整 UID 表生成固定身份，未匹配时清零结果。
 * @param   map 调用期间有效的只读映射；NULL、全零 UID 或重复 UID 表均拒绝。
 * @param   uid_words 调用期间有效的三个只读 UID 字；NULL 或未知 UID 均拒绝。
 * @param   profile 调用者拥有的输出；NULL 返回 false，否则始终完全覆盖且失败为全零。
 * @return  true 表示匹配 A/B 并填充全部字段，false 表示无有效配置。
 * @note    先在局部结果完成输入读取再复制输出，不保留输入存储；没有硬件副作用。
 */
bool BoardIdentity_Resolve(const BoardIdentityMap *map,
                           const uint32_t uid_words[3],
                           BoardIdentityProfile *profile)
{
    BoardIdentityProfile resolved;
    bool success = false;

    if (profile == NULL)
    {
        return false;
    }

    memset(&resolved, 0, sizeof(resolved));
    if (map != NULL && uid_words != NULL &&
        !IsZeroUid(map->a_uid) && !IsZeroUid(map->b_uid) &&
        !UidsEqual(map->a_uid, map->b_uid))
    {
        const bool is_a = UidsEqual(uid_words, map->a_uid);
        const bool is_b = UidsEqual(uid_words, map->b_uid);

        if (is_a || is_b)
        {
            resolved.role = is_a ? BOARD_ROLE_A : BOARD_ROLE_B;
            memcpy(resolved.uid_words, uid_words, sizeof(resolved.uid_words));
            memcpy(resolved.peer_uid_words, is_a ? map->b_uid : map->a_uid,
                   sizeof(resolved.peer_uid_words));
            memcpy(resolved.local_ip, is_a ? ip_a : ip_b, sizeof(resolved.local_ip));
            memcpy(resolved.peer_ip, is_a ? ip_b : ip_a, sizeof(resolved.peer_ip));
            memcpy(resolved.local_mac, is_a ? mac_a : mac_b, sizeof(resolved.local_mac));
            memcpy(resolved.netmask, netmask, sizeof(resolved.netmask));
            success = true;
        }
    }

    memcpy(profile, &resolved, sizeof(resolved));
    return success;
}

/**
 * @brief   返回 A、B 或未配置的静态名称。
 * @param   role 输入角色；非 A/B 值统一映射为 UNCONFIGURED。
 * @return  静态只读 NUL 结尾字符串，不转移所有权。
 * @note    此转换不触发 UID 读取或网络初始化。
 */
const char *BoardIdentity_RoleName(BoardRole role)
{
    switch (role)
    {
        case BOARD_ROLE_A: return "A";
        case BOARD_ROLE_B: return "B";
        default: return "UNCONFIGURED";
    }
}
