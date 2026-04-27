/* Phase 3 Plan 01: ADPCM subcommands for the spu94 CLI.
 *
 * Three subcommands:
 *   spu94 adpcm-encode INPUT.wav OUTPUT.vag
 *   spu94 adpcm-decode INPUT.vag OUTPUT.wav
 *   spu94 adpcm-roundtrip INPUT.wav OUTPUT.wav
 */
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_vag.h>

#include "wav_io.h"

/* One-line error printing discipline (D-05). */
#define SPU94_ERROR(...) do {                            \
        fputs("spu94: error: ", stderr);                 \
        fprintf(stderr, __VA_ARGS__);                    \
        fputc('\n', stderr);                             \
    } while (0)

/* -----------------------------------------------------------------------
 * adpcm-encode: WAV -> VAG
 * ----------------------------------------------------------------------- */

static void print_encode_help(void) {
    fputs(
        "Usage: spu94 adpcm-encode INPUT.wav OUTPUT.vag\n"
        "\n"
        "Encode a WAV file to Sony ADPCM (.vag) format.\n"
        "Stereo input produces a dual-mono VAG (two consecutive mono streams).\n"
        "\n"
        "Options:\n"
        "  -h, --help    Show this message and exit.\n",
        stdout);
}

/* Encode one channel of PCM samples into ADPCM blocks, writing to fp.
 * Returns 0 on success, -1 on write error. */
static int encode_channel(const int16_t *samples, uint64_t num_frames,
                          uint32_t sample_rate, FILE *fp) {
    uint64_t num_blocks = (num_frames + SPU94_ADPCM_BLOCK_SAMPLES - 1)
                          / SPU94_ADPCM_BLOCK_SAMPLES;
    uint32_t data_size = (uint32_t)(num_blocks * SPU94_ADPCM_BLOCK_BYTES);

    /* Write VAG header */
    uint8_t hdr_buf[SPU94_VAG_HEADER_BYTES];
    spu94_vag_write_header(hdr_buf, data_size, sample_rate, "spu94");
    if (fwrite(hdr_buf, 1, SPU94_VAG_HEADER_BYTES, fp) != SPU94_VAG_HEADER_BYTES)
        return -1;

    /* Encode blocks */
    spu94_adpcm_state enc_state = {0, 0};
    for (uint64_t b = 0; b < num_blocks; b++) {
        uint64_t offset = b * SPU94_ADPCM_BLOCK_SAMPLES;
        uint64_t remaining = num_frames - offset;
        uint32_t block_len = (remaining < SPU94_ADPCM_BLOCK_SAMPLES)
                             ? (uint32_t)remaining
                             : SPU94_ADPCM_BLOCK_SAMPLES;

        int16_t padded[SPU94_ADPCM_BLOCK_SAMPLES];
        memset(padded, 0, sizeof padded);
        memcpy(padded, &samples[offset], block_len * sizeof(int16_t));

        uint8_t flags = (b == num_blocks - 1) ? SPU94_VAG_FLAG_END
                                              : SPU94_VAG_FLAG_NORMAL;
        uint8_t block[SPU94_ADPCM_BLOCK_BYTES];
        spu94_adpcm_encode_block(&enc_state, padded, flags, block);

        if (fwrite(block, 1, SPU94_ADPCM_BLOCK_BYTES, fp) != SPU94_ADPCM_BLOCK_BYTES)
            return -1;
    }
    return 0;
}

int cmd_adpcm_encode(int argc, char **argv) {
    optind = 1;
    opterr = 0;
    static const struct option long_opts[] = {
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_opts, NULL)) != -1) {
        if (opt == 'h') { print_encode_help(); return 0; }
    }
    if (argc - optind != 2) {
        SPU94_ERROR("adpcm-encode requires INPUT.wav OUTPUT.vag (try --help)");
        return 2;
    }
    const char *in_path  = argv[optind];
    const char *out_path = argv[optind + 1];

    char err_buf[512];
    spu94_cli_wav_t input;
    memset(&input, 0, sizeof input);
    if (spu94_cli_wav_load(in_path, &input, err_buf, sizeof err_buf) != 0) {
        SPU94_ERROR("%s", err_buf);
        return 2;
    }

    FILE *fp = fopen(out_path, "wb");
    if (!fp) {
        free(input.L); free(input.R);
        SPU94_ERROR("cannot open output file '%s'", out_path);
        return 2;
    }

    /* Encode left channel */
    if (encode_channel(input.L, input.num_frames, input.sample_rate, fp) != 0) {
        fclose(fp);
        free(input.L); free(input.R);
        SPU94_ERROR("write error encoding left channel");
        return 2;
    }

    /* Encode right channel if stereo (D-04: dual-sequential) */
    if (input.num_channels >= 2 && input.R) {
        if (encode_channel(input.R, input.num_frames, input.sample_rate, fp) != 0) {
            fclose(fp);
            free(input.L); free(input.R);
            SPU94_ERROR("write error encoding right channel");
            return 2;
        }
    }

    fclose(fp);
    free(input.L); free(input.R);
    return 0;
}

