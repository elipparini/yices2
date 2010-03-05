#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <inttypes.h>

#include "cputime.h"
#include "bit_tricks.h"


/*
 * Get number of trailing 0-bits in x for non-zero x
 */ 
static inline uint32_t naive_ctz(uint32_t x) {
  uint32_t m, i;

  assert(x != 0);
  m = 0x1;
  i = 0;
  while ((x & m) == 0) {
    i ++;
    m <<= 1;
  }
  return i;
}

/*
 * Number of 1 bits in x
 */
static inline uint32_t naive_popcount32(uint32_t x) {
  uint32_t c;

  c = 0;
  while (x != 0) {
    x &= (x - 1); // clear least significant bit
    c ++;
  }

  return c;
}

/*
 * Number of 1 bits in 64 bit integer x
 */
static inline uint32_t naive_popcount64(uint64_t x) {
  uint32_t c;

  c = 0;
  while (x != 0) {
    x &= (x - 1);  // clear least significant bit
    c ++;
  }

  return c;
}


#define N 500000000
#define X (1<<31)

int main() {
  uint32_t i, n;
  uint64_t x;
  double c, d;

  printf("=== Base test ===\n");
  for (i=0; i<32; i++) {
    n = 1<<i;
    printf("naive_ctz(%"PRIu32") = %"PRIu32"\n", n, naive_ctz(n));
  }
  printf("\n");

  for (i=0; i<32; i++) {
    n = 1<<i;
    printf("__builtin_ctz(%"PRIu32") = %d\n", n, __builtin_ctz(n));
  }
  printf("\n");

  n = 5;
  for (i=0; i<60; i++) {
    printf("naive_popcount(%"PRIu32") = %"PRIu32"\n", n, naive_popcount32(n));
    printf("builtin_popcount(%"PRIu32") = %"PRIu32"\n", n, popcount32(n));
    n *= 3;
  }
  printf("\n");
  
  x = 5;
  for (i=0; i<100; i++) {
    printf("naive_popcount(%"PRIu64") = %"PRIu32"\n", x, naive_popcount64(x));
    printf("builtin_popcount(%"PRIu64") = %"PRIu32"\n", x, popcount64(x));
    x *= 7;
  }
  printf("\n");

  for (i=0; i<32; i++) {
    n = 1<<i;
    printf("naive_popcount(%"PRIu32") = %"PRIu32"\n", n, naive_popcount32(n));
    printf("builtin_popcount(%"PRIu32") = %"PRIu32"\n", n, popcount32(n));
    n --;
    printf("naive_popcount(%"PRIu32") = %"PRIu32"\n", n, naive_popcount32(n));
    printf("builtin_popcount(%"PRIu32") = %"PRIu32"\n", n, popcount32(n));
  }
  printf("\n");

  for (i=0; i<64; i++) {
    x = ((uint64_t) 1)<<i;
    printf("naive_popcount(%"PRIu64") = %"PRIu32"\n", x, naive_popcount64(x));
    printf("builtin_popcount(%"PRIu64") = %"PRIu32"\n", x, popcount64(x));
    x --;
    printf("naive_popcount(%"PRIu64") = %"PRIu32"\n", x, naive_popcount64(x));
    printf("builtin_popcount(%"PRIu64") = %"PRIu32"\n", x, popcount64(x));
  }
  printf("\n");
  fflush(stdout);
  
  i = 0;
  c = get_cpu_time();
  for (n=0; n<N; n++) {
    i = naive_ctz(n|X);
    i += naive_ctz((n<<8) | X);
    i += naive_ctz((n<<16)| X);
    i += naive_ctz((n<<24)| X);
  }
  d = get_cpu_time();
  printf("Naive ctz:    %.2f s (i = %"PRIu32")\n", (d - c), i);


  i = 0;
  c = get_cpu_time();
  for (n=0; n<N; n++) {
    i = __builtin_ctz(n|X);
    i += __builtin_ctz((n<<8)|X);
    i += __builtin_ctz((n<<16)|X);
    i += __builtin_ctz((n<<24)|X);
  }
  d = get_cpu_time();
  printf("Built-in ctz: %.2f s (i = %"PRIu32")\n\n", (d - c), i);


  return 0;
}
