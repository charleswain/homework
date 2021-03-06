
#include "encode.h"
#include "huffman.h"
#include "pack.h"
#include "qzz.h"
#include "dct.h"
#include "tiff.h"
#include "conv.h"
#include "upsampler.h"
#include "downsampler.h"
#include "library.h"

/* Computes how many MCUs are required to cover a given dimension */
static inline uint16_t mcu_per_dim(uint8_t mcu, uint16_t dim);

/* Compresses raw mcu data, and computes Huffman tables */
void compute_jpeg(struct jpeg_data *jpeg, bool *error)
{
        if (jpeg == NULL || *error) {
                *error = true;
                return;
        }

        uint8_t i_c;
        uint8_t mcu_h = jpeg->mcu.h;
        uint8_t mcu_v = jpeg->mcu.v;
        uint8_t mcu_h_dim = jpeg->mcu.h_dim;
        uint8_t mcu_v_dim = jpeg->mcu.v_dim;
        uint32_t nb_mcu = jpeg->mcu.nb;
        uint32_t block_idx = 0;

        uint8_t nb_blocks_h, nb_blocks_v, nb_blocks;
        uint8_t i_q;
        int32_t *last_DC;

        int32_t *block;
        int32_t qzz[BLOCK_SIZE];
        uint8_t *mcu_data, *qtable;
        uint8_t dct[mcu_h_dim * mcu_v_dim][BLOCK_SIZE];

        uint32_t *mcu_RGB = NULL;
        uint8_t data_YCbCr[3][mcu_h * mcu_v];
        uint8_t *mcu_YCbCr[3] = {
                (uint8_t*)&data_YCbCr[0],
                (uint8_t*)&data_YCbCr[1],
                (uint8_t*)&data_YCbCr[2]
        };


        /* Use 1 table for each tree (AC/DC)
         * Allocate 0x100 values because Huffman values use 8 bits */
        uint32_t freq_data[MAX_COMPS][2][0x100];

        /* Components's frequence tables */
        uint32_t *freqs[MAX_COMPS][2] = {
                { (uint32_t*)&freq_data[0][0], (uint32_t*)&freq_data[0][1] },
                { (uint32_t*)&freq_data[1][0], (uint32_t*)&freq_data[1][1] },
                { (uint32_t*)&freq_data[2][0], (uint32_t*)&freq_data[2][1] }
        };

        /* Set default frequencies to 0 */
        memset(freq_data, 0, sizeof(freq_data));


        const uint8_t nb_mcu_blocks = mcu_h_dim * mcu_v_dim + (jpeg->nb_comps - 1);
        jpeg->mcu_data = malloc(nb_mcu * nb_mcu_blocks * BLOCK_SIZE * sizeof(int32_t));

        if (jpeg->mcu_data == NULL) {
                *error = true;
                return;
        }

        /* Encode all MCUs */
        for (uint32_t i = 0; i < nb_mcu; i++) {

                mcu_RGB = &(jpeg->raw_data[i * jpeg->mcu.size]);

                /* Convert RGB to YCbCr for color images */
                if (jpeg->nb_comps == 3)
                        ARGB_to_YCbCr(mcu_RGB, mcu_YCbCr, mcu_h_dim, mcu_v_dim);

                /* Convert RGB to Y for gray images */
                else if (jpeg->nb_comps == 1)
                        ARGB_to_Y(mcu_RGB, mcu_YCbCr[0], mcu_h_dim, mcu_v_dim);

                else
                        *error = true;


                /* Encode each component in the correct order */
                for (uint8_t j = 0; j < jpeg->nb_comps; j++) {

                        /* Retrieve component informations */
                        i_c = jpeg->comp_order[j];
                        i_q = jpeg->comps[i_c].i_q;
                        nb_blocks_h = jpeg->comps[i_c].nb_blocks_h;
                        nb_blocks_v = jpeg->comps[i_c].nb_blocks_v;
                        nb_blocks = nb_blocks_h * nb_blocks_v;
                        last_DC = &jpeg->comps[i_c].last_DC;

                        /* Downsample current MCUs */
                        mcu_data = mcu_YCbCr[i_c];
                        downsampler(mcu_data, mcu_h_dim, mcu_v_dim, (uint8_t*)dct, nb_blocks_h, nb_blocks_v);

                        /* Compress each MCU and compute data frequencies */
                        for (uint8_t n = 0; n < nb_blocks; n++) {

                                dct_block((uint8_t*)&dct[n], qzz);

                                block = &jpeg->mcu_data[block_idx];

                                qtable = (uint8_t*)&jpeg->qtables[i_q];
                                qzz_block (qzz, block, qtable);

                                /* Empty pack_block execution counting frequencies */
                                pack_block(NULL, NULL, last_DC, NULL, block, freqs[i_c]);

                                block_idx += BLOCK_SIZE;
                        }
                }
        }

        /*  Reset last_DC fields so that write_blocks can work fine */
        for (uint8_t i = 0; i < jpeg->nb_comps; i++)
                jpeg->comps[i].last_DC = 0;

        /*  Create all Huffman trees */
        for (uint8_t i = 0; i < jpeg->nb_comps; i++) {
                jpeg->htables[0][i] = create_huffman_tree(freqs[i][0], error);
                jpeg->htables[1][i] = create_huffman_tree(freqs[i][1], error);
        }
}

