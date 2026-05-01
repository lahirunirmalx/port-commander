/**
 * 24-bit ADC protocol implementation.
 * 
 * Parses: CH1+12345678CH2-00012345...CH8+xxxxxxxx\n
 * Thread-safe with background reader.
 */

#define _DEFAULT_SOURCE
#include "adc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>

static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

double adc_raw_to_uv(const adc_cal_t *cal, int32_t raw) {
    if (!cal) return 0.0;
    
    /* Apply calibration: ((raw - offset) * gain) * (vref / 2^23) */
    /* 24-bit ADC: full scale = 2^23 for bipolar (±8,388,608) */
    double corrected = ((double)raw - cal->offset) * cal->gain;
    
    /* Scale to microvolts using reference */
    /* vref is full-scale in µV, 2^23 = 8388608 for 24-bit bipolar */
    return corrected * (cal->vref / 8388608.0);
}

void adc_format_value(char *buf, size_t buflen, double uv, adc_scale_t scale) {
    if (!buf || buflen < 2) return;
    
    double val;
    const char *unit;
    int decimals;
    
    switch (scale) {
        case ADC_SCALE_UV:
            val = uv;
            unit = "µV";
            decimals = 2;
            break;
        case ADC_SCALE_MV:
            val = uv / 1000.0;
            unit = "mV";
            decimals = 5;
            break;
        case ADC_SCALE_V:
        default:
            val = uv / 1000000.0;
            unit = "V";
            decimals = 8;
            break;
    }
    
    snprintf(buf, buflen, "%+.*f %s", decimals, val, unit);
}

void adc_format_raw(char *buf, size_t buflen, int32_t raw) {
    if (!buf || buflen < 2) return;
    
    /* Format with sign and digit grouping: +12,345,678 */
    char sign = (raw >= 0) ? '+' : '-';
    uint32_t abs_val = (raw >= 0) ? (uint32_t)raw : (uint32_t)(-raw);
    
    /* Break into groups */
    int d1 = abs_val % 1000;
    int d2 = (abs_val / 1000) % 1000;
    int d3 = (abs_val / 1000000) % 1000;
    
    if (abs_val >= 1000000) {
        snprintf(buf, buflen, "%c%d,%03d,%03d", sign, d3, d2, d1);
    } else if (abs_val >= 1000) {
        snprintf(buf, buflen, "%c%d,%03d", sign, d2, d1);
    } else {
        snprintf(buf, buflen, "%c%d", sign, d1);
    }
}

/**
 * Parse a single channel from the serial string.
 * Format: CHn+12345678 or CHn-00012345
 * Returns pointer to next character after parsed data, or NULL on error.
 */
static const char *parse_channel(const char *str, int *ch_num, int32_t *value) {
    if (!str || !ch_num || !value) return NULL;
    
    /* Expect "CH" */
    if (str[0] != 'C' || str[1] != 'H') return NULL;
    str += 2;
    
    /* Channel number 1-8 */
    if (*str < '1' || *str > '8') return NULL;
    *ch_num = *str - '0';
    str++;
    
    /* Sign */
    int sign = 1;
    if (*str == '+') {
        sign = 1;
        str++;
    } else if (*str == '-') {
        sign = -1;
        str++;
    } else {
        return NULL;
    }
    
    /* 8 digits */
    int64_t v = 0;
    for (int i = 0; i < 8; i++) {
        if (!isdigit((unsigned char)str[i])) return NULL;
        v = v * 10 + (str[i] - '0');
    }
    str += 8;
    
    *value = (int32_t)(v * sign);
    return str;
}

/**
 * Parse a complete line of ADC data.
 * Format: CH1+12345678CH2-00012345...
 */
static int parse_adc_line(const char *line, int32_t *values, bool *present) {
    if (!line || !values || !present) return 0;
    
    /* Clear presence flags */
    for (int i = 0; i < ADC_MAX_CHANNELS; i++) {
        present[i] = false;
        values[i] = 0;
    }
    
    const char *p = line;
    int parsed = 0;
    
    /* Skip leading whitespace */
    while (*p && isspace((unsigned char)*p)) p++;
    
    while (*p) {
        int ch_num;
        int32_t val;
        
        const char *next = parse_channel(p, &ch_num, &val);
        if (!next) {
            /* Skip unknown characters */
            p++;
            continue;
        }
        
        if (ch_num >= 1 && ch_num <= ADC_MAX_CHANNELS) {
            values[ch_num - 1] = val;
            present[ch_num - 1] = true;
            parsed++;
        }
        
        p = next;
    }
    
    return parsed;
}

