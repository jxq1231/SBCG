/* Test instrumentation counts real rectangle calls; it is not linked into firmware. */
#include "UI/rgb565_canvas.h"
#include <assert.h>
#include <stdio.h>
static unsigned rectangles;
void __cyg_profile_func_enter(void *,void *) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *,void *) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *function,void *caller)
{(void)caller;if(function==(void *)UI_FillRect)++rectangles;}
void __cyg_profile_func_exit(void *function,void *caller){(void)function;(void)caller;}
int main(void)
{
 static uint16_t pixels[480*272];UI_Canvas canvas;
 assert(UI_CanvasInit(&canvas,pixels,480*272,480,272,480));
 canvas.clip_first=100;canvas.clip_end=108;
 UI_DrawText(&canvas,0,0,"THIS TEXT IS OUTSIDE THE CURRENT EIGHT ROW BAND",4,0xffff);
 assert(rectangles==0U);
 UI_DrawText(&canvas,0,100,"VISIBLE",1,0xffff);assert(rectangles>0U);
 puts("PASS off-band text performs zero rectangle work; visible text still draws");return 0;
}
