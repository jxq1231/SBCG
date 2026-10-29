#include "Sync/sync_estimator.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#define WINDOW_NS UINT64_C(1000000000)
#define MAX_NS SYNC_ESTIMATOR_MAX_TIMESTAMP_NS

typedef struct
{
    SyncExchange exchange;
    int64_t offset_x2_ns, mean_path_delay_x2_ns;
} Example;

static const Example examples[] = {
    {{1000000000,1000150000,1000350000,1000400000},100000,200000},
    {{1000000000,1000050000,1000250000,1000400000},-100000,200000},
    {{1000000000,1000100000,1000300000,1000400000},0,200000},
    {{1000000000,999600000,999800000,1000400000},-1000000,200000},
    {{1000,1001,1002,1002},1,1},
    /* Unequal paths bias the estimate: true offset 50 us appears as 100 us. */
    {{1000000000,1000200000,1000400000,1000400000},200000,200000}
};
static unsigned int calls;

static void Expect(SyncExchange exchange, uint64_t window_ns,
                   int64_t offset_x2_ns, int64_t delay_x2_ns)
{
    SyncEstimate result = {123456789,-987654321};
    ++calls;
    assert(SyncEstimator_Calculate(&exchange,window_ns,&result) == SYNC_ESTIMATOR_OK);
    assert(result.offset_x2_ns == offset_x2_ns);
    assert(result.mean_path_delay_x2_ns == delay_x2_ns);
}

static void Reject(const SyncExchange *exchange, uint64_t window_ns,
                   SyncEstimatorStatus expected)
{
    SyncEstimate result = {123456789,-987654321};
    ++calls;
    assert(SyncEstimator_Calculate(exchange,window_ns,&result) == expected);
    assert(result.offset_x2_ns == 123456789);
    assert(result.mean_path_delay_x2_ns == -987654321);
}

