#pragma once

#include <stdint.h>

typedef int32_t FixedPoint32;

#define FP_SIZE 32
#define FP_INTPART_SIZE 17
#define FP_FRACPART_SIZE 14
#define FP32_F (1 << FP_FRACPART_SIZE)

/* Create a FixedPoint32 Value from int value n */
inline FixedPoint32 FP32_create(int32_t n){
    return n * (1 << 14);
}

/* Round Fixed Point Value to Int */
inline int32_t FP32_to_int_rzero(FixedPoint32 x){
    return x / FP32_F;
}
inline int32_t FP32_to_int_rnear(FixedPoint32 x){
    if (x >= 0){
        return (x + (1 << 13)) / FP32_F;
    }
    else {
        return (x - (1 << 13)) / FP32_F;
    }
}
/* Operations */
inline FixedPoint32 FP32_add(FixedPoint32 x, FixedPoint32 y){
    return x + y;
}
inline FixedPoint32 FP32_sub(FixedPoint32 x, FixedPoint32 y){
    return x - y;
}
inline FixedPoint32 FP32_mul(FixedPoint32 x, FixedPoint32 y){
    int64_t x_64 = (int64_t)x;
    return (FixedPoint32)(x_64 * y / FP32_F);
}
inline FixedPoint32 FP32_div(FixedPoint32 x, FixedPoint32 y){
    int64_t x_64 = (int64_t)x;
    return (FixedPoint32)(x_64 * FP32_F / y);
}

inline FixedPoint32 FP32_add_int(FixedPoint32 x, int32_t y){
    return x + (y * FP32_F);
}
inline FixedPoint32 FP32_sub_int(FixedPoint32 x, int32_t y){
    return x - (y * FP32_F);
}
inline FixedPoint32 FP32_mul_int(FixedPoint32 x, int32_t y){
    return x * y;
}
inline FixedPoint32 FP32_div_int(FixedPoint32 x, int32_t y){
    return x / y;
}

/* Utils for operate number */
inline int clip(int value, int min_value, int max_value){
    if (value > max_value) return max_value;
    if (value < min_value) return min_value;
    return value;
}