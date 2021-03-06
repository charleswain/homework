
#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include <complex.h>
#include <inttypes.h>


#define COMMENT "JPEG Encoder by ND, IK & LG. Ensimag 2015"

#define USAGE "Usage : %s <input_file> -o <output_file> [options]\n"\
              "\n"\
              "Options list :\n"\
              "    -c <quality>  : Compression rate [0-25] (0 : lossless, 25 : highest)\n"\
              "    -m <mcu_size> : Output MCU sizes, either 8x8 / 16x8 / 8x16 / 16x16\n"\
              "    -g            : Encode as a gray image\n"\
              "    -d            : Decode to TIFF instead of encoding\n"\
              "    -h            : Display this help\n"\
              "\n"\
              "Supported input images : TIFF, JPEG\n"



#define BLOCK_DIM 8
#define BLOCK_SIZE 64

#define DEFAULT_COMPRESSION 3

#define DEFAULT_MCU_WIDTH BLOCK_DIM*2
#define DEFAULT_MCU_HEIGHT BLOCK_DIM*2


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

#ifndef M_SQRT1_2
#define M_SQRT1_2 0.70710678118654752440
#endif


// Log
#define LOG_LEVEL 3
#define TRACE(__level, ...)	if ( __level >= LOG_LEVEL ) { printf(__VA_ARGS__); }
#define INFO(__message) printf("[%s: %s, l.%d] %s.\n", __FILE__, __func__, __LINE__, __message);

// Other
#define UNUSED(arg) ((void)(arg))
#define SAFE_FREE(p) do { if (p != NULL) { free(p), p = NULL; } } while (0)

/* Macros */
#define _BYTE(c, i)       ((c >> 8*i) & 0xFF)
#define GET_BYTE(c)       _BYTE(c, 0)
#define RED(c)            _BYTE(c, 2)
#define GREEN(c)          _BYTE(c, 1)
#define BLUE(c)           GET_BYTE(c)


#endif

#ifdef DEBUG
#define trace(fmt, args...)do{ \
   fprintf(stderr, fmt, ## args);\
   fflush(stderr);\
}while(0);
#else 
#define  trace(fmt, args...)do{}while(0);
#endif

