#ifndef SBCG_AUDIO_DMA_TEST_MAIN_H
#define SBCG_AUDIO_DMA_TEST_MAIN_H
#include <stdint.h>
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef enum {HAL_SAI_STATE_RESET,HAL_SAI_STATE_READY,HAL_SAI_STATE_BUSY_RX} HAL_SAI_StateTypeDef;
typedef struct {uint32_t CR1;} SAI_Block_TypeDef;
typedef enum {HAL_DMA_STATE_READY,HAL_DMA_STATE_BUSY,HAL_DMA_STATE_TIMEOUT} HAL_DMA_StateTypeDef;
typedef struct {uint32_t CR;} DMA_Stream_TypeDef;
typedef struct {uint32_t remaining,flags,ErrorCode; DMA_Stream_TypeDef *Instance; HAL_DMA_StateTypeDef State;} DMA_HandleTypeDef;
#define DMA_SxCR_EN 1U
typedef struct {uint32_t Mckdiv;} SAI_InitTypeDef;
typedef struct {SAI_Block_TypeDef *Instance; SAI_InitTypeDef Init; DMA_HandleTypeDef *hdmarx; HAL_SAI_StateTypeDef State;uint32_t ErrorCode;} SAI_HandleTypeDef;
#define SAI_xCR1_SAIEN 1U
#define RCC_PERIPHCLK_SAI2 2U
#define RESET 0U
#define __HAL_SAI_ENABLE(h) ((h)->Instance->CR1 |= SAI_xCR1_SAIEN)
#define __HAL_SAI_DISABLE(h) ((h)->Instance->CR1 &= ~SAI_xCR1_SAIEN)
uint32_t TestDmaCounter(DMA_HandleTypeDef *handle);
#define __HAL_DMA_GET_COUNTER(h) TestDmaCounter(h)
#define __HAL_DMA_GET_HT_FLAG_INDEX(h) 1U
#define __HAL_DMA_GET_TC_FLAG_INDEX(h) 2U
#define __HAL_DMA_GET_TE_FLAG_INDEX(h) 4U
#define __HAL_DMA_GET_FE_FLAG_INDEX(h) 8U
#define __HAL_DMA_GET_DME_FLAG_INDEX(h) 16U
#define __HAL_DMA_GET_FLAG(h,f) ((h)->flags & (f))
extern uint32_t test_primask;
static inline uint32_t __get_PRIMASK(void) {return test_primask;}
static inline void __disable_irq(void) {test_primask=1;}
static inline void __set_PRIMASK(uint32_t x) {test_primask=x;}
static inline void __DMB(void) {}
uint32_t HAL_GetTick(void);
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t which);
HAL_SAI_StateTypeDef HAL_SAI_GetState(const SAI_HandleTypeDef *h);
uint32_t HAL_SAI_GetError(const SAI_HandleTypeDef *h);
uint32_t HAL_DMA_GetError(const DMA_HandleTypeDef *h);
HAL_DMA_StateTypeDef HAL_DMA_GetState(const DMA_HandleTypeDef *h);
HAL_StatusTypeDef HAL_SAI_Receive_DMA(SAI_HandleTypeDef *h,uint8_t *p,uint16_t size);
HAL_StatusTypeDef HAL_SAI_DMAStop(SAI_HandleTypeDef *h);
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *h);
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *h);
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *h);
#endif
