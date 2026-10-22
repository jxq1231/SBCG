#include "app_main.h"
#include "BSP/bsp_console.h"
#include "Network/ethernet_monitor.h"
#include "Network/udp_heartbeat.h"
#include "Network/udp_probe.h"
#include "Network/udp_time_exchange.h"
#include "Sync/ptp_clock.h"
#include "Sync/ptp_rx.h"
#include "Sync/ptp_tx.h"
#include "Sync/sync_observation.h"
#include "Temperature/temperature_service.h"
#include "Console/temperature_console.h"
#include "Audio/codec_monitor.h"
#include "Audio/audio_capture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* Fail promptly in unattended Windows runs, without a runtime assertion dialog. */
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

static uint32_t tick_ms;
static uint32_t write_count;
static bool write_ok;
static char last_line[256];
static uint32_t monitor_calls;
static EthernetMonitor_Log monitor_log;
static uint32_t lwip_calls;
static uint32_t last_lwip_tick_ms;
static uint32_t udp_calls;
static bool udp_initialized;
static bool probe_initialized;
static uint32_t probe_calls;
static bool ptp_initialized;
static uint32_t ptp_calls;
static bool rx_initialized;
static uint32_t rx_calls;
static bool tx_initialized;
static uint32_t tx_calls;
static bool time_initialized, observation_initialized;
static uint32_t observation_calls, observation_init_calls;
static bool deinit_ok = true;
static bool time_init_ok = true;
static uint32_t deinit_calls, completed_init_calls, time_calls;
static UDPTimeExchange_Log time_log;
static uint32_t temperature_init_calls, temperature_calls;
static bool temperature_initialized, temperature_low;
static unsigned console_inits, console_calls, rx_inits;
static bool reply_pending, rx_init_ok = true;
static TemperatureConsole_Log console_log;
static unsigned codec_inits, codec_calls, codec_reads;
static bool start_low_in_process;
static unsigned audio_inits, audio_calls, audio_allowed;

void AudioCapture_Init(uint32_t now_ms, AudioCapture_Log log)
{
    assert(temperature_initialized && console_inits>0 && !temperature_low);
    assert(now_ms==tick_ms && log!=NULL);
    ++audio_inits; audio_calls=audio_allowed=0;
}
void AudioCapture_Process(uint32_t now_ms, bool processing_allowed)
{
    assert(audio_inits>0 && now_ms==tick_ms);
    assert(temperature_calls==lwip_calls && console_calls==temperature_calls);
    assert(processing_allowed==!temperature_low);
    ++audio_calls;
    if (processing_allowed) { ++audio_allowed; }
}

void CodecMonitor_Init(uint32_t now_ms, CodecMonitor_Log log)
{
    assert(temperature_initialized && console_inits>0 && now_ms==tick_ms && log!=NULL);
    ++codec_inits; codec_calls=codec_reads=0;
}
void CodecMonitor_Process(uint32_t now_ms, bool io_allowed)
{
    assert(codec_inits>0 && now_ms==tick_ms);
    assert(temperature_calls==lwip_calls && console_calls==temperature_calls);
    assert(io_allowed==!temperature_low);
    ++codec_calls;
    if(io_allowed) { ++codec_reads; }
}

bool BSP_Console_RxInit(void) { ++rx_inits; return rx_init_ok; }
void TemperatureConsole_Init(TemperatureConsole_Log log)
{ assert(temperature_initialized); console_log = log; ++console_inits; console_calls=0; }
void TemperatureConsole_Process(uint32_t now_ms)
{
    assert(temperature_initialized && now_ms == tick_ms);
    ++console_calls;
    if (reply_pending && !temperature_low)
    {
        static const char reply[] = "$CMD,NAME=PING,RESULT=OK\r\n";
        console_log(reply, (int)sizeof reply - 1); reply_pending = false;
    }
}

bool TemperatureService_IsStartLowActive(void)
{
    return temperature_low;
}

