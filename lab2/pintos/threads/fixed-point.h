#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#define SHIFT_BITS 14
#define FIXED_POINT_F (1 << SHIFT_BITS)
typedef int fixed_point_number;


// Convert n to fixed point: n * f
static inline fixed_point_number int_to_fixed(int x){
  return x << SHIFT_BITS;
}

// Convert x to integer (rounding toward zero): x / f
static inline int fixed_to_int(fixed_point_number x){
  return x >> SHIFT_BITS;
}

// Convert x to integer (rounding to nearest): (x + f / 2) / f if x >= 0, (x - f / 2) / f if x <= 0.
static inline int fixed_to_int_round(fixed_point_number x){
  if (x >= 0)
    return (x + (1 << (SHIFT_BITS - 1))) >> SHIFT_BITS;
  else
    return (x - (1 << (SHIFT_BITS - 1))) >> SHIFT_BITS;
}
// Add x and y: x + y
static inline fixed_point_number fixed_add_fixed(fixed_point_number x, fixed_point_number y){
  return x + y;
}

// Subtract y from x: x - y
static inline fixed_point_number fixed_subtract_fixed(fixed_point_number x, fixed_point_number y){
  return x - y;
}

// Add x and n: x + n * f
static inline fixed_point_number fixed_add_int(fixed_point_number x, int n){
  return x + (n << SHIFT_BITS);
}

// Subtract n from x: x - n * f
static inline fixed_point_number fixed_subtract_int(fixed_point_number x, int n){
  return x - (n << SHIFT_BITS);
}

// Multiply x by y: ((int64_t) x) * y / f
static inline fixed_point_number fixed_multiply_fixed(fixed_point_number x, fixed_point_number y){
  return ((int64_t) x) * y / FIXED_POINT_F;
}

// Multiply x by n: x * n
static inline fixed_point_number fixed_multiply_int(fixed_point_number x, int n){
  return x * n;
}

// Divide x by y: ((int64_t) x) * f / y
static inline fixed_point_number fixed_divide_fixed(fixed_point_number x, fixed_point_number y){
  return ((int64_t) x) * FIXED_POINT_F / y;
}   
// Divide x by n: x / n
static inline fixed_point_number fixed_divide_int(fixed_point_number x, int n){
  return x / n;
}

#endif