#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>

#define HUFFMAN_BITS_SIZE 256
#define HUFFMAN_HASH_NBITS 9 //程序中查找码字长度
#define HUFFMAN_HASH_SIZE (1UL<<HUFFMAN_HASH_NBITS)
/* Flags that can be set by any applications */
#define TINYJPEG_FLAGS_MJPEG_TABLE	(1<<1)

#if DEBUG
#define trace(fmt, args...) do { \
   fprintf(stderr, fmt, ## args); \
   fflush(stderr); \
} while(0)
#else
#define trace(fmt, args...) do { } while (0)
#endif

#define cY	0
#define cCb	1
#define cCr	2



enum{
    COMPONENTS=3,
    HUFFMAN_TABLES=256,
    MAX_URL_LENGTH = 128,
};

enum{
    DQT = 0xDB,
    SOF = 0xC0,
    DHT = 0xC4,
    SOI = 0xD8,
    SOS = 0xDA,
    RST = 0xD0,
    RST7 = 0xD7,
    EOI = 0xD9,
    DRI = 0xDD,
    APPO = 0xE0,
};

/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */

/* IMPORTANT: these are only valid for 8-bit data precision! */
static const unsigned char bits_dc_luminance[17] =
{ 
  0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 
};
static const unsigned char val_dc_luminance[] =
{
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 
};
  
static const unsigned char bits_dc_chrominance[17] =
{
  0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 
};
static const unsigned char val_dc_chrominance[] = 
{
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 
};
  
static const unsigned char bits_ac_luminance[17] =
{
  0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d 
};
static const unsigned char val_ac_luminance[] =
{
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
  0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
  0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
  0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
  0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
  0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
  0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
  0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
  0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
  0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
  0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
  0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
  0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};

static const unsigned char bits_ac_chrominance[17] =
{ 
  0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 
};

static const unsigned char val_ac_chrominance[] =
{
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
  0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
  0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
  0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
  0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
  0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
  0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
  0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
  0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
  0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
  0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
  0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
  0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
  0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
  0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};

struct huffman_table{
    short int lookup[HUFFMAN_HASH_SIZE];
    /*得到解码时读取的符号位数 */
    unsigned char code_size[HUFFMAN_HASH_SIZE];
    /*一些地方存放的值不在查找表编码,如果256个值编码足以存储所有的值*/
    uint16_t slowtable[16-HUFFMAN_HASH_NBITS][256];
};

struct component{
    unsigned int Hfactor; /*水平采样因子*/
    unsigned int Vfactor;
    float *Q_table;
    struct huffman_table *AC_table;
    struct huffman_table *DC_table;
    short int previous_DC;
    short int DCT[64];

#if SANITY_CHECK
    unsigned int cid;
#endif
};

/*Allocate a new tinyjpeg decoder object*/

struct jdec_private{

    /* public variables*/
    uint8_t *components[COMPONENTS];
    unsigned int width, height;  /*size of  the image*/
    unsigned int flags;

    /*private variables*/
    const unsigned char *stream_begin, *stream_end;
    unsigned int stream_length;

    const unsigned char *stream;
    unsigned int reservoir, nbits_in_reservoir;

    struct component component_infos[COMPONENTS];
    float Q_tables[COMPONENTS][64];
    struct huffman_table HTDC[HUFFMAN_TABLES];
    struct huffman_table HTAC[HUFFMAN_TABLES];
    int default_huffman_table_initialized;
    int restart_interval;
    int restarts_to_go;
    int last_rst_marker_seen;

    /* Temp space used after the IDCT to store each components */
    uint8_t Y[64*4], Cr[64], Cb[64];

    jmp_buf jump_state;
    /* Internal Pointer use for colorspace conversion , do not moidfy it !!!*/
    uint8_t *plane[COMPONENTS];

    /* */
    int tempY[4];
    int tempU, tempV;
    int DQT_table_num;

};

struct jdec_private *tinyjpeg_init(void){
    struct jdec_private *priv;

    priv = (struct jdec_private *)calloc(1, sizeof(struct jdec_private));
    if(priv == NULL)
        return NULL;
    priv->DQT_table_num=0;
    return priv;
};

typedef void (*decode_MCU_fct) (struct jdec_private *priv);
typedef void (*convert_colorspace_fct) (struct jdec_private *priv);

static void resync(struct jdec_private *priv);

#define look_nbits(reservoir,nbits_in_reservoir,stream,nbits_wanted,result) do { \
   fill_nbits(reservoir,nbits_in_reservoir,stream,(nbits_wanted)); \
   result = ((reservoir)>>(nbits_in_reservoir-(nbits_wanted))); \
}  while(0);
#define fill_nbits(reservoir,nbits_in_reservoir,stream,nbits_wanted) do { \
   while (nbits_in_reservoir<nbits_wanted) \
    { \
      unsigned char c; \
      if (stream >= priv->stream_end) \
        longjmp(priv->jump_state, -EIO); \
      c = *stream++; \
      reservoir <<= 8; \
      if (c == 0xff && *stream == 0x00) \
        stream++; \
      reservoir |= c; \
      nbits_in_reservoir+=8; \
    } \
}  while(0);

#if defined(__GNUC__) && (__GNUC__ > 3) && defined(__OPTIMIZE__)
#define __likely(x)       __builtin_expect(!!(x), 1)
#define __unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define __likely(x)       (x)
#define __unlikely(x)     (x)
#endif

#define skip_nbits(reservoir,nbits_in_reservoir,stream,nbits_wanted) do { \
   nbits_in_reservoir -= (nbits_wanted); \
   reservoir &= ((1U<<nbits_in_reservoir)-1); \
}  while(0);

/* Signed version !!!! */
#define get_nbits(reservoir,nbits_in_reservoir,stream,nbits_wanted,result) do { \
   fill_nbits(reservoir,nbits_in_reservoir,stream,(nbits_wanted)); \
   result = ((reservoir)>>(nbits_in_reservoir-(nbits_wanted))); \
   nbits_in_reservoir -= (nbits_wanted);  \
   reservoir &= ((1U<<nbits_in_reservoir)-1); \
   if ((unsigned int)result < (1UL<<((nbits_wanted)-1))) \
       result += (0xFFFFFFFFUL<<(nbits_wanted))+1; \
}  while(0);
/* Signed version !!!! */
#define get_nbits(reservoir,nbits_in_reservoir,stream,nbits_wanted,result) do { \
   fill_nbits(reservoir,nbits_in_reservoir,stream,(nbits_wanted)); \
   result = ((reservoir)>>(nbits_in_reservoir-(nbits_wanted))); \
   nbits_in_reservoir -= (nbits_wanted);  \
   reservoir &= ((1U<<nbits_in_reservoir)-1); \
   if ((unsigned int)result < (1UL<<((nbits_wanted)-1))) \
       result += (0xFFFFFFFFUL<<(nbits_wanted))+1; \
}  while(0);