void TemperatureService_Init(uint32_t now_ms,const uint32_t uid_words[3],TemperatureLog log)
{
    assert(completed_init_calls==deinit_calls && observation_initialized);
    assert(now_ms==tick_ms && uid_words[0]==0x001A002DU && log!=NULL);
    temperature_initialized=true; ++temperature_init_calls; temperature_calls=0U;
    temperature_low=false;
}
void TemperatureService_Process(uint32_t now_ms)
{
    assert(temperature_initialized && now_ms==tick_ms);
    assert(observation_calls==lwip_calls && time_calls==lwip_calls);
    ++temperature_calls;
    if (start_low_in_process) { temperature_low=true; }
}

void SyncObservation_Init(uint32_t now_ms,SyncObservation_Log log)
{
    assert(tx_initialized && now_ms==tick_ms && log!=NULL);
    observation_initialized=true; observation_calls=0; ++observation_init_calls;
}
void SyncObservation_Process(uint32_t now_ms)
{
    assert(observation_initialized && now_ms==tick_ms && time_calls==lwip_calls);
    ++observation_calls;
}

bool UDPTimeExchange_Deinit(void)
{
    ++deinit_calls;
    if (!deinit_ok) { return false; }
    time_initialized = false;
    return true;
}

bool UDPTimeExchange_Init(uint32_t now_ms, const uint32_t uid_words[3],
                          UDPTimeExchange_Tick tick, UDPTimeExchange_Log log)
{
    assert(tx_initialized && observation_initialized && now_ms == tick_ms && log != NULL);
    assert(tick != NULL && tick() == tick_ms && deinit_calls == completed_init_calls + 1U);
    assert(uid_words[0] == 0x001A002D && uid_words[2] == 0x36363631);
    time_initialized = time_init_ok;
    time_log = log;
    time_calls = 0;
    ++completed_init_calls;
    return time_init_ok;
}

void UDPTimeExchange_Process(uint32_t now_ms)
{
    assert(now_ms == tick_ms && tx_calls == lwip_calls);
    ++time_calls;
}

void PtpTx_Init(uint32_t now_ms,const uint32_t uid_words[3],PtpTx_Log log)
{
    assert(rx_initialized && now_ms==tick_ms && log!=NULL);
    assert(uid_words[0]==0x001A002D && uid_words[2]==0x36363631);
    tx_initialized=true;tx_calls=0;
}
void PtpTx_Process(uint32_t now_ms)
{
    assert(tx_initialized && now_ms==tick_ms && rx_calls==lwip_calls);
    ++tx_calls;
}

void PtpRx_Init(uint32_t now_ms, PtpRx_Log log)
{
    assert(ptp_initialized && now_ms==tick_ms && log!=NULL);
    rx_initialized=true; rx_calls=0;
}
void PtpRx_Process(uint32_t now_ms)
{
    assert(rx_initialized && now_ms==tick_ms && ptp_calls==lwip_calls);
    ++rx_calls;
}

void PtpClock_Init(uint32_t now_ms, PtpClock_Log log)
{
    assert(deinit_calls == completed_init_calls + 1U);
    assert(now_ms == tick_ms && log != NULL);
    assert(udp_initialized && probe_initialized);
    ptp_initialized = true;
    ptp_calls = 0;
}
void PtpClock_Process(uint32_t now_ms)
{
    assert(ptp_initialized && now_ms == tick_ms);
    assert(lwip_calls > 0 && last_lwip_tick_ms == now_ms);
    assert(udp_calls == lwip_calls && probe_calls == lwip_calls);
    ++ptp_calls;
}

void UDPProbe_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPProbe_Log log)
{
    assert(now_ms == tick_ms && log != NULL);
    assert(uid_words[0] == 0x001A002DU && uid_words[2] == 0x36363631U);
    probe_initialized = true;
    probe_calls = 0;
}
void UDPProbe_Process(uint32_t now_ms)
{
    assert(probe_initialized && now_ms == tick_ms);
    assert(lwip_calls > 0 && last_lwip_tick_ms == now_ms);
    ++probe_calls;
}

void UDPHeartbeat_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPHeartbeat_Log log)
{
    assert(now_ms == tick_ms && log != NULL);
    assert(uid_words[0] == 0x001A002DU && uid_words[1] == 0x30365105U &&
           uid_words[2] == 0x36363631U);
    udp_initialized = true;
    udp_calls = 0;
}

void UDPHeartbeat_Process(uint32_t now_ms)
{
    assert(udp_initialized && now_ms == tick_ms);
    assert(lwip_calls > 0U && last_lwip_tick_ms == now_ms);
    ++udp_calls;
}

