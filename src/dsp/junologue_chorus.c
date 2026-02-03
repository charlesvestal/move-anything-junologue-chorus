/*
 * Junologue Chorus - Juno-60 Chorus Emulation for Move Anything
 *
 * Port of peterall/junologue-chorus (MIT License)
 * Original by Peter Allwin
 * Based on research by Andy Harman (pendragon-andyh/Juno60)
 * and JP Cimalando. Soft limiter from Emilie Gillet's stmlib.
 *
 * Chorus I:   0.513 Hz triangle LFO, 1.66-5.35ms delay, stereo
 * Chorus II:  0.863 Hz triangle LFO, 1.66-5.35ms delay, stereo
 * Chorus I+II: Both LFOs mixed at equal gain (Korg interpretation)
 *
 * Stereo is created by reading the delay with inverted LFO for
 * the right channel (180-degree phase opposition), matching the
 * original Juno-60 hardware which uses two BBD lines with
 * inverted modulation.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "plugin_api_v1.h"

#define SAMPLE_RATE 44100.0f

/* Delay buffer - power of 2 for efficient masking */
#define DELAY_BUF_SIZE 512
#define DELAY_BUF_MASK (DELAY_BUF_SIZE - 1)

/*
 * Juno-60 chorus delay times from Andy Harman's measurements:
 * Min delay: 1.66ms, Max delay: 5.35ms (same for both channels)
 * Stereo from inverted LFO modulation between left and right BBDs.
 */
#define DELAY_MIN_SEC  0.00166f
#define DELAY_MAX_SEC  0.00535f

/* Pre-computed delay times in samples at 44100 Hz */
static const float DT_MIN_S = DELAY_MIN_SEC * 44100.0f;   /* ~73.2 samples */
static const float DT_RNG_S = (DELAY_MAX_SEC - DELAY_MIN_SEC) * 44100.0f; /* ~162.7 samples */

/* LFO rates in Hz (from Harman's measurements) */
static const float LFO_RATE[2] = { 0.513f, 0.863f };

/* Mode gains: [lfo1_gain, lfo2_gain] */
static const float MODE_GAIN[3][2] = {
    { 1.0f, 0.0f },                        /* Mode I   */
    { 0.70710678f, 0.70710678f },           /* Mode I+II */
    { 0.0f, 1.0f }                          /* Mode II  */
};

/* ================================================================
 * DSP Primitives
 * ================================================================ */

/* Soft limiter (Emilie Gillet / stmlib) */
static inline float soft_limit(float x) {
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/* Fast square root via inverse sqrt approximation */
static inline float fast_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float x2 = x * 0.5f;
    float y = x;
    int32_t i;
    memcpy(&i, &y, sizeof(i));
    i = 0x5f3759df - (i >> 1);
    memcpy(&y, &i, sizeof(y));
    y = y * (1.5f - (x2 * y * y));
    y = y * (1.5f - (x2 * y * y));
    return 1.0f / y;
}

/*
 * One-pole lowpass filter with unity DC gain.
 *
 * y[n] = y[n-1] + alpha * (x[n] - y[n-1])
 *
 * alpha = w / (1 + w)  where w = 2*pi*fc/fs
 * This avoids the tan() instability near Nyquist that the bilinear
 * transform version has, and maintains unity gain at DC.
 */
typedef struct {
    float alpha;
    float state;
} fo_lpf_t;

static void fo_lpf_init(fo_lpf_t *f) {
    f->alpha = 1.0f;   /* pass-through until set */
    f->state = 0.0f;
}

static void fo_lpf_set_cutoff(fo_lpf_t *f, float hz) {
    if (hz <= 0.0f) {
        f->alpha = 0.0f;
        return;
    }
    /* Clamp to below Nyquist */
    if (hz >= SAMPLE_RATE * 0.49f) {
        f->alpha = 1.0f;
        return;
    }
    float w = 2.0f * (float)M_PI * hz / SAMPLE_RATE;
    f->alpha = w / (1.0f + w);
}