static uint32_t Next(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

int main(void)
{
    for (size_t i=0; i<sizeof(examples)/sizeof(examples[0]); ++i)
    {
        Expect(examples[i].exchange,WINDOW_NS,
               examples[i].offset_x2_ns,examples[i].mean_path_delay_x2_ns);
    }
    puts("PASS: six design examples preserve offset sign and expose asymmetric-path bias");

    Expect((SyncExchange){1000,1001,1002,1002},WINDOW_NS,1,1);
    Expect((SyncExchange){1000,1000,1001,1002},WINDOW_NS,-1,1);
    puts("PASS: positive and negative half-nanosecond numerators are not rounded");

    Expect((SyncExchange){0,0,0,0},1,0,0);
    Expect((SyncExchange){10,15,20,15},10,10,0);
    puts("PASS: zero timestamps and zero path delay are valid arithmetic inputs");

    for (size_t i=0; i<sizeof(examples)/sizeof(examples[0]); ++i)
    {
        SyncExchange x=examples[i].exchange;
        x.t1_a_tx_ns+=UINT64_C(4000000000000000000);
        x.t2_b_rx_ns+=UINT64_C(4000000000000000000);
        x.t3_b_tx_ns+=UINT64_C(4000000000000000000);
        x.t4_a_rx_ns+=UINT64_C(4000000000000000000);
        Expect(x,WINDOW_NS,examples[i].offset_x2_ns,examples[i].mean_path_delay_x2_ns);
    }
    puts("PASS: a large common epoch leaves both estimates unchanged");

    Expect((SyncExchange){999999950,1000000050,1000000250,1000000350},1000,0,200);
    puts("PASS: an exchange crossing a second boundary needs no special-case arithmetic");

    Expect((SyncExchange){MAX_NS,MAX_NS,MAX_NS,MAX_NS},MAX_NS,0,0);
    Expect((SyncExchange){0,MAX_NS,MAX_NS,0},MAX_NS,INT64_C(8589934591999999998),0);
    Expect((SyncExchange){MAX_NS,0,0,MAX_NS},MAX_NS,-INT64_C(8589934591999999998),0);
    Expect((SyncExchange){MAX_NS-400000,MAX_NS-250000,MAX_NS-50000,MAX_NS},WINDOW_NS,100000,200000);
    Expect((SyncExchange){0,0,0,MAX_NS},MAX_NS,-INT64_C(4294967295999999999),
           INT64_C(4294967295999999999));
    puts("PASS: maximum-domain positive and negative differences fit signed 64-bit results");

    SyncExchange x=examples[0].exchange;
    Reject(NULL,WINDOW_NS,SYNC_ESTIMATOR_BAD_ARGUMENT);
    ++calls; assert(SyncEstimator_Calculate(&x,WINDOW_NS,NULL)==SYNC_ESTIMATOR_BAD_ARGUMENT);
    ++calls; assert(SyncEstimator_Calculate(NULL,0,NULL)==SYNC_ESTIMATOR_BAD_ARGUMENT);
    puts("PASS: null arguments take priority without accessing input or output");

    for (unsigned int i=0; i<4; ++i)
    {
        for (unsigned int j=0; j<2; ++j)
        {
            x=examples[0].exchange;
            uint64_t invalid=j==0 ? MAX_NS+1 : UINT64_MAX;
            switch (i)
            {
                case 0:x.t1_a_tx_ns=invalid;break;
                case 1:x.t2_b_rx_ns=invalid;break;
                case 2:x.t3_b_tx_ns=invalid;break;
                default:x.t4_a_rx_ns=invalid;break;
            }
            Reject(&x,WINDOW_NS,SYNC_ESTIMATOR_OUT_OF_RANGE);
        }
    }
    puts("PASS: each timestamp is range-checked before signed conversion or subtraction");

    x=examples[0].exchange;
    Reject(&x,0,SYNC_ESTIMATOR_OUT_OF_RANGE);
    Reject(&x,MAX_NS+1,SYNC_ESTIMATOR_OUT_OF_RANGE);
    Reject(&x,UINT64_MAX,SYNC_ESTIMATOR_OUT_OF_RANGE);
    puts("PASS: a zero or out-of-domain exchange window is rejected without output changes");

    x=(SyncExchange){10,20,30,9};Reject(&x,1,SYNC_ESTIMATOR_BAD_ORDER);
    x=(SyncExchange){10,20,19,30};Reject(&x,1,SYNC_ESTIMATOR_BAD_ORDER);
    x=(SyncExchange){MAX_NS+1,20,19,0};Reject(&x,1,SYNC_ESTIMATOR_OUT_OF_RANGE);
    puts("PASS: only within-domain order is required and validation precedence is stable");

    Expect((SyncExchange){0,0,WINDOW_NS,WINDOW_NS},WINDOW_NS,0,0);
    Expect((SyncExchange){0,0,WINDOW_NS-1,WINDOW_NS},WINDOW_NS,-1,1);
    x=(SyncExchange){0,0,0,WINDOW_NS+1};Reject(&x,WINDOW_NS,SYNC_ESTIMATOR_WINDOW_EXCEEDED);
    x=(SyncExchange){0,0,WINDOW_NS+1,0};Reject(&x,WINDOW_NS,SYNC_ESTIMATOR_WINDOW_EXCEEDED);
    puts("PASS: exact window boundaries are accepted and either domain exceeding by 1 ns is rejected");

    x=(SyncExchange){0,0,100,99};Reject(&x,WINDOW_NS,SYNC_ESTIMATOR_NEGATIVE_DELAY);
    puts("PASS: negative path delay is rejected rather than clamped or made absolute");

    uint32_t state=UINT32_C(20261020);
    for (unsigned int i=0; i<1000; ++i)
    {
        const uint64_t epoch_bits=(uint64_t)Next(&state)<<32U;
        const uint64_t a_tx_ns=WINDOW_NS+((epoch_bits|Next(&state))%UINT64_C(999000000001));
        const int64_t true_offset_ns=(int64_t)(Next(&state)%UINT32_C(2000001))-1000000;
        const uint64_t forward_path_ns=Next(&state)%UINT32_C(1000001);
        const uint64_t reverse_path_ns=Next(&state)%UINT32_C(1000001);
        const uint64_t residence_ns=Next(&state)%UINT32_C(1000001);
        const uint64_t b_rx_ns=(uint64_t)((int64_t)a_tx_ns+(int64_t)forward_path_ns+true_offset_ns);
        x=(SyncExchange){a_tx_ns,b_rx_ns,b_rx_ns+residence_ns,
                         a_tx_ns+forward_path_ns+residence_ns+reverse_path_ns};
        Expect(x,WINDOW_NS,2*true_offset_ns+(int64_t)forward_path_ns-(int64_t)reverse_path_ns,
               (int64_t)(forward_path_ns+reverse_path_ns));
    }
    puts("PASS: 1000 seeded physical models recover the expected signed estimate and path sum");
    printf("SUMMARY: 13 groups, %u checked estimator calls, including 1000 physical models\n",calls);
    return 0;
}