/**
 * Update channel with new raw value.
 */
static void update_channel(adc_channel_t *ch, int32_t raw, uint64_t now_ms, bool stats_running) {
    ch->raw = raw;
    ch->scaled_uv = adc_raw_to_uv(&ch->cal, raw);
    ch->valid = true;
    ch->timestamp_ms = now_ms;
    
    /* Update trace */
    ch->trace.raw[ch->trace.head] = raw;
    ch->trace.values[ch->trace.head] = ch->scaled_uv;
    ch->trace.head = (ch->trace.head + 1) % ADC_TRACE_LEN;
    if (ch->trace.count < ADC_TRACE_LEN) ch->trace.count++;
    
    /* Update statistics if enabled */
    if (stats_running) {
        if (ch->stats.sample_count == 0) {
            ch->stats.min_raw = raw;
            ch->stats.max_raw = raw;
            ch->stats.min_uv = ch->scaled_uv;
            ch->stats.max_uv = ch->scaled_uv;
        } else {
            if (raw < ch->stats.min_raw) {
                ch->stats.min_raw = raw;
                ch->stats.min_uv = ch->scaled_uv;
            }
            if (raw > ch->stats.max_raw) {
                ch->stats.max_raw = raw;
                ch->stats.max_uv = ch->scaled_uv;
            }
        }
        ch->stats.sum_raw += raw;
        ch->stats.sample_count++;
        
        /* Compute average */
        if (ch->stats.sample_count > 0) {
            double avg_raw = (double)ch->stats.sum_raw / ch->stats.sample_count;
            ch->stats.avg_uv = adc_raw_to_uv(&ch->cal, (int32_t)avg_raw);
        }
    }
}

/**
 * Reader thread - continuously reads and parses serial data.
 */
static void *reader_thread(void *arg) {
    adc_context_t *ctx = (adc_context_t *)arg;
    char line[256];
    
    while (ctx->running) {
        if (!serial_is_open(ctx->serial)) {
            ctx->connected = false;
            usleep(100000); /* 100ms */
            continue;
        }
        
        if (serial_read_line(ctx->serial, line, sizeof(line), 50)) {
            ctx->rx_count++;
            ctx->connected = true;
            
            int32_t values[ADC_MAX_CHANNELS];
            bool present[ADC_MAX_CHANNELS];
            
            int parsed = parse_adc_line(line, values, present);
            
            if (parsed > 0) {
                uint64_t now = get_time_ms();
                
                pthread_mutex_lock(&ctx->lock);
                
                for (int i = 0; i < ADC_MAX_CHANNELS; i++) {
                    if (present[i]) {
                        update_channel(&ctx->ch[i], values[i], now, ctx->stats_running);
                    }
                }
                
                pthread_mutex_unlock(&ctx->lock);
            } else {
                ctx->parse_errors++;
            }
        }
    }
    
    return NULL;
}

bool adc_init(adc_context_t *ctx, const char *device, int baud) {
    if (!ctx) return false;
    
    memset(ctx, 0, sizeof(*ctx));
    pthread_mutex_init(&ctx->lock, NULL);
    
    /* Initialize default calibration for all channels */
    for (int i = 0; i < ADC_MAX_CHANNELS; i++) {
        ctx->ch[i].cal.offset = 0.0;
        ctx->ch[i].cal.gain = 1.0;
        ctx->ch[i].cal.vref = 10000000.0;  /* 10V = 10,000,000 µV */
        ctx->ch[i].cal.enabled = true;
        snprintf(ctx->ch[i].cal.label, sizeof(ctx->ch[i].cal.label), "CH%d", i + 1);
    }
    
    ctx->display_scale = ADC_SCALE_MV;
    ctx->stats_running = true;
    
    /* Open serial port */
    ctx->serial = serial_open(device, baud);
    if (!ctx->serial) {
        return false;
    }
    
    /* Start reader thread */
    ctx->running = true;
    if (pthread_create(&ctx->reader_thread, NULL, reader_thread, ctx) != 0) {
        serial_close(ctx->serial);
        ctx->serial = NULL;
        return false;
    }
    
    return true;
}