/* -----------------------------------------------------------------------
 * adpcm-decode: VAG -> WAV
 * ----------------------------------------------------------------------- */

static void print_decode_help(void) {
    fputs(
        "Usage: spu94 adpcm-decode INPUT.vag OUTPUT.wav\n"
        "\n"
        "Decode a .vag file back to WAV.\n"
        "Dual-mono VAG files (stereo) are detected automatically.\n"
        "\n"
        "Options:\n"
        "  -h, --help    Show this message and exit.\n",
        stdout);
}

/* Decode one VAG stream from fp (header already parsed).
 * Allocates *out_samples. Returns actual decoded sample count. */
static int64_t decode_stream(FILE *fp, const spu94_vag_header *hdr,
                             int16_t **out_samples) {
    uint64_t num_blocks = (uint64_t)hdr->data_size / SPU94_ADPCM_BLOCK_BYTES;
    if (num_blocks == 0) {
        *out_samples = NULL;
        return 0;
    }

    /* T-03-02: cap allocation to data_size/16 * 28 * sizeof(int16_t) */
    uint64_t max_samples = num_blocks * SPU94_ADPCM_BLOCK_SAMPLES;
    int16_t *samples = (int16_t *)malloc((size_t)(max_samples * sizeof(int16_t)));
    if (!samples) {
        *out_samples = NULL;
        return -1;
    }

    spu94_adpcm_state dec_state = {0, 0};
    uint64_t total_samples = 0;

    for (uint64_t b = 0; b < num_blocks; b++) {
        uint8_t block[SPU94_ADPCM_BLOCK_BYTES];
        if (fread(block, 1, SPU94_ADPCM_BLOCK_BYTES, fp) != SPU94_ADPCM_BLOCK_BYTES)
            break;

        int16_t decoded[SPU94_ADPCM_BLOCK_SAMPLES];
        uint8_t flags = spu94_adpcm_decode_block(&dec_state, block, decoded);

        memcpy(&samples[total_samples], decoded,
               SPU94_ADPCM_BLOCK_SAMPLES * sizeof(int16_t));
        total_samples += SPU94_ADPCM_BLOCK_SAMPLES;

        /* Stop on end or terminator flags */
        if (flags == SPU94_VAG_FLAG_END || flags == SPU94_VAG_FLAG_TERMINATOR)
            break;
    }

    *out_samples = samples;
    return (int64_t)total_samples;
}