/* Writes a whole JPEG header */
void write_header(struct bitstream *stream, struct jpeg_data *jpeg, bool *error)
{
        /* Write header data */
        write_section(stream, SOI, jpeg, error);
        write_section(stream, APP0, jpeg, error);
        write_section(stream, COM, jpeg, error);
        write_section(stream, SOF0, jpeg, error);

        /* Write all Quantification tables */
        write_section(stream, DQT, jpeg, error);

        /* Write all Huffman tables */
        write_section(stream, DHT, jpeg, error);

        /* Write SOS data */
        write_section(stream, SOS, jpeg, error);
}

/* Writes a specific JPEG section */
void write_section(struct bitstream *stream, enum jpeg_section section,
                   struct jpeg_data *jpeg, bool *error)
{
        uint8_t byte;

        if (stream == NULL || error == NULL || *error)
                return;


        /* Write SECTION_HEAD */
        write_byte(stream, SECTION_HEAD);

        /* Write section marker */
        write_byte(stream, section);


        if (section == SOI || section == EOI)
                return;


        uint16_t size = 0;
        uint32_t pos_size;

        /* Write dummy size : rewrite later */
        pos_size = pos_bitstream(stream);
        write_short_BE(stream, 0);


        /* Process each section */
        switch (section) {

        case APP0:
                if (!*error) {
                        const char *jfif = "JFIF";

                        for (uint8_t i = 0; i < 5; i++)
                                write_byte(stream, jfif[i]);

                        /* Current JFIF version : 1.2 */
                        write_byte(stream, 0x01);
                        write_byte(stream, 0x02);

                        /* Density units : No units, aspect ratio only specified */
                        write_byte(stream, 0x00);

                        /* X density : Integer horizontal pixel density */
                        write_short_BE(stream, 1);

                        /* Y density : Integer vertical pixel density */
                        write_short_BE(stream, 1);


                        /* Thumbnail width (no thumbnail) */
                        write_byte(stream, 0);

                        /* Thumbnail height (no thumbnail) */
                        write_byte(stream, 0);
                }
                break;

        case COM:
                {
                        const char *comment = COMMENT;
                        const uint16_t len = strlen(comment);

                        for (uint16_t i = 0; i < len; i++)
                                write_byte(stream, comment[i]);
                }

                break;

        case DQT:
                if (jpeg != NULL) {

                        bool mem[MAX_QTABLES];
                        memset(mem, 0, sizeof(mem));

                        /* Write all quantification tables */
                        for (uint8_t i = 0; i < jpeg->nb_comps; i++) {

                                /* Accuracy : 0 for 8 bits */
                                const uint8_t accuracy = 0;
                                uint8_t i_q = jpeg->comps[i].i_q;

                                if (mem[i_q])
                                        continue;

                                if (i_q >= MAX_QTABLES)
                                        *error = true;

                                else {
                                        mem[i_q] = true;
                                        byte = accuracy << 4;
                                        byte |= i_q & 0xF;
                                        write_byte(stream, byte);
                                }


                                /*
                                 * Write one quantification table
                                 */
                                if (i_q < MAX_QTABLES) {
                                        uint8_t *qtable = (uint8_t*)&jpeg->qtables[i_q];

                                        for (uint8_t j = 0; j < BLOCK_SIZE; j++)
                                                write_byte(stream, qtable[j]);

                                } else
                                        *error = true;

                                /* Update jpeg status */
                                jpeg->state |= DQT_OK;
                        }
                } else
                        *error = true;

                break;

        case SOF0:
                if (jpeg != NULL) {
                        const uint8_t accuracy = 8;
                        write_byte(stream, accuracy);


                        /* Read jpeg informations */
                        write_short_BE(stream, jpeg->height);
                        write_short_BE(stream, jpeg->width);
                        write_byte(stream, jpeg->nb_comps);


                        /* Write all component informations */
                        for (uint8_t i = 0; i < jpeg->nb_comps; i++) {
                                uint8_t i_c = i + 1;
                                uint8_t i_q = jpeg->comps[i].i_q;
                                uint8_t h_sampling_factor = jpeg->comps[i].nb_blocks_h;
                                uint8_t v_sampling_factor = jpeg->comps[i].nb_blocks_v;

                                /* Write component index */
                                write_byte(stream, i_c);


                                /* Write sampling factors */
                                byte = h_sampling_factor << 4;
                                byte |= v_sampling_factor & 0xF;
                                write_byte(stream, byte);

                                /*
                                 * Write the component's
                                 * quantification table index
                                 */
                                write_byte(stream, i_q);

                                jpeg->state |= SOF0_OK;
                        }
                } else
                        *error = true;

                break;

        case DHT:
                if (jpeg != NULL) {
                        uint8_t i_h;
                        uint8_t type;

                        /* Write all Huffman tables */
                        for (uint8_t h = 0; h < 2; h++) {
                                type = h;

                                for (uint8_t i = 0; i < MAX_HTABLES; i++) {

                                        i_h = i;
                                        struct huff_table **table = &jpeg->htables[type][i_h];

                                        if (*table == NULL)
                                                continue;

                                        /*
                                         * 0 for DC
                                         * 1 for AC
                                         */
                                        byte = (type & 1) << 4;
                                        byte |= i_h & 0xF;
                                        write_byte(stream, byte);

                                        /* Write one Huffman table */
                                        write_huffman_table(stream, table);
                                }
                        }

                        jpeg->state |= DHT_OK;
                } else
                        *error = true;

                break;

        /* Start Of Scan */
        case SOS:
                if (jpeg != NULL) {
                        uint8_t i_c;

                        write_byte(stream, jpeg->nb_comps);

                        /* Write component informations */
                        for (uint8_t i = 0; i < jpeg->nb_comps; i++) {

                                i_c = jpeg->comp_order[i];
                                write_byte(stream, i_c + 1);


                                /* Write Huffman table indexes */
                                uint8_t i_dc = jpeg->comps[i_c].i_dc;
                                uint8_t i_ac = jpeg->comps[i_c].i_ac;

                                byte = (i_dc << 4) | (i_ac & 0xF);

                                write_byte(stream, byte);
                        }

                        /* Write remaining SOS data */
                        write_byte(stream, 0x00);
                        write_byte(stream, 0x3F);
                        write_byte(stream, 0x00);
                } else
                        *error = true;

                break;

        // case TEM:
        // case DNL:
        // case DHP:
        // case EXP:
        default:
                *error = true;
        }