static inline float fo_lpf_process(fo_lpf_t *f, float x) {
    f->state += f->alpha * (x - f->state);
    return f->state;
}

/* --- Delay line with fractional read --- */

typedef struct {
    float buf[DELAY_BUF_SIZE];
    int write_pos;
} delay_line_t;

static void delay_init(delay_line_t *d) {
    memset(d->buf, 0, sizeof(d->buf));
    d->write_pos = 0;
}

static inline void delay_write(delay_line_t *d, float x) {
    d->buf[d->write_pos] = x;
    d->write_pos = (d->write_pos + 1) & DELAY_BUF_MASK;
}

static inline float delay_read_frac(delay_line_t *d, float delay_samples) {
    int di = (int)delay_samples;
    float frac = delay_samples - (float)di;
    int p0 = (d->write_pos - 1 - di) & DELAY_BUF_MASK;
    int p1 = (p0 - 1) & DELAY_BUF_MASK;
    return d->buf[p0] * (1.0f - frac) + d->buf[p1] * frac;
}

/* --- Triangle LFO (unipolar 0..1) --- */

typedef struct {
    float phase, phase_inc;
} lfo_t;

static void lfo_init(lfo_t *l, float rate_hz) {
    l->phase = 0.0f;
    l->phase_inc = rate_hz / SAMPLE_RATE;
}

static inline float lfo_tick(lfo_t *l) {
    l->phase += l->phase_inc;
    if (l->phase >= 1.0f) l->phase -= 1.0f;
    float t = l->phase * 2.0f;
    return (t > 1.0f) ? (2.0f - t) : t;
}

/* ================================================================
 * Audio FX API v2 - Instance-based
 * ================================================================ */

static const host_api_v1_t *g_host = NULL;

#define AUDIO_FX_API_VERSION_2 2

typedef struct audio_fx_api_v2 {
    uint32_t api_version;
    void *(*create_instance)(const char *module_dir, const char *config_json);
    void (*destroy_instance)(void *instance);
    void (*process_block)(void *instance, int16_t *audio_inout, int frames);
    void (*set_param)(void *instance, const char *key, const char *val);
    int  (*get_param)(void *instance, const char *key, char *buf, int buf_len);
} audio_fx_api_v2_t;

typedef audio_fx_api_v2_t *(*audio_fx_init_v2_fn)(const host_api_v1_t *host);

/* LPF cutoff ranges in Hz - clamped below Nyquist */
#define PRE_LPF_MIN   2000.0f
#define PRE_LPF_MAX   20000.0f
#define POST_LPF_MIN  6000.0f
#define POST_LPF_MAX  20000.0f

/* Instance structure */
typedef struct {
    char module_dir[256];

    /* Parameters */
    int   mode;         /* 0=I, 1=I+II, 2=II */
    float mix;          /* 0-1 dry/wet */
    float brightness;   /* 0-1 filter brightness */

    /* Derived gains */
    float gain_a;       /* LFO1 tap gain */
    float gain_b;       /* LFO2 tap gain */

    /* DSP state */
    delay_line_t delay;
    lfo_t        lfo1;
    lfo_t        lfo2;
    fo_lpf_t     pre_lpf;
    fo_lpf_t     post_lpf_l;
    fo_lpf_t     post_lpf_r;
} jc_instance_t;

static void jc_log(const char *msg) {
    if (g_host && g_host->log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[junologue-chorus] %s", msg);
        g_host->log(buf);
    }
}

static void jc_update_params(jc_instance_t *inst) {
    /* Mode gains */
    int m = inst->mode;
    if (m < 0) m = 0;
    if (m > 2) m = 2;
    inst->gain_a = MODE_GAIN[m][0];
    inst->gain_b = MODE_GAIN[m][1];

    /* Filter cutoffs from brightness (quadratic curve) */
    float br = inst->brightness * inst->brightness;
    float pre_hz  = PRE_LPF_MIN  + br * (PRE_LPF_MAX  - PRE_LPF_MIN);
    float post_hz = POST_LPF_MIN + br * (POST_LPF_MAX - POST_LPF_MIN);

    fo_lpf_set_cutoff(&inst->pre_lpf,    pre_hz);
    fo_lpf_set_cutoff(&inst->post_lpf_l, post_hz);
    fo_lpf_set_cutoff(&inst->post_lpf_r, post_hz);
}

