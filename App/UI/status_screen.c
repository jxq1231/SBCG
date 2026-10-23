/**
 * @file status_screen.c
 * @brief 480x272 RGB565基础状态页，支持行带绘制且不触碰硬件。
 * @details 先格式化同一快照，再画距离/同步/网络/环境/音频；真实测距无效时使用占位。
 * @note 不刷新LTDC、不切帧缓冲；后续板级集成必须先完成CubeMX与SDRAM验收。
 */
#include "status_screen.h"
#include <string.h>
#define BACKGROUND UI_RGB565(11,18,32)
#define PANEL UI_RGB565(23,34,52)
#define TEXT UI_RGB565(232,240,248)
#define MUTED UI_RGB565(146,164,184)
#define ACCENT UI_RGB565(45,212,191)
#define WARNING UI_RGB565(251,191,36)
/** @brief 在非NULL c画矩形面板，x/y/w/h为像素；无返回值，裁剪由画布处理。 */
static void Panel(UI_Canvas *c,int32_t x,int32_t y,uint32_t w,uint32_t h)
{UI_FillRect(c,x,y,w,h,PANEL);UI_FillRect(c,x,y,3,h,ACCENT);}
/** @brief 把非NULL s在now_ms下渲染至c的first_row起row_count行；错误不写并返回false，成功true。 */
bool UI_StatusRenderRows(UI_Canvas *canvas,const UI_StatusSnapshot *s,uint32_t now_ms,uint16_t first_row,uint16_t row_count)
{
 if(canvas==NULL || s==NULL || canvas->pixels==NULL || canvas->width!=UI_SCREEN_WIDTH || canvas->height!=UI_SCREEN_HEIGHT || canvas->stride<UI_SCREEN_WIDTH || row_count==0U || first_row>=UI_SCREEN_HEIGHT || row_count>UI_SCREEN_HEIGHT-first_row)return false;
 UI_StatusText text;if(!UI_StatusFormat(s,now_ms,&text))return false;
 UI_Canvas c=*canvas;c.clip_first=first_row;c.clip_end=(uint16_t)(first_row+row_count);
 UI_FillRect(&c,0,0,480,272,BACKGROUND);
 UI_DrawText(&c,14,12,"SBCG",2,ACCENT);UI_DrawText(&c,100,15,text.role,1,TEXT);UI_DrawText(&c,302,15,"SYSTEM STATUS",1,MUTED);
 UI_FillRect(&c,12,35,456,1,UI_RGB565(44,60,80));
 Panel(&c,12,48,216,116);Panel(&c,240,48,228,116);
 UI_DrawText(&c,25,60,"DISTANCE",1,MUTED);UI_DrawText(&c,25,89,text.distance,strlen(text.distance)>10U?2U:3U,TEXT);
 UI_DrawText(&c,25,144,strcmp(text.distance,"-- MM")==0?"WAITING FOR MEASUREMENT":"VALUE IN MILLIMETRES",1,MUTED);
 UI_DrawText(&c,254,60,"CLOCK / SYNC",1,MUTED);UI_DrawText(&c,254,83,text.sync,2,TEXT);
 UI_DrawText(&c,254,114,text.offset,1,MUTED);UI_DrawText(&c,254,144,text.precision,1,WARNING);
 Panel(&c,12,176,140,72);Panel(&c,164,176,140,72);Panel(&c,316,176,152,72);
 UI_DrawText(&c,24,186,"NETWORK",1,MUTED);UI_DrawText(&c,24,205,text.network,1,TEXT);UI_DrawText(&c,24,230,"ETHERNET",1,MUTED);
 UI_DrawText(&c,176,186,"ENVIRONMENT",1,MUTED);UI_DrawText(&c,176,205,text.temperature,strlen(text.temperature)>10U?1U:2U,TEXT);UI_DrawText(&c,176,230,text.speed,1,MUTED);
 UI_DrawText(&c,328,186,"AUDIO",1,MUTED);UI_DrawText(&c,328,204,text.audio,2,strcmp(text.audio,"RUN")==0?ACCENT:MUTED);UI_DrawText(&c,328,230,text.counts,1,MUTED);
 UI_DrawText(&c,14,259,"LOCAL MODEL ONLY / EXTERNAL PRECISION UNVERIFIED",1,MUTED);
 return true;
}