int cmd_adpcm_decode(int argc, char **argv) {
    optind = 1;
    opterr = 0;
    static const struct option long_opts[] = {
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_opts, NULL)) != -1) {
        if (opt == 'h') { print_decode_help(); return 0; }
    }
    if (argc - optind != 2) {
        SPU94_ERROR("adpcm-decode requires INPUT.vag OUTPUT.wav (try --help)");
        return 2;
    }
    const char *in_path  = argv[optind];
    const char *out_path = argv[optind + 1];

    FILE *fp = fopen(in_path, "rb");
    if (!fp) {
        SPU94_ERROR("cannot open input file '%s'", in_path);
        return 2;
    }

    /* Read and parse first VAG header */
    uint8_t hdr_buf[SPU94_VAG_HEADER_BYTES];
    if (fread(hdr_buf, 1, SPU94_VAG_HEADER_BYTES, fp) != SPU94_VAG_HEADER_BYTES) {
        fclose(fp);
        SPU94_ERROR("file too short for VAG header");
        return 2;
    }

    spu94_vag_header hdr;
    if (spu94_vag_read_header(hdr_buf, &hdr) != 0) {
        fclose(fp);
        SPU94_ERROR("invalid VAG file (bad magic)");
        return 2;
    }

    /* Decode left channel */
    int16_t *L = NULL;
    int64_t L_count = decode_stream(fp, &hdr, &L);
    if (L_count < 0) {
        fclose(fp);
        SPU94_ERROR("out of memory decoding left channel");
        return 2;
    }

    /* Check for second VAG header (dual-mono stereo per D-04) */
    int16_t *R = NULL;
    int64_t R_count = 0;
    uint8_t hdr2_buf[SPU94_VAG_HEADER_BYTES];
    if (fread(hdr2_buf, 1, SPU94_VAG_HEADER_BYTES, fp) == SPU94_VAG_HEADER_BYTES) {
        spu94_vag_header hdr2;
        if (spu94_vag_read_header(hdr2_buf, &hdr2) == 0) {
            R_count = decode_stream(fp, &hdr2, &R);
            if (R_count < 0) {
                free(L);
                fclose(fp);
                SPU94_ERROR("out of memory decoding right channel");
                return 2;
            }
        }
    }
    fclose(fp);

    /* If mono, duplicate L to R for WAV output */
    uint64_t num_frames = (uint64_t)L_count;
    if (!R || R_count == 0) {
        R = (int16_t *)malloc((size_t)(num_frames * sizeof(int16_t)));
        if (!R) {
            free(L);
            SPU94_ERROR("out of memory allocating right channel");
            return 2;
        }
        memcpy(R, L, (size_t)(num_frames * sizeof(int16_t)));
    } else {
        /* Use the shorter of the two channels */
        if ((uint64_t)R_count < num_frames)
            num_frames = (uint64_t)R_count;
    }

    char err_buf[512];
    int wrc = spu94_cli_wav_write(out_path, L, R, num_frames,
                                  hdr.sample_rate, err_buf, sizeof err_buf);
    free(L);
    free(R);
    if (wrc != 0) {
        SPU94_ERROR("%s", err_buf);
        return 2;
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * adpcm-roundtrip: WAV -> ADPCM in memory -> WAV
 * ----------------------------------------------------------------------- */

static void print_roundtrip_help(void) {
    fputs(
        "Usage: spu94 adpcm-roundtrip INPUT.wav OUTPUT.wav\n"
        "\n"
        "Encode WAV to ADPCM in memory and decode back to WAV.\n"
        "Shows ADPCM coloration in isolation (no intermediate .vag file).\n"
        "\n"
        "Options:\n"
        "  -h, --help    Show this message and exit.\n",
        stdout);
}

/* Process one channel through encode+decode roundtrip in memory. */
static void roundtrip_channel(const int16_t *in, int16_t *out,
                              uint64_t num_frames) {
    spu94_adpcm_state enc_state = {0, 0};
    spu94_adpcm_state dec_state = {0, 0};

    for (uint64_t i = 0; i < num_frames; i += SPU94_ADPCM_BLOCK_SAMPLES) {
        uint64_t remaining = num_frames - i;
        uint32_t block_len = (remaining < SPU94_ADPCM_BLOCK_SAMPLES)
                             ? (uint32_t)remaining
                             : SPU94_ADPCM_BLOCK_SAMPLES;

        int16_t padded[SPU94_ADPCM_BLOCK_SAMPLES];
        memset(padded, 0, sizeof padded);
        memcpy(padded, &in[i], block_len * sizeof(int16_t));

        uint8_t block[SPU94_ADPCM_BLOCK_BYTES];
        spu94_adpcm_encode_block(&enc_state, padded, 0, block);

        int16_t decoded[SPU94_ADPCM_BLOCK_SAMPLES];
        spu94_adpcm_decode_block(&dec_state, block, decoded);

        memcpy(&out[i], decoded, block_len * sizeof(int16_t));
    }
}

int cmd_adpcm_roundtrip(int argc, char **argv) {
    optind = 1;
    opterr = 0;
    static const struct option long_opts[] = {
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_opts, NULL)) != -1) {
        if (opt == 'h') { print_roundtrip_help(); return 0; }
    }
    if (argc - optind != 2) {
        SPU94_ERROR("adpcm-roundtrip requires INPUT.wav OUTPUT.wav (try --help)");
        return 2;
    }
    const char *in_path  = argv[optind];
    const char *out_path = argv[optind + 1];

    char err_buf[512];
    spu94_cli_wav_t input;
    memset(&input, 0, sizeof input);
    if (spu94_cli_wav_load(in_path, &input, err_buf, sizeof err_buf) != 0) {
        SPU94_ERROR("%s", err_buf);
        return 2;
    }

    int16_t *L_out = (int16_t *)malloc((size_t)(input.num_frames * sizeof(int16_t)));
    int16_t *R_out = (int16_t *)malloc((size_t)(input.num_frames * sizeof(int16_t)));
    if (!L_out || !R_out) {
        free(L_out); free(R_out);
        free(input.L); free(input.R);
        SPU94_ERROR("out of memory allocating output buffers");
        return 2;
    }

    roundtrip_channel(input.L, L_out, input.num_frames);
    if (input.R) {
        roundtrip_channel(input.R, R_out, input.num_frames);
    } else {
        memcpy(R_out, L_out, (size_t)(input.num_frames * sizeof(int16_t)));
    }

    int wrc = spu94_cli_wav_write(out_path, L_out, R_out, input.num_frames,
                                  input.sample_rate, err_buf, sizeof err_buf);
    free(L_out); free(R_out);
    free(input.L); free(input.R);
    if (wrc != 0) {
        SPU94_ERROR("%s", err_buf);
        return 2;
    }

    return 0;
}
