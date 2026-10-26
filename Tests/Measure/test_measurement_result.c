#include "Measure/measurement_result.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static TemperatureSnapshot Temp(bool valid,uint32_t speed,uint32_t version)
{
    TemperatureSnapshot t={0};t.valid=valid;t.sound_speed_mm_s=speed;t.version=version;return t;
}
static SoundPairResult Pair(bool valid,int64_t delta,uint8_t quality,SoundDirection dir)
{
    SoundPairResult p={0};p.valid=valid;p.status=valid?SOUND_PAIR_OK:SOUND_PAIR_QUALITY;
    p.delta_ns=delta;p.quality=quality;p.direction=dir;p.time_domain=7U;return p;
}
int main(void)
{
    MeasurementResult_Init(0U);MeasurementResultSnapshot out;
    assert(!MeasurementResult_Get(0U,&out) && out.status==MEASUREMENT_NONE);
    TemperatureSnapshot temp=Temp(true,343000U,7U);
    SoundPairResult pair=Pair(true,1000000,80U,SOUND_DIRECTION_A);
    assert(MeasurementResult_PublishPair(&pair,&temp,100U));
    assert(MeasurementResult_Get(100U,&out) && out.valid && out.id==1U);
    assert(out.distance_mm==343 && out.direction==SOUND_DIRECTION_A &&
           out.quality==80U && out.sound_speed_mm_s==343000U && out.temperature_version==7U);
    assert(!MeasurementResult_Get(100U+MEASUREMENT_MAX_AGE_MS,&out) && !out.valid);
    /* No temperature => cannot publish formal distance. */
    assert(!MeasurementResult_PublishPair(&pair,&(TemperatureSnapshot){0},200U));
    assert(!MeasurementResult_Get(200U,&out) && out.status==MEASUREMENT_REJECTED);
    /* Valid temperature but too far/negative/type mismatch is rejected. */
    pair=Pair(true,100000000,80U,SOUND_DIRECTION_A);
    assert(!MeasurementResult_PublishPair(&pair,&temp,300U));
    pair=Pair(false,0,0U,SOUND_DIRECTION_UNKNOWN);
    assert(!MeasurementResult_PublishPair(&pair,&temp,400U));
    /* Direction B and temperature version propagate on success. */
    pair=Pair(true,-1000000,60U,SOUND_DIRECTION_B);
    temp=Temp(true,350000U,9U);
    assert(MeasurementResult_PublishPair(&pair,&temp,500U));
    assert(MeasurementResult_Get(500U,&out) && out.direction==SOUND_DIRECTION_B &&
           out.distance_mm==350 && out.sound_speed_mm_s==350000U && out.temperature_version==9U);
    MeasurementResult_Invalidate(600U);
    assert(!MeasurementResult_Get(600U,&out) && out.status==MEASUREMENT_NONE);
    /* Remote A result keeps its ID/distance and is not recomputed locally. */
    MeasurementResultSnapshot remote=(MeasurementResultSnapshot){
        .status=MEASUREMENT_VALID,.valid=true,.id=42U,.distance_mm=777,
        .direction=SOUND_DIRECTION_B,.quality=90U,.sound_speed_mm_s=346000U,
        .temperature_version=3U,.temperature_valid=true,.time_domain=1U};
    assert(MeasurementResult_PublishRemote(&remote,700U));
    assert(MeasurementResult_Get(700U,&out) && out.id==42U && out.distance_mm==777 &&
           out.direction==SOUND_DIRECTION_B && out.quality==90U);
    remote.time_domain=0U;assert(!MeasurementResult_PublishRemote(&remote,800U));
    puts("PASS measurement result: id/age/temp version/direction/speed/range gating and remote ID");
    return 0;
}
