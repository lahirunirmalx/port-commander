/**
 * 24-bit ADC protocol layer — 8-channel, thread-safe.
 * 
 * Serial format: CH1+12345678CH2-00012345...CH8+xxxxxxxx\n
 * Each channel: "CHn" + sign(+/-) + 8 digits
 * 8½ digit display = sign + 8 digits (±99,999,999)
 */

#ifndef ADC_PROTOCOL_H
#define ADC_PROTOCOL_H

#include "serial_port.h"
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#define ADC_MAX_CHANNELS  8
#define ADC_TRACE_LEN     200

typedef enum {
    ADC_SCALE_UV = 0,   /* microvolts */
    ADC_SCALE_MV,       /* millivolts */
    ADC_SCALE_V         /* volts */
} adc_scale_t;

/**
 * Per-channel calibration parameters.
 */
typedef struct {
    double offset;      /* Offset in raw counts (subtracted from raw) */
    double gain;        /* Gain multiplier (applied after offset) */
    double vref;        /* Reference voltage in µV (full-scale) */
    bool enabled;       /* Channel enabled for display */
    char label[16];     /* User-defined label */
} adc_cal_t;

/**
 * Per-channel statistics.
 */
typedef struct {
    int32_t min_raw;
    int32_t max_raw;
    int64_t sum_raw;
    uint32_t sample_count;
    double min_uv;
    double max_uv;
    double avg_uv;
} adc_stats_t;

/**
 * Per-channel trace history for oscilloscope display.
 */
typedef struct {
    double values[ADC_TRACE_LEN];  /* Scaled values in µV */
    int32_t raw[ADC_TRACE_LEN];    /* Raw ADC counts */
    int head;
    int count;
} adc_trace_t;

/**
 * Single channel status.
 */
typedef struct {
    int32_t raw;            /* Raw ADC count (signed 24-bit, but 8 digits = up to ±99,999,999) */
    double scaled_uv;       /* Scaled value in microvolts */
    bool valid;             /* Data received at least once */
    uint64_t timestamp_ms;  /* Last update time */
    
    adc_cal_t cal;          /* Calibration parameters */
    adc_stats_t stats;      /* Running statistics */
    adc_trace_t trace;      /* Oscilloscope trace */
} adc_channel_t;

/**
 * ADC context for thread-safe operations.
 */
typedef struct {
    serial_port_t *serial;
    adc_channel_t ch[ADC_MAX_CHANNELS];
    
    pthread_mutex_t lock;
    pthread_t reader_thread;
    
    volatile bool running;
    volatile bool connected;
    volatile uint32_t rx_count;
    volatile uint32_t err_count;
    volatile uint32_t parse_errors;
    
    /* Global settings */
    adc_scale_t display_scale;  /* Current display scale */
    bool stats_running;         /* Statistics accumulation enabled */
} adc_context_t;

/**
 * Initialize ADC context and start reader thread.
 * Sets default calibration: offset=0, gain=1.0, vref=10,000,000 µV (10V)
 */
bool adc_init(adc_context_t *ctx, const char *device, int baud);

/**
 * Shutdown ADC context and stop reader thread.
 */
void adc_shutdown(adc_context_t *ctx);

/**
 * Get channel data (thread-safe copy).
 * @param ch 0-7
 */
void adc_get_channel(adc_context_t *ctx, int ch, adc_channel_t *out);

/**
 * Check if connected to serial device.
 */
bool adc_is_connected(adc_context_t *ctx);

/**
 * Set calibration for a channel.
 * @param ch 0-7
 */
void adc_set_calibration(adc_context_t *ctx, int ch, double offset, double gain, double vref_uv);

/**
 * Get calibration for a channel.
 */
void adc_get_calibration(adc_context_t *ctx, int ch, adc_cal_t *out);

/**
 * Enable/disable a channel.
 */
void adc_set_channel_enabled(adc_context_t *ctx, int ch, bool enabled);

/**
 * Set channel label.
 */
void adc_set_channel_label(adc_context_t *ctx, int ch, const char *label);

/**
 * Reset statistics for a channel (or all if ch < 0).
 */
void adc_reset_stats(adc_context_t *ctx, int ch);

/**
 * Set display scale (µV, mV, V).
 */
void adc_set_scale(adc_context_t *ctx, adc_scale_t scale);

/**
 * Get display scale.
 */
adc_scale_t adc_get_scale(adc_context_t *ctx);

/**
 * Start/stop statistics accumulation.
 */
void adc_set_stats_running(adc_context_t *ctx, bool running);

/**
 * Get communication statistics.
 */
void adc_get_stats(adc_context_t *ctx, uint32_t *rx_count, uint32_t *err_count, uint32_t *parse_errors);

/**
 * Convert raw ADC count to scaled value using calibration.
 * Returns value in microvolts.
 */
double adc_raw_to_uv(const adc_cal_t *cal, int32_t raw);

/**
 * Format a value for display with proper unit suffix.
 * @param buf output buffer (at least 24 chars)
 * @param uv value in microvolts
 * @param scale desired display scale
 */
void adc_format_value(char *buf, size_t buflen, double uv, adc_scale_t scale);

/**
 * Format raw ADC count with sign and grouping.
 * @param buf output buffer (at least 16 chars)
 * @param raw raw ADC count
 */
void adc_format_raw(char *buf, size_t buflen, int32_t raw);

#endif /* ADC_PROTOCOL_H */
