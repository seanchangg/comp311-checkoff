#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define SIZE 1000000
#define ITERS 10000

static inline void keep(void *p) { __asm__ volatile("" : : "r"(p) : "memory"); }

int main() {
    int32_t* v1 = malloc(SIZE*sizeof(int32_t));
    int32_t* v2 = malloc(SIZE * sizeof(int32_t));
    int32_t* v3 = malloc(SIZE*sizeof(int32_t));
    for (int i = 0; i<SIZE; ++i) {
            v1[i] = rand();
            v2[i] = rand();
    }
    int64_t sum = 0;
    clock_t start = clock();
    for (int j = 0;j<ITERS;++j) {
        for (int i = 0; i<SIZE; ++i) {
            v3[i] = v1[i] + v2[i];
        }
        keep(v1);
        keep(v2);
        keep(v3);
    }
    clock_t end = clock();
    free(v1);
    free(v2);
    free(v3);
    double elapsed = (double)(end-start) /CLOCKS_PER_SEC;
    printf("Average Time: %.10f microseconds\n", (elapsed*1000000) / ITERS);
    return 0;
}