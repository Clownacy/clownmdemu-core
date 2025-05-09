#ifndef LOW_PASS_FILTER_H
#define LOW_PASS_FILTER_H

#include <stddef.h>

#include "clowncommon/clowncommon.h"

#define LOW_PASS_FILTER_FIXED_BASE (1 << 16)
#define LOW_PASS_FILTER_FIXED_MULTIPLY(MULTIPLICANT, MULTIPLIER) ((MULTIPLICANT) * (cc_s32f)(MULTIPLIER) / LOW_PASS_FILTER_FIXED_BASE)
#define LOW_PASS_FILTER_COMPUTE_FIXED(x, OUTPUT_COEFFICIENT) (cc_s32f)CC_DIVIDE_ROUND(x * LOW_PASS_FILTER_FIXED_BASE, OUTPUT_COEFFICIENT)
#define LOW_PASS_FILTER_COMPUTE_MAGIC_FIRST_ORDER(OUTPUT_COEFFICIENT, INPUT_COEFFICIENT) LOW_PASS_FILTER_COMPUTE_FIXED(1.0, OUTPUT_COEFFICIENT), LOW_PASS_FILTER_COMPUTE_FIXED(INPUT_COEFFICIENT, OUTPUT_COEFFICIENT)
#define LOW_PASS_FILTER_COMPUTE_MAGIC_SECOND_ORDER(OUTPUT_COEFFICIENT, INPUT_COEFFICIENT_A, INPUT_COEFFICIENT_B) LOW_PASS_FILTER_COMPUTE_FIXED(1.0, OUTPUT_COEFFICIENT), LOW_PASS_FILTER_COMPUTE_FIXED(INPUT_COEFFICIENT_A, OUTPUT_COEFFICIENT), LOW_PASS_FILTER_COMPUTE_FIXED(INPUT_COEFFICIENT_B, OUTPUT_COEFFICIENT)

typedef struct LowPassFilter_FirstOrder_State
{
	cc_s16l previous_sample, previous_output;
} LowPassFilter_FirstOrder_State;

typedef struct LowPassFilter_SecondOrder_State
{
	cc_s16l previous_samples[2], previous_outputs[2];
} LowPassFilter_SecondOrder_State;

void LowPassFilter_FirstOrder_Initialise(LowPassFilter_FirstOrder_State *states, cc_u8f total_channels);
void LowPassFilter_FirstOrder_Apply(LowPassFilter_FirstOrder_State *states, cc_u8f total_channels, cc_s16l *sample_buffer, size_t total_frames, cc_s32f sample_magic, cc_s32f output_magic);

void LowPassFilter_SecondOrder_Initialise(LowPassFilter_SecondOrder_State *states, cc_u8f total_channels);
void LowPassFilter_SecondOrder_Apply(LowPassFilter_SecondOrder_State *states, cc_u8f total_channels, cc_s16l *sample_buffer, size_t total_frames, cc_s32f sample_magic, cc_s32f output_magic_1, cc_s32f output_magic_2);

#endif /* LOW_PASS_FILTER_H */