/* --- API callbacks --- */

static void *v2_create_instance(const char *module_dir, const char *config_json) {
    jc_log("Creating instance");

    jc_instance_t *inst = (jc_instance_t *)calloc(1, sizeof(jc_instance_t));
    if (!inst) {
        jc_log("Failed to allocate instance");
        return NULL;
    }

    if (module_dir)
        strncpy(inst->module_dir, module_dir, sizeof(inst->module_dir) - 1);

    /* Defaults */
    inst->mode       = 1;     /* Mode I+II (richer default) */
    inst->mix        = 0.5f;
    inst->brightness = 1.0f;  /* Full brightness (no filtering) */

    /* Init DSP */
    delay_init(&inst->delay);
    lfo_init(&inst->lfo1, LFO_RATE[0]);
    lfo_init(&inst->lfo2, LFO_RATE[1]);
    fo_lpf_init(&inst->pre_lpf);
    fo_lpf_init(&inst->post_lpf_l);
    fo_lpf_init(&inst->post_lpf_r);

    jc_update_params(inst);

    jc_log("Instance created");
    return inst;
}

static void v2_destroy_instance(void *instance) {
    if (!instance) return;
    jc_log("Destroying instance");
    free(instance);
}

static void v2_process_block(void *instance, int16_t *audio_inout, int frames) {
    jc_instance_t *inst = (jc_instance_t *)instance;
    if (!inst) return;

    const float ga = inst->gain_a;
    const float gb = inst->gain_b;

    /* Equal-power crossfade for dry/wet */
    const float dry_g = fast_sqrt(1.0f - inst->mix);
    const float wet_g = fast_sqrt(inst->mix);

    for (int i = 0; i < frames; i++) {
        float in_l = (float)audio_inout[i * 2]     / 32768.0f;
        float in_r = (float)audio_inout[i * 2 + 1] / 32768.0f;

        /*
         * Mono sum -> soft-limit -> pre-filter -> delay write
         * The Juno-60 sums to mono before the BBD (no compander).
         */
        float mono = (in_l + in_r) * 0.5f;
        mono = fo_lpf_process(&inst->pre_lpf, soft_limit(mono));
        delay_write(&inst->delay, mono);

        /* Advance LFOs */
        float v1 = lfo_tick(&inst->lfo1);
        float v2 = lfo_tick(&inst->lfo2);

        /*
         * Read delay with same range for L and R, but inverted LFO
         * for the right channel (180-degree phase opposition),
         * matching the Juno-60's dual-BBD stereo architecture.
         */
        float tap1_l = delay_read_frac(&inst->delay, DT_MIN_S + DT_RNG_S * v1);
        float tap1_r = delay_read_frac(&inst->delay, DT_MIN_S + DT_RNG_S * (1.0f - v1));
        float tap2_l = delay_read_frac(&inst->delay, DT_MIN_S + DT_RNG_S * v2);
        float tap2_r = delay_read_frac(&inst->delay, DT_MIN_S + DT_RNG_S * (1.0f - v2));

        /* Combine taps with mode gains */
        float wet_l = tap1_l * ga + tap2_l * gb;
        float wet_r = tap1_r * ga + tap2_r * gb;

        /* Post-filter */
        wet_l = fo_lpf_process(&inst->post_lpf_l, wet_l);
        wet_r = fo_lpf_process(&inst->post_lpf_r, wet_r);

        /* Mix dry and wet */
        float out_l = in_l * dry_g + wet_l * wet_g;
        float out_r = in_r * dry_g + wet_r * wet_g;

        /* Clamp */
        if (out_l >  1.0f) out_l =  1.0f;
        if (out_l < -1.0f) out_l = -1.0f;
        if (out_r >  1.0f) out_r =  1.0f;
        if (out_r < -1.0f) out_r = -1.0f;

        audio_inout[i * 2]     = (int16_t)(out_l * 32767.0f);
        audio_inout[i * 2 + 1] = (int16_t)(out_r * 32767.0f);
    }
}