        uint32_t end_section;

        end_section = pos_bitstream(stream);
        size = end_section - pos_size;

        /* Write section size and then restore current position */
        seek_bitstream(stream, pos_size);
        write_short_BE(stream, size);

        seek_bitstream(stream, end_section);
}

/* Writes previously compressed JPEG data */
void write_blocks(struct bitstream *stream, struct jpeg_data *jpeg, bool *error)
{
        if (stream == NULL || *error || jpeg == NULL || jpeg->state != ALL_OK) {
                *error = true;
                return;
        }

        uint8_t i_c;
        uint32_t nb_mcu = jpeg->mcu.nb;
        uint8_t nb_blocks_h, nb_blocks_v, nb_blocks;
        int32_t *last_DC;
        int32_t *block;
        uint32_t block_idx = 0;


        /* Write all compressed MCUs */
        for (uint32_t i = 0; i < nb_mcu; i++) {

                /* Write each component in the correct order */
                for (uint8_t i = 0; i < jpeg->nb_comps; i++) {

                        /* Retrieve component informations */
                        i_c = jpeg->comp_order[i];
                        nb_blocks_h = jpeg->comps[i_c].nb_blocks_h;
                        nb_blocks_v = jpeg->comps[i_c].nb_blocks_v;
                        nb_blocks = nb_blocks_h * nb_blocks_v;
                        last_DC = &jpeg->comps[i_c].last_DC;

                        /* Write each MCU */
                        for (uint8_t n = 0; n < nb_blocks; n++) {
                                block = &jpeg->mcu_data[block_idx];

                                pack_block(stream, jpeg->htables[0][i_c], last_DC,
                                                     jpeg->htables[1][i_c], block, NULL);

                                block_idx += BLOCK_SIZE;
                        }

                }
        }

        /* Enforce last bits into the file */
        flush_bitstream(stream);
}