void adc_shutdown(adc_context_t *ctx) {
    if (!ctx) return;
    
    ctx->running = false;
    
    if (ctx->reader_thread) {
        pthread_join(ctx->reader_thread, NULL);
    }
    
    if (ctx->serial) {
        serial_close(ctx->serial);
        ctx->serial = NULL;
    }
    
    pthread_mutex_destroy(&ctx->lock);
}

void adc_get_channel(adc_context_t *ctx, int ch, adc_channel_t *out) {
    if (!ctx || !out || ch < 0 || ch >= ADC_MAX_CHANNELS) return;
    
    pthread_mutex_lock(&ctx->lock);
    *out = ctx->ch[ch];
    pthread_mutex_unlock(&ctx->lock);
}

bool adc_is_connected(adc_context_t *ctx) {
    return ctx && ctx->connected;
}

void adc_set_calibration(adc_context_t *ctx, int ch, double offset, double gain, double vref_uv) {
    if (!ctx || ch < 0 || ch >= ADC_MAX_CHANNELS) return;
    
    pthread_mutex_lock(&ctx->lock);
    ctx->ch[ch].cal.offset = offset;
    ctx->ch[ch].cal.gain = gain;
    ctx->ch[ch].cal.vref = vref_uv;
    
    /* Recalculate scaled value with new calibration */
    if (ctx->ch[ch].valid) {
        ctx->ch[ch].scaled_uv = adc_raw_to_uv(&ctx->ch[ch].cal, ctx->ch[ch].raw);
    }
    pthread_mutex_unlock(&ctx->lock);
}

void adc_get_calibration(adc_context_t *ctx, int ch, adc_cal_t *out) {
    if (!ctx || !out || ch < 0 || ch >= ADC_MAX_CHANNELS) return;
    
    pthread_mutex_lock(&ctx->lock);
    *out = ctx->ch[ch].cal;
    pthread_mutex_unlock(&ctx->lock);
}

void adc_set_channel_enabled(adc_context_t *ctx, int ch, bool enabled) {
    if (!ctx || ch < 0 || ch >= ADC_MAX_CHANNELS) return;
    
    pthread_mutex_lock(&ctx->lock);
    ctx->ch[ch].cal.enabled = enabled;
    pthread_mutex_unlock(&ctx->lock);
}

void adc_set_channel_label(adc_context_t *ctx, int ch, const char *label) {
    if (!ctx || !label || ch < 0 || ch >= ADC_MAX_CHANNELS) return;
    
    pthread_mutex_lock(&ctx->lock);
    strncpy(ctx->ch[ch].cal.label, label, sizeof(ctx->ch[ch].cal.label) - 1);
    ctx->ch[ch].cal.label[sizeof(ctx->ch[ch].cal.label) - 1] = '\0';
    pthread_mutex_unlock(&ctx->lock);
}

void adc_reset_stats(adc_context_t *ctx, int ch) {
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->lock);
    
    if (ch < 0) {
        /* Reset all channels */
        for (int i = 0; i < ADC_MAX_CHANNELS; i++) {
            memset(&ctx->ch[i].stats, 0, sizeof(adc_stats_t));
        }
    } else if (ch < ADC_MAX_CHANNELS) {
        memset(&ctx->ch[ch].stats, 0, sizeof(adc_stats_t));
    }
    
    pthread_mutex_unlock(&ctx->lock);
}

void adc_set_scale(adc_context_t *ctx, adc_scale_t scale) {
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->lock);
    ctx->display_scale = scale;
    pthread_mutex_unlock(&ctx->lock);
}

adc_scale_t adc_get_scale(adc_context_t *ctx) {
    if (!ctx) return ADC_SCALE_MV;
    
    pthread_mutex_lock(&ctx->lock);
    adc_scale_t s = ctx->display_scale;
    pthread_mutex_unlock(&ctx->lock);
    
    return s;
}

void adc_set_stats_running(adc_context_t *ctx, bool running) {
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->lock);
    ctx->stats_running = running;
    pthread_mutex_unlock(&ctx->lock);
}

void adc_get_stats(adc_context_t *ctx, uint32_t *rx_count, uint32_t *err_count, uint32_t *parse_errors) {
    if (!ctx) return;
    
    if (rx_count) *rx_count = ctx->rx_count;
    if (err_count) *err_count = ctx->err_count;
    if (parse_errors) *parse_errors = ctx->parse_errors;
}