/* --- JSON helper --- */

static int json_get_number(const char *json, const char *key, float *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;
    pos += strlen(search);
    while (*pos == ' ') pos++;
    *out = (float)atof(pos);
    return 0;
}

/* --- Parameter handling --- */

static const char *mode_names[3] = { "I", "I+II", "II" };

static void v2_set_param(void *instance, const char *key, const char *val) {
    jc_instance_t *inst = (jc_instance_t *)instance;
    if (!inst) return;

    /* State restore from patch save */
    if (strcmp(key, "state") == 0) {
        float v;
        if (json_get_number(val, "mode", &v) == 0) {
            inst->mode = (int)v;
            if (inst->mode < 0) inst->mode = 0;
            if (inst->mode > 2) inst->mode = 2;
        }
        if (json_get_number(val, "mix", &v) == 0) inst->mix = v;
        if (json_get_number(val, "brightness", &v) == 0) inst->brightness = v;
        jc_update_params(inst);
        return;
    }

    if (strcmp(key, "mode") == 0) {
        /* Accept both string names and numeric values */
        if (strcmp(val, "I") == 0)         inst->mode = 0;
        else if (strcmp(val, "I+II") == 0) inst->mode = 1;
        else if (strcmp(val, "II") == 0)   inst->mode = 2;
        else {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m > 2) m = 2;
            inst->mode = m;
        }
    } else if (strcmp(key, "mix") == 0) {
        float v = (float)atof(val);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        inst->mix = v;
    } else if (strcmp(key, "brightness") == 0) {
        float v = (float)atof(val);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        inst->brightness = v;
    }

    jc_update_params(inst);
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    jc_instance_t *inst = (jc_instance_t *)instance;
    if (!inst) return -1;

    if (strcmp(key, "mode") == 0) {
        return snprintf(buf, buf_len, "%s", mode_names[inst->mode]);
    } else if (strcmp(key, "mix") == 0) {
        return snprintf(buf, buf_len, "%.2f", inst->mix);
    } else if (strcmp(key, "brightness") == 0) {
        return snprintf(buf, buf_len, "%.2f", inst->brightness);
    } else if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "Juno Chorus");
    } else if (strcmp(key, "state") == 0) {
        return snprintf(buf, buf_len,
            "{\"mode\":%d,\"mix\":%.4f,\"brightness\":%.4f}",
            inst->mode, inst->mix, inst->brightness);
    } else if (strcmp(key, "ui_hierarchy") == 0) {
        const char *h = "{"
            "\"modes\":null,"
            "\"levels\":{"
                "\"root\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"mode\",\"mix\",\"brightness\"],"
                    "\"params\":[\"mode\",\"mix\",\"brightness\"]"
                "}"
            "}"
        "}";
        int len = (int)strlen(h);
        if (len < buf_len) {
            strcpy(buf, h);
            return len;
        }
        return -1;
    }

    return -1;
}

/* ================================================================
 * Entry point
 * ================================================================ */

static audio_fx_api_v2_t g_fx_api_v2;

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;

    memset(&g_fx_api_v2, 0, sizeof(g_fx_api_v2));
    g_fx_api_v2.api_version     = AUDIO_FX_API_VERSION_2;
    g_fx_api_v2.create_instance = v2_create_instance;
    g_fx_api_v2.destroy_instance = v2_destroy_instance;
    g_fx_api_v2.process_block   = v2_process_block;
    g_fx_api_v2.set_param       = v2_set_param;
    g_fx_api_v2.get_param       = v2_get_param;

    jc_log("Junologue Chorus v2 plugin initialized");

    return &g_fx_api_v2;
}
