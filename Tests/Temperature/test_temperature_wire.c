#include "Network/temperature_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)

int main(void)
{
    TemperatureWireMessage in={true,TEMPERATURE_SOURCE_AUTO,23500,345541U,7U};
    uint8_t data[TEMPERATURE_WIRE_BYTES];
    TemperatureWireMessage out;
    CHECK(TemperatureWire_Encode(&in,data,sizeof data));
    CHECK(TemperatureWire_Decode(data,sizeof data,&out));
    CHECK(out.valid && out.source==TEMPERATURE_SOURCE_AUTO);
    CHECK(out.temperature_milli_c==23500 && out.sound_speed_mm_s==345541U);
    CHECK(out.version==7U);

    uint8_t bad[sizeof data];
    memcpy(bad,data,sizeof data);bad[0]='X';
    CHECK(!TemperatureWire_Decode(bad,sizeof bad,&out));
    memcpy(bad,data,sizeof data);bad[4]=2U;
    CHECK(!TemperatureWire_Decode(bad,sizeof bad,&out));
    memcpy(bad,data,sizeof data);bad[5]=0U;
    CHECK(!TemperatureWire_Decode(bad,sizeof bad,&out));
    CHECK(!TemperatureWire_Decode(data,(uint16_t)(sizeof data-1U),&out));
    CHECK(!TemperatureWire_Decode(NULL,sizeof data,&out));
    CHECK(!TemperatureWire_Decode(data,sizeof data,NULL));

    TemperatureWireMessage wrong=in;wrong.sound_speed_mm_s++;
    CHECK(!TemperatureWire_Encode(&wrong,data,sizeof data));
    wrong=in;wrong.temperature_milli_c=60001;
    CHECK(!TemperatureWire_Encode(&wrong,data,sizeof data));
    wrong=in;wrong.version=0U;
    CHECK(!TemperatureWire_Encode(&wrong,data,sizeof data));
    wrong=in;wrong.valid=false;
    CHECK(!TemperatureWire_Encode(&wrong,data,sizeof data));

    in.source=TEMPERATURE_SOURCE_MANUAL;in.temperature_milli_c=0;
    in.sound_speed_mm_s=331300U;in.version=9U;
    CHECK(TemperatureWire_Encode(&in,data,sizeof data));
    CHECK(TemperatureWire_Decode(data,sizeof data,&out));
    CHECK(out.source==TEMPERATURE_SOURCE_MANUAL && out.temperature_milli_c==0);
    puts("PASS temperature_wire: valid AUTO/MANUAL, range/formula/length/magic rejection");
    return 0;
}