void MX_LWIP_Process(void)
{
    ++lwip_calls;
    last_lwip_tick_ms = tick_ms;
}

void EthernetMonitor_Init(uint32_t now_ms, EthernetMonitor_Log log_line)
{
    assert(now_ms == tick_ms);
    monitor_calls = 0U;
    lwip_calls = 0U;
    monitor_log = log_line;
}

void EthernetMonitor_Process(uint32_t now_ms)
{
    assert(now_ms == tick_ms && monitor_log != NULL);
    assert(lwip_calls > 0U && last_lwip_tick_ms == tick_ms);
    ++monitor_calls;
}

uint32_t BSP_Console_GetTickMs(void)
{
    return tick_ms;
}

void BSP_Console_GetInfo(BSP_ConsoleInfo *info)
{
    info->core_clock_hz = 200000000U;
    info->uid_words[0] = 0x001A002DU;
    info->uid_words[1] = 0x30365105U;
    info->uid_words[2] = 0x36363631U;
}

bool BSP_Console_Write(const char *data, uint16_t length)
{
    assert(!temperature_low);
    assert(length > 0U && length < sizeof(last_line));
    assert(data[length - 2U] == '\r' && data[length - 1U] == '\n');
    memcpy(last_line, data, length);
    last_line[length] = '\0';
    ++write_count;
    return write_ok;
}

static void Reset(uint32_t start_ms, bool initial_write_ok)
{
    tick_ms = start_ms;
    write_count = 0U;
    write_ok = initial_write_ok;
    udp_initialized = false;
    probe_initialized = false;
    ptp_initialized = false;
    rx_initialized = false;
    tx_initialized = false;
    time_initialized = observation_initialized = false;
    observation_calls = observation_init_calls = 0U;
    deinit_calls = 0U;
    completed_init_calls = 0U;
    temperature_initialized=false; temperature_calls=temperature_init_calls=0U;
    temperature_low=false;
    console_inits=console_calls=rx_inits=0U; reply_pending=false;
    codec_inits=codec_calls=codec_reads=0; start_low_in_process=false;
    audio_inits=audio_calls=audio_allowed=0;
    App_Init();
}

