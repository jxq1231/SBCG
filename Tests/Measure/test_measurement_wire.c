#include "Network/measurement_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static MeasurementResultSnapshot Result(void)
{
    return (MeasurementResultSnapshot){.status=MEASUREMENT_VALID,.valid=true,.id=123U,
        .distance_mm=456,.direction=SOUND_DIRECTION_A,.quality=88U,
        .sound_speed_mm_s=343000U,.temperature_version=5U,.temperature_valid=true,
        .time_domain=1U,.calibration_version=6U,.calibration_offset_mm=-12};
}
int main(void)
{
    uint8_t data[MEASUREMENT_WIRE_BYTES];MeasurementResultSnapshot result=Result(),decoded;
    assert(MeasurementWire_Encode(&result,data,sizeof data)==MEASUREMENT_WIRE_OK);
    assert(MeasurementWire_Decode(data,sizeof data,&decoded)==MEASUREMENT_WIRE_OK);
    assert(decoded.id==123U && decoded.distance_mm==456 && decoded.direction==SOUND_DIRECTION_A &&
           decoded.quality==88U && decoded.sound_speed_mm_s==343000U &&
           decoded.temperature_version==5U && decoded.time_domain==1U &&
           decoded.calibration_version==6U && decoded.calibration_offset_mm==-12);
    assert(MeasurementWire_Decode(data,27U,&decoded)==MEASUREMENT_WIRE_INVALID_LENGTH);
    data[0]='X';assert(MeasurementWire_Decode(data,sizeof data,&decoded)==MEASUREMENT_WIRE_INVALID_MAGIC);
    result=Result();result.valid=false;
    assert(MeasurementWire_Encode(&result,data,sizeof data)==MEASUREMENT_WIRE_INVALID_FIELD);
    result=Result();result.time_domain=0U;
    assert(MeasurementWire_Encode(&result,data,sizeof data)==MEASUREMENT_WIRE_INVALID_FIELD);
    result=Result();result.direction=SOUND_DIRECTION_UNKNOWN;
    assert(MeasurementWire_Encode(&result,data,sizeof data)==MEASUREMENT_WIRE_INVALID_FIELD);
    result=Result();result.calibration_offset_mm=1001;
    assert(MeasurementWire_Encode(&result,data,sizeof data)==MEASUREMENT_WIRE_INVALID_FIELD);
    result=Result();result.calibration_offset_mm=-1001;
    assert(MeasurementWire_Encode(&result,data,sizeof data)==MEASUREMENT_WIRE_INVALID_FIELD);
    result=Result();assert(MeasurementWire_Encode(&result,data,sizeof data)==MEASUREMENT_WIRE_OK);
    data[6]=0U;assert(MeasurementWire_Decode(data,sizeof data,&decoded)==MEASUREMENT_WIRE_INVALID_FIELD);
    puts("PASS measurement wire: A/B ID/distance/direction/quality/speed/temp/domain roundtrip and gates");
    return 0;
}