/* Detects MCU informations from header data */
void detect_mcu(struct jpeg_data *jpeg, bool *error)
{
        uint8_t mcu_h = BLOCK_DIM;
        uint8_t mcu_v = BLOCK_DIM;

        /* Extract the MCU size */
        for (uint8_t i = 0; i < jpeg->nb_comps; i++) {
                mcu_h *= jpeg->comps[i].nb_blocks_h;
                mcu_v *= jpeg->comps[i].nb_blocks_v;
        }

        jpeg->mcu.h = mcu_h;
        jpeg->mcu.v = mcu_v;

        compute_mcu(jpeg, error);
}

/* Computes MCU informations */
void compute_mcu(struct jpeg_data *jpeg, bool *error)
{
        uint8_t mcu_h = jpeg->mcu.h;
        uint8_t mcu_v = jpeg->mcu.v;
        uint8_t mcu_h_dim = mcu_h / BLOCK_DIM;
        uint8_t mcu_v_dim = mcu_v / BLOCK_DIM;

        /* Various checks ensuring MCU validity */
        if (jpeg->width == 0
                || jpeg->height == 0
                || !(mcu_h == 8 || mcu_h == 16)
                || !(mcu_v == 8 || mcu_v == 16))
                *error = true;

        else {
                /* Count MCUs */
                uint32_t nb_mcu_h = mcu_per_dim(mcu_h, jpeg->width);
                uint32_t nb_mcu_v = mcu_per_dim(mcu_v, jpeg->height);
                uint32_t nb_mcu = nb_mcu_h * nb_mcu_v;


                /* Store computed MCU data */
                jpeg->mcu.h_dim = mcu_h_dim;
                jpeg->mcu.v_dim = mcu_v_dim;

                jpeg->mcu.nb_h = nb_mcu_h;
                jpeg->mcu.nb_v = nb_mcu_v;
                jpeg->mcu.nb = nb_mcu;

                jpeg->mcu.size = mcu_h * mcu_v;


                /* Y blocks */
                jpeg->comps[0].nb_blocks_h = mcu_h_dim;
                jpeg->comps[0].nb_blocks_v = mcu_v_dim;

                /* Cb blocks */
                jpeg->comps[1].nb_blocks_h = 1;
                jpeg->comps[1].nb_blocks_v = 1;

                /* Cr blocks */
                jpeg->comps[2].nb_blocks_h = 1;
                jpeg->comps[2].nb_blocks_v = 1;
        }
}