int main(void)
{
    Reset(0U, true);
    assert(write_count == 1U);
    assert(udp_initialized);
    assert(probe_initialized);
    assert(ptp_initialized);
    assert(rx_initialized);
    assert(tx_initialized);
    assert(time_initialized && observation_initialized && observation_init_calls==1U);
    assert(temperature_initialized && temperature_init_calls==1U);
    assert(console_inits==1U && rx_inits==1U && console_log!=NULL);
    assert(strcmp(last_line,
        "$BOOT,FW=SBCG-M3.2-INDEX,HCLK_HZ=200000000,UID_W0=001A002D,"
        "UID_W1=30365105,UID_W2=36363631\r\n") == 0);
    puts("PASS: startup identifies firmware, clock and UID");

    App_Process();
    tick_ms = 999U;
    App_Process();
    assert(write_count == 1U);
    assert(monitor_calls == 2U);
    assert(lwip_calls == 2U);
    assert(udp_calls == 2U);
    assert(probe_calls == 2U);
    assert(ptp_calls == 2U);
    assert(rx_calls == 2U);
    assert(tx_calls == 2U);
    assert(time_calls == 2U && observation_calls==2U);
    assert(temperature_calls==2U);
    tick_ms = 1000U;
    App_Process();
    assert(write_count == 2U);
    assert(strcmp(last_line,
        "$HEARTBEAT,SEQ=1,TICK_MS=1000,TX_ERRORS=0\r\n") == 0);
    App_Process();
    assert(write_count == 2U);
    puts("PASS: no heartbeat before 1000 ms, one at the deadline");

    Reset(UINT32_MAX - 499U, true);
    tick_ms = 499U;
    App_Process();
    assert(write_count == 1U);
    tick_ms = 500U;
    App_Process();
    assert(write_count == 2U);
    assert(strstr(last_line, "SEQ=1,TICK_MS=500,") != NULL);
    puts("PASS: heartbeat scheduling survives uint32 tick wrap");

    Reset(0U, true);
    tick_ms = 10000U;
    App_Process();
    assert(write_count == 2U);
    tick_ms = 10001U;
    App_Process();
    assert(write_count == 2U);
    tick_ms = 11000U;
    App_Process();
    assert(write_count == 3U);
    puts("PASS: delayed main loop does not burst missed heartbeats");

    Reset(0U, false);
    tick_ms = 1000U;
    App_Process();
    assert(write_count == 2U);
    assert(strstr(last_line, "TX_ERRORS=1\r\n") != NULL);
    App_Process();
    assert(write_count == 2U);
    write_ok = true;
    tick_ms = 2000U;
    App_Process();
    assert(write_count == 3U);
    assert(strcmp(last_line,
        "$HEARTBEAT,SEQ=2,TICK_MS=2000,TX_ERRORS=2\r\n") == 0);
    tick_ms = 3000U;
    App_Process();
    assert(strstr(last_line, "TX_ERRORS=2\r\n") != NULL);
    puts("PASS: failures are counted and recovery remains rate limited");

    Reset(0U, true);
    char longest_line[257];
    memset(longest_line, 'X', sizeof(longest_line));
    longest_line[253] = '\r'; longest_line[254] = '\n'; longest_line[255] = '\0';
    time_log(longest_line, 255);
    assert(write_count == 2U && strlen(last_line) == 255U);
    time_log(longest_line, 256);
    assert(write_count == 2U);
    tick_ms = 1000U; App_Process();
    assert(strstr(last_line, "TX_ERRORS=1\r\n") != NULL);
    puts("PASS: 255-byte statistics fit and truncated 256-byte lines count an error");

    Reset(0U, true);
    App_Process();
    assert(time_initialized && time_calls == 1U);
    App_Init();
    assert(deinit_calls == 2U && completed_init_calls == 2U && time_initialized);
    assert(temperature_init_calls==2U && temperature_calls==0U);
    assert(audio_inits==2U && audio_calls==0U);
    assert(write_count == 2U && time_calls == 0U && tx_calls == 0U && observation_calls==0U && observation_init_calls==2U);
    App_Process();
    assert(time_calls == 1U && tx_calls == 1U && lwip_calls == 1U);
    puts("PASS: whole application can reinitialize a running service and preserve scheduling");

    const uint32_t writes_before = write_count;
    const uint32_t tx_before = tx_calls;
    const uint32_t obs_before = observation_init_calls;
    const uint32_t temp_before=temperature_init_calls;
    const unsigned audio_before=audio_inits;
    deinit_ok = false;
    App_Init();
    assert(write_count == writes_before + 1U && tx_calls == tx_before && observation_init_calls==obs_before);
    assert(temperature_init_calls==temp_before);
    assert(audio_inits==audio_before);
    assert(strcmp(last_line, "$TS_INIT,RESULT=OWNERSHIP_ERROR\r\n") == 0);
    deinit_ok = true;
    puts("PASS: lost wrapper ownership stops application reinitialization");

    time_init_ok = false;
    Reset(0U, true);
    assert(strcmp(last_line, "$TS_INIT,RESULT=INIT_ERROR\r\n") == 0);
    tick_ms = 1000U; App_Process();
    assert(tx_calls == 1U && time_calls == 1U);
    assert(temperature_initialized && temperature_calls==1U);
    time_init_ok = true;
    puts("PASS: service initialization failure leaves original business scheduling active");

    Reset(0U,true);
    temperature_low=true;
    char deferred[32]="$DEFER,N=1\r\n";
    time_log(deferred,(int)strlen(deferred));
    strcpy(deferred,"$DEFER,N=2\r\n");
    time_log(deferred,(int)strlen(deferred));
    memset(deferred,'X',sizeof(deferred));
    assert(write_count==1U);
    App_Process();
    assert(write_count==1U && time_calls==1U && temperature_calls==1U);
    temperature_low=false;
    App_Process();
    assert(write_count==2U && strcmp(last_line,"$DEFER,N=1\r\n")==0);
    App_Process();
    assert(write_count==3U && strcmp(last_line,"$DEFER,N=2\r\n")==0);
    puts("PASS: UART waits for DATA release, owns queued bytes and drains FIFO one line per loop");

    Reset(0U,true); temperature_low=true;
    for(unsigned int i=0;i<18U;++i)
    {
        char line[40];
        int length=snprintf(line,sizeof(line),"$DEFER,N=%u\r\n",i);
        time_log(line,length);
    }
    assert(write_count==1U);
    temperature_low=false;
    for(unsigned int i=0;i<16U;++i)
    {
        char expected[40];
        (void)snprintf(expected,sizeof(expected),"$DEFER,N=%u\r\n",i);
        App_Process(); assert(strcmp(last_line,expected)==0);
    }
    tick_ms=1000U; App_Process();
    assert(strstr(last_line,"TX_ERRORS=2\r\n")!=NULL);
    puts("PASS: bounded queue overflow is visible and never transmits during start low");

    Reset(0U,true); temperature_low=true;
    time_log("$DEFER,N=1\r\n",12);
    temperature_low=false; write_ok=false; App_Process();
    write_ok=true; tick_ms=1000U; App_Process();
    assert(strstr(last_line,"TX_ERRORS=1\r\n")!=NULL);
    puts("PASS: a failed deferred UART write is counted without blocking future logs");

    Reset(0U,true); temperature_low=true;
    time_log("$DEFER,N=1\r\n",12);
    App_Init();
    assert(!temperature_low && write_count==2U && strstr(last_line,"$BOOT,")==last_line);
    App_Process(); assert(write_count==2U);
    puts("PASS: successful application reinitialization releases DATA and clears old queued logs");

    Reset(0U,true); temperature_low=true;
    for(unsigned int i=0;i<10U;++i)
    {
        char line[40]; int length=snprintf(line,sizeof(line),"$DEFER,N=%u\r\n",i);
        time_log(line,length);
    }
    temperature_low=false;
    for(unsigned int i=0;i<8U;++i) { App_Process(); }
    temperature_low=true;
    for(unsigned int i=10U;i<20U;++i)
    {
        char line[40]; int length=snprintf(line,sizeof(line),"$DEFER,N=%u\r\n",i);
        time_log(line,length);
    }
    temperature_low=false;
    for(unsigned int i=8U;i<20U;++i)
    {
        char expected[40]; (void)snprintf(expected,sizeof(expected),"$DEFER,N=%u\r\n",i);
        App_Process(); assert(strcmp(last_line,expected)==0);
    }
    assert(write_count==21U);
    puts("PASS: circular queue preserves FIFO order while wrapping with pending records");

    Reset(0U,true); temperature_low=true;
    time_log("$DEFER,N=1\r\n",12);
    deinit_ok=false; App_Init();
    assert(write_count==1U && temperature_low && temperature_init_calls==1U);
    temperature_low=false; App_Process();
    assert(strcmp(last_line,"$DEFER,N=1\r\n")==0);
    App_Process(); assert(strcmp(last_line,"$TS_INIT,RESULT=OWNERSHIP_ERROR\r\n")==0);
    deinit_ok=true;
    puts("PASS: ownership failure preserves pending DATA state and existing queued records");
    Reset(0U,true);
    reply_pending=true; temperature_low=true;
    App_Process();
    assert(console_calls==1U && reply_pending && write_count==1U);
    temperature_low=false; App_Process();
    assert(console_calls==2U && !reply_pending && strstr(last_line,"NAME=PING,RESULT=OK"));
    rx_init_ok=false; Reset(0U,true);
    assert(strstr(last_line,"$CONSOLE,INIT=ERROR"));
    rx_init_ok=true;
    puts("PASS: command RX initializes once and replies use the DHT-safe logger");
    Reset(0U,true);
    assert(codec_inits==1U && codec_calls==0U);
    assert(audio_inits==1U && audio_calls==0U);
    start_low_in_process=true; App_Process();
    assert(codec_calls==1U && codec_reads==0U);
    assert(audio_calls==1U && audio_allowed==0U);
    start_low_in_process=false; temperature_low=false; App_Process();
    assert(codec_calls==2U && codec_reads==1U);
    assert(audio_calls==2U && audio_allowed==1U);
    puts("PASS: audio follows temperature initialization, defers on current DATA low and preserves reinit ownership guard");
    puts("PASS: codec I/O uses current DHT state after temperature processing and resumes on release");
    return 0;
}
