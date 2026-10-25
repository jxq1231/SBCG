/** @file test_app.c @brief 复用真实App调度桩验证单次启动复位诊断和失败语义。 */
#define SBCG_TEST_SKIP_TOUCH_MAIN
#include "../TouchGFXService/test_app.c"
#include "BSP/bsp_reset.h"
static unsigned reset_reads;
static bool have_reset=true;
/** @brief 提供多标志启动副本。@param out 非空输出。@return 测试设置的捕获资格。 */
bool BSP_Reset_GetSnapshot(BSP_ResetSnapshot *out)
{
 ++reset_reads;
 if(!have_reset)return false;
 *out=(BSP_ResetSnapshot){.raw_csr=UINT32_C(0xfe000003),.reset_flags=UINT32_C(0xfe000000),.clear_ok=true};
 return true;
}
/** @brief 核对BOOT后下一队列行及仅启动读取一次。@param argc 参数数量。@param argv 选择missing场景。@return 全部通过为0。 */
int main(int argc,char **argv)
{
 if(argc>1&&!strcmp(argv[1],"missing"))have_reset=false;
 memcpy(info_uid,BoardIdentity_DefaultMap.a_uid,sizeof info_uid);
 assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));
 Reset(0,true);
 assert(strstr(last_line,"$BOOT,FW=SBCG-U3.1-RESET,")&&reset_reads==1U);
 App_Process();
 if(have_reset)assert(strstr(last_line,"$RESET,CSR=FE000003,FLAGS=FE000000,PIN=1,POR=1,BOR=1,SFT=1,IWDG=1,WWDG=1,LPWR=1,CLEAR_OK=1,CAPTURED=1"));
 else assert(strstr(last_line,"$RESET,CSR=00000000,FLAGS=00000000,PIN=0,POR=0,BOR=0,SFT=0,IWDG=0,WWDG=0,LPWR=0,CLEAR_OK=0,CAPTURED=0"));
 for(unsigned i=0;i<5;++i)App_Process();
 assert(reset_reads==1U&&framework_inits==1U);
 puts("PASS reset App: boot version, bounded bit fields, absent evidence explicit, no periodic reads");
 return 0;
}