/* Computes how many MCUs are required to cover a given dimension */
static inline uint16_t mcu_per_dim(uint8_t mcu, uint16_t dim)
{
        uint16_t nb = dim / mcu;

        return (dim % mcu) ? ++nb : nb;
}

/* Frees all JPEG Huffman tables */
void free_jpeg_data(struct jpeg_data *jpeg)
{
        if (jpeg == NULL)
                return;

        for (uint8_t i = 0; i < 2; i++)
                for (uint8_t j = 0; j < MAX_HTABLES; j++)
                        free_huffman_table(jpeg->htables[i][j]);
}

/*zoon image  add by chuanwen*/
void zoom_image(struct jpeg_data *jpeg, bool *error){
    if(error ==NULL || *error || jpeg ==NULL){
            return ;
    }  
    uint32_t oheight = jpeg->height,owidth = jpeg->width;
    uint32_t  omcu_nb = jpeg->mcu.nb;
    float rate = 0.5;
    uint32_t nheight = oheight, nwidth = owidth*rate;
    uint32_t nmcu_nb = omcu_nb*rate;
      
    uint32_t scale = jpeg->width* jpeg->height;
    uint32_t* oraw_data = jpeg->raw_data;

    uint8_t mcu_h_dim = jpeg->mcu.h_dim;
    uint8_t mcu_v_dim = jpeg->mcu.v_dim;

    jpeg->raw_data = calloc(scale* rate,  sizeof(uint32_t));

    //narrow -mcus 
    uint8_t mcu_v = jpeg->mcu.nb_v;
    uint8_t mcu_h = jpeg->mcu.nb_h;
//    jpeg->mcu.nb= 45;
   uint8_t mcu_size = 255;
    trace(">> mcu_h:%d, muc_v:%d, mcu_size:%d\n", mcu_h, mcu_v, jpeg->mcu.size);
    trace(">> width:%d, height:%d , mcu_h_dim:%d, mcu_v_dim:%d\n", nwidth, nheight,jpeg->mcu.h ,jpeg->mcu.v);

    /* Initialize output information */
  //  jpeg->mcu.h = mcu_h;
  //  jpeg->mcu.v = mcu_v;
    jpeg->width = nwidth;
    jpeg->height = nheight;

    /*compute mcu*/
    compute_mcu(jpeg, error);

  //  for(uint32_t i=0;i<scale*rate;i++){
  //      jpeg->raw_data[i] = oraw_data[i];
  //  }


    for(uint32_t i=0;i<mcu_v;i++){
        for(uint32_t j=0;j<mcu_h;j++){
            uint32_t mcu_count = (i*mcu_v + j)*mcu_size;
           // trace("%d ", mcu_count);
            for(uint8_t k=0;k<mcu_size && ( mcu_count+ k) <scale*rate;k++){
               jpeg->raw_data[mcu_count + k] = oraw_data[2*mcu_count + k];
            }
        }
    }

    trace(">> zooming....\n");
/*
   //magnify  
    uint32_t size = scale;
    for(uint32_t i=0,j=0;j<size;j++,i+=2){
        jpeg->raw_data[i] = oraw_data[j];
        jpeg->raw_data[i+1] = oraw_data[j];
    }
*/
    SAFE_FREE(oraw_data);
}

