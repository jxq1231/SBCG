#include "main.h"
#include "BSP/bsp_console.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
UART_HandleTypeDef huart1;
static UART_HandleTypeDef other;
static uint8_t *receive;
static HAL_StatusTypeDef arm_result = HAL_OK;
static uint32_t mask, tick, arms;
static DMA_HandleTypeDef txdma;
static const uint8_t *dma_ptr;
static uint16_t dma_len;
static unsigned dma_calls;
static HAL_StatusTypeDef dma_result = HAL_OK;
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask = 1U; }
void __set_PRIMASK(uint32_t value) { mask = value; }
uint32_t HAL_GetTick(void) { return tick; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return 200000000U; }
uint32_t HAL_GetUIDw0(void) { return 1U; }
uint32_t HAL_GetUIDw1(void) { return 2U; }
uint32_t HAL_GetUIDw2(void) { return 3U; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, const uint8_t *p, uint16_t n, uint32_t t)
{ (void)h; (void)p; (void)n; (void)t; return HAL_OK; }
/** @brief 记录一次 DMA 发送请求；完成由测试显式触发。 */
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, const uint8_t *p, uint16_t n)
{
    assert(h == &huart1);
    ++dma_calls; dma_ptr = p; dma_len = n;
    if (dma_result != HAL_OK) { return dma_result; }
    h->gState = HAL_UART_STATE_BUSY_TX;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h)
{ h->RxState = HAL_UART_STATE_READY; h->ErrorCode = 0U; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *p, uint16_t n)
{
    assert(h == &huart1 && n == 1U); ++arms;
    if (arm_result != HAL_OK) { return arm_result; }
    receive = p; h->RxState = HAL_UART_STATE_BUSY_RX; h->ErrorCode = 0U; return HAL_OK;
}
static void byte(uint8_t value)
{
    *receive = value; huart1.RxState = HAL_UART_STATE_READY;
    HAL_UART_RxCpltCallback(&huart1);
}
int main(void)
{
    uint8_t value;
    BSP_ConsoleRxStats stats;
    assert(BSP_Console_RxInit());
    byte('A'); byte('B');
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_BYTE && value == 'A');
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_BYTE && value == 'B');
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_NONE);
    for (unsigned i = 0; i < 600; ++i)
    { byte((uint8_t)i); assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_BYTE && value == (uint8_t)i); }
    unsigned before = arms; HAL_UART_RxCpltCallback(&other); HAL_UART_ErrorCallback(&other);
    assert(arms == before);
    byte('A'); huart1.ErrorCode = HAL_UART_ERROR_ORE;
    byte('X'); assert(huart1.ErrorCode == HAL_UART_ERROR_ORE && arms == before + 1U);
    HAL_UART_ErrorCallback(&huart1);
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_GAP);
    byte('\n'); assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_BYTE && value == '\n');
    for (unsigned i = 0; i < 300; ++i) { byte('X'); }
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_GAP);
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_NONE);
    arm_result = HAL_BUSY; byte('X');
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_GAP);
    arm_result = HAL_OK; tick = 10U; BSP_Console_RxProcess();
    byte('Z'); assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_BYTE && value == 'Z');
    BSP_Console_GetRxStats(&stats);
    assert(stats.errors == 1U && stats.overflows == 1U && stats.rearm_errors == 1U);
    mask = 1U; (void)BSP_Console_Read(&value); assert(mask == 1U); mask = 0U;
    assert(BSP_Console_RxInit());
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_NONE);
    BSP_Console_GetRxStats(&stats); assert(stats.errors == 0U && stats.overflows == 0U);
    /* FE/NE without RXNE: HAL keeps the existing receive active. */
    before = arms; huart1.ErrorCode = 2U;
    HAL_UART_ErrorCallback(&huart1);
    assert(arms == before && huart1.RxState == HAL_UART_STATE_BUSY_RX);
    huart1.ErrorCode = 0U; /* HAL clears a nonblocking error after callback. */
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_GAP);
    byte('N'); assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_BYTE && value == 'N');
    /* Start/retry failure never spins or discards the caller's IRQ mask. */
    arm_result = HAL_ERROR;
    assert(!BSP_Console_RxInit());
    before = arms;
    for (unsigned i=0; i<20U; ++i) { BSP_Console_RxProcess(); }
    assert(arms == before);
    tick += 10U; arm_result = HAL_OK; BSP_Console_RxProcess();
    assert(arms == before + 1U);
    assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_GAP);
    byte('R'); assert(BSP_Console_Read(&value) == BSP_CONSOLE_RX_BYTE && value == 'R');

    /* TX: without a linked DMA handle, enqueue falls back to blocking send. */
    BSP_ConsoleTxStats tx;
    static uint8_t big[2047];
    memset(big, 'Q', sizeof big);
    huart1.hdmatx = NULL;
    assert(BSP_Console_Write("AB", 2U));
    BSP_Console_GetTxStats(&tx);
    assert(tx.enqueued == 1U && tx.fallbacks == 1U && tx.dropped == 0U);

    /* TX: DMA path advances only on completion, then chains the next chunk. */
    huart1.hdmatx = &txdma; dma_calls = 0U; dma_result = HAL_OK;
    assert(BSP_Console_Write("HELLO", 5U));
    assert(dma_calls == 1U && dma_len == 5U && memcmp(dma_ptr, "HELLO", 5U) == 0);
    HAL_UART_TxCpltCallback(&huart1);
    assert(dma_calls == 1U);

    /* TX: ring wrap splits into contiguous DMA chunks whose total is the line. */
    const unsigned before_big = dma_calls;
    assert(BSP_Console_Write((const char *)big, (uint16_t)sizeof big));
    assert(dma_calls == before_big + 1U);
    const uint16_t first_len = dma_len;
    HAL_UART_TxCpltCallback(&huart1);
    assert(dma_calls == before_big + 2U);
    assert((unsigned)first_len + (unsigned)dma_len == sizeof big);
    HAL_UART_TxCpltCallback(&huart1);
    assert(BSP_Console_Write("XY", 2U));
    assert(dma_calls == before_big + 3U && dma_len == 2U && dma_ptr[0] == 'X');
    HAL_UART_TxCpltCallback(&huart1);

    /* TX: full ring drops the whole line and counts it. */
    assert(BSP_Console_Write((const char *)big, (uint16_t)sizeof big));
    assert(!BSP_Console_Write("Z", 1U));
    BSP_Console_GetTxStats(&tx);
    assert(tx.dropped == 1U && tx.high_water == 2047U);
    HAL_UART_TxCpltCallback(&huart1);
    HAL_UART_TxCpltCallback(&huart1);

    /* TX: DMA submission error keeps the queued bytes for the next kick. */
    dma_result = HAL_ERROR;
    assert(BSP_Console_Write("ERR", 3U));
    BSP_Console_GetTxStats(&tx);
    assert(tx.dma_errors == 1U);
    dma_result = HAL_OK;
    assert(BSP_Console_Write("!", 1U));
    assert(dma_len == 4U);
    HAL_UART_TxCpltCallback(&huart1);

    puts("PASS console BSP: ring/wrap, foreign UART, errors, overflow, rearm, critical sections, DMA TX");
    return 0;
}
