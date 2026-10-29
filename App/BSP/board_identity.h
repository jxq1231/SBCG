/**
 * @file        board_identity.h
 * @brief       声明由两个已登记 MCU UID 确定角色和唯一网络身份的纯 C 接口。
 * @details     定义 UID 映射、A/B 角色和本机/对端网络资料；IPv4 与 MAC 均为按显示顺序排列的字节数组。
 * @note        不访问硬件，不修改时钟；未知 UID 不回退到默认角色。
 */
#ifndef SBCG_BOARD_IDENTITY_H
#define SBCG_BOARD_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 未配置角色为零；A 是参考板，B 是另一块板，此枚举不表示时钟已同步。 */
typedef enum
{
    BOARD_ROLE_UNCONFIGURED = 0,
    BOARD_ROLE_A,
    BOARD_ROLE_B
} BoardRole;

/** @brief 两块板的原始三字 UID；每个三字 UID 必须非全零，且 A 与 B 不得相同。 */
typedef struct
{
    uint32_t a_uid[3];
    uint32_t b_uid[3];
} BoardIdentityMap;

/**
 * @brief 解析后的本机/对端身份；UID 为原始 32 位字，网络地址为高位字节在前的独立数组。
 * @note  role 为零时无有效网络身份；本结构不是网络报文，不依赖结构体布局传输。
 */
typedef struct
{
    BoardRole role;
    uint32_t uid_words[3];
    uint32_t peer_uid_words[3];
    uint8_t local_ip[4];
    uint8_t peer_ip[4];
    uint8_t local_mac[6];
    uint8_t netmask[4];
    uint8_t gateway[4];
} BoardIdentityProfile;

/** @brief 当前两块 STM32F746G-DISCO 的真实 UID 映射，只读且具有静态生命周期。 */
extern const BoardIdentityMap BoardIdentity_DefaultMap;

/**
 * @brief   精确匹配三个 UID 字，生成固定 A/B 网络身份。
 * @param   map 调用者拥有的只读 A/B UID 表，调用期间有效；NULL 或无效表返回 false。
 * @param   uid_words 调用者拥有的三个原始只读 UID 字，调用期间有效；NULL 或未登记 UID 返回 false。
 * @param   profile 调用者拥有的输出结构；NULL 返回 false，其他失败清零全部输出字节。
 * @return  true 表示已生成有效 A/B 身份；false 表示无有效身份。
 * @note    不保留输入指针；A/B 分别使用 192.168.10.1/.2 与 02:00:00:00:00:01/02，网关为零。
 */
bool BoardIdentity_Resolve(const BoardIdentityMap *map,
                           const uint32_t uid_words[3],
                           BoardIdentityProfile *profile);

/**
 * @brief   将角色转换为有界静态名称。
 * @param   role 输入角色；未配置及其他枚举值均按未配置处理。
 * @return  静态只读字符串 A、B 或 UNCONFIGURED；调用者不得修改或释放。
 * @note    不访问硬件，不更改身份状态。
 */
const char *BoardIdentity_RoleName(BoardRole role);

#endif
