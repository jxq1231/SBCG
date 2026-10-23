/**
 * @file rgb565_canvas.c
 * @brief 在已验证布局的RGB565内存中裁剪绘制矩形和简易ASCII字形。
 * @details 所有矩形先以64位坐标裁剪；保护行跨度填充及外侧内存，最多处理80字符。
 * @note 无动态分配/固定地址/外设调用；内部小字库仅用于首版状态文本。
 */
#include "rgb565_canvas.h"
#include <limits.h>
#include <string.h>
/** @brief 检查out/pixels与像素容量和stride，失败清out并返回false，不写像素。 */
bool UI_CanvasInit(UI_Canvas *out,uint16_t *pixels,size_t pixel_count,uint16_t width,uint16_t height,uint32_t stride)
{
 if(out==NULL)return false;
 memset(out,0,sizeof *out);
 if(pixels==NULL || width==0U || height==0U || stride<width || (size_t)(height-1U)>(SIZE_MAX-width)/stride)return false;
 const size_t needed=(size_t)(height-1U)*stride+width;
 if(pixel_count<needed)return false;
 *out=(UI_Canvas){pixels,width,height,stride,0,height};return true;
}
/** @brief 将有符号起点x/y及无符号w/h裁剪到canvas可见区和行带，再以color填充；无返回值。 */
void UI_FillRect(UI_Canvas *c,int32_t x,int32_t y,uint32_t w,uint32_t h,uint16_t color)
{
 if(c==NULL || c->pixels==NULL || c->stride<c->width)return;
 int64_t left=x,top=y,right=(int64_t)x+w,bottom=(int64_t)y+h;
 if(left<0)left=0;
 if(top<c->clip_first)top=c->clip_first;
 if(right>c->width)right=c->width;
 if(bottom>c->clip_end)bottom=c->clip_end;
 if(bottom>c->height)bottom=c->height;
 if(right<=left || bottom<=top)return;
 for(size_t row=(size_t)top;row<(size_t)bottom;++row)
  for(size_t col=(size_t)left;col<(size_t)right;++col)c->pixels[row*c->stride+col]=color;
}
/* 每行的低5位是自有5x7字形，左侧使用bit4；仅用于ASCII状态页。 */
static const uint8_t letters[26][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
static const uint8_t digits[10][7]={{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
/** @brief 根据字符ch和0..6的row取5位字形，返回行像素掩码；未知字符显示问号。 */
static uint8_t Glyph(char ch,unsigned row)
{
 if(ch>='a' && ch<='z')ch=(char)(ch-'a'+'A');
 if(ch>='A' && ch<='Z')return letters[(unsigned)(ch-'A')][row];
 if(ch>='0' && ch<='9')return digits[(unsigned)(ch-'0')][row];
 if(ch==' ')return 0;
 if(ch=='-')return row==3U?14U:0U;
 if(ch=='.')return row==6U?4U:0U;
 if(ch==':')return row==2U || row==5U?4U:0U;
 if(ch=='/')return (uint8_t)(row<5U?1U<<row:16U);
 static const uint8_t unknown[7]={14,17,1,2,4,0,4};return unknown[row];
}
/** @brief 在c中用color画text的最多80字符；x/y为起点，scale为1..4整数倍，NULL或非法scale不写，无返回值。 */
void UI_DrawText(UI_Canvas *c,int32_t x,int32_t y,const char *text,uint8_t scale,uint16_t color)
{
 if(c==NULL || text==NULL || scale<1U || scale>4U)return;
 for(unsigned i=0;i<80U && text[i]!=0;++i){
  for(unsigned row=0;row<7U;++row){const uint8_t mask=Glyph(text[i],row);
   for(unsigned col=0;col<5U;++col)if((mask&(1U<<(4U-col)))!=0U){
    const int64_t px=(int64_t)x+((int64_t)i*6+col)*scale,py=(int64_t)y+(int64_t)row*scale;
    if(px>=INT32_MIN && px<=INT32_MAX && py>=INT32_MIN && py<=INT32_MAX)UI_FillRect(c,(int32_t)px,(int32_t)py,scale,scale,color);
   }
  }
 }
}
