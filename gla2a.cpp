#include "gla2a.h"
#include <lv2/core/lv2.h>
#include <lv2/log/logger.h>
#include <lv2/log/log.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// --- Costanti e Definizioni ---
#define M_PI_F 3.14159265358979323846f

// --- Calibrazione del Compressore ---
#define PEAK_REDUCTION_MIN_DB -60.0f // Soglia min per Peak Reduction (più compressione)
#define PEAK_REDUCTION_MAX_DB -10.0f // Soglia max per Peak Reduction (meno compressione)
#define GAIN_MAX_DB 12.0f            // Guadagno massimo applicabile in dB
#define KNEE_WIDTH_DB 10.0f          // Larghezza della soft-knee in dB

// Tempi di smoothing per il detector e il gain (questi cambieranno con la ratio mode)
#define DETECTOR_ATTACK_FACTOR_BASE 0.02f // Base per tempo attacco (più lento per valvola)
#define DETECTOR_RELEASE_FACTOR_BASE 0.5f  // Base per tempo rilascio (più lento per valvola)

// --- Valvola Distortion Parameters ---
// Questi sono parametri per una funzione di shaping che simula una valvola.
// Possono essere calibrati a orecchio.
#define VALVE_SATURATION_DRIVE 0.7f // Quanto spingere la valvola (0.0 a 1.0)
#define VALVE_SATURATION_MIX   0.7f // Mix tra segnale distorto e pulito (0.0 a 1.0)
#define VALVE_SATURATION_THRESHOLD 0.7f // Soglia a cui la distorsione valvolare è più evidente

// --- Soft-Clipping Finale (Limiter) ---
#define FINAL_SOFT_CLIP_THRESHOLD_DB -1.0f // Inizia il soft-clip finale a -1 dBFS
#define FINAL_SOFT_CLIP_AMOUNT 0.5f        // Quanto è "soft" il clip finale (0.0 a 1.0, 1.0 è hard clip)

// --- RMS Meter Smoothing ---
#define RMS_METER_SMOOTH_MS 50.0f // Tempo in ms per la costante di tempo RMS del meter

// --- OVERSEMPLING/UPSAMPLING ---
#define UPSAMPLE_FACTOR 4 // Fattore di oversampling (2x, 4x, 8x, etc.)
// Useremo 3 filtri biquad in cascata per l'upsampling e il downsampling,
// per ottenere un filtro passa-basso di 6° ordine (36 dB/ottava).
#define NUM_BIQUADS_FOR_OS_FILTER 3 
#define OS_FILTER_Q 0.707f // Q di Butterworth per risposta piatta (o calibra per più risonanza)

// --- FILTRI BIQUAD PER SIDECHAIN (6° ORDINE = 3 BIQUAD IN CASCATA) ---
#define NUM_BIQUADS_FOR_6TH_ORDER 3 // Ogni biquad è 2° ordine (12 dB/ottava)


// --- Funzioni di Utilità Generali ---

static float to_db(float linear_val) {
    if (linear_val <= 0.00000000001f) return -90.0f;
    return 20.0f * log10f(linear_val);
}

static float db_to_linear(float db_val) {
    return powf(10.0f, db_val / 20.0f);
}

// Funzione per applicare il soft-clipping finale (stadio di output)
static float apply_final_soft_clip(float sample, float threshold_linear, float amount) {
    float sign = (sample >= 0) ? 1.0f : -1.0f;
    float abs_sample = fabsf(sample);

    if (abs_sample <= threshold_linear) {
        return sample;
    } else {
        float normalized_over_threshold = (abs_sample - threshold_linear) / (1.0f - threshold_linear);
        float clipped_val = threshold_linear + (1.0f - threshold_linear) * (1.0f - expf(-amount * normalized_over_threshold));
        return sign * fminf(clipped_val, 1.0f); // Clampa a 1.0f per sicurezza
    }
}

// Funzione di saturazione/distorsione valvolare (soft-clipping intrinseco)
// Modelled after an arctangent or tanh-like curve, typical of tube saturation.
// 'drive' controls how much the signal pushes the tube.
static float apply_valve_saturation(float sample, float drive_factor, float saturation_threshold) {
    // Normalizza il sample in base alla soglia
    float x = sample * drive_factor / saturation_threshold;
    // Applica una funzione di shaping (tipo arctan o tanh)
    float distorted_x = atanf(x * M_PI_F * 0.5f) / (M_PI_F * 0.5f); // Scala per mantenere il range
    // Ri-scala al range originale
    return distorted_x * saturation_threshold;
}

// Funzione per calcolare l'RMS per il meter di output
static float calculate_rms_level(const float* buffer, uint32_t n_samples, float current_rms, float alpha) {
    if (n_samples == 0) return current_rms;
    float sum_sq = 0.0f;
    for (uint32_t i = 0; i < n_samples; ++i) {
        sum_sq += buffer[i] * buffer[i];
    }
    float block_rms_linear = sqrtf(sum_sq / n_samples);
    return (current_rms * (1.0f - alpha)) + (block_rms_linear * alpha);
}


// --- Strutture e Funzioni per Filtri Biquad ---

typedef struct {
    float a0, a1, a2, b0, b1, b2; // Coefficienti
    float z1, z2;                 // Stati precedenti
} BiquadFilter;

static void biquad_init(BiquadFilter* f) {
    f->a0 = f->a1 = f->a2 = f->b0 = f->b1 = f->b2 = 0.0f;
    f->z1 = f->z2 = 0.0f;
}

static float biquad_process(BiquadFilter* f, float in) {
    float out = in * f->b0 + f->z1;
    f->z1 = in * f->b1 + f->z2 - f->a1 * out;
    f->z2 = in * f->b2 - f->a2 * out;
    return out;
}

// Calcola i coefficienti per un filtro biquad (Low Pass o High Pass)
// freq_hz: frequenza di taglio
// q_val: fattore di qualità (risonanza)
// type: 0 per Low Pass, 1 per High Pass
static void calculate_biquad_coeffs(BiquadFilter* f, double samplerate, float freq_hz, float q_val, int type) {
    if (freq_hz <= 0.0f) freq_hz = 1.0f; // Evita divisione per zero o log(0)
    if (q_val <= 0.0f) q_val = 0.1f;    // Evita divisione per zero o Q troppo basso

    float omega = 2.0f * M_PI_F * freq_hz / samplerate;
    float sin_omega = sinf(omega);
    float cos_omega = cosf(omega);
    float alpha = sin_omega / (2.0f * q_val); // Q del filtro

    float b0, b1, b2, a0, a1, a2;

    if (type == 0) { // Low Pass Filter
        b0 = (1.0f - cos_omega) / 2.0f;
        b1 = 1.0f - cos_omega;
        b2 = (1.0f - cos_omega) / 2.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cos_omega;
        a2 = 1.0f - alpha;
    } else { // High Pass Filter
        b0 = (1.0f + cos_omega) / 2.0f;
        b1 = -(1.0f + cos_omega);
        b2 = (1.0f + cos_omega) / 2.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cos_omega;
        a2 = 1.0f - alpha;
    }

    // Normalizza i coefficienti per a0
    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
    f->a0 = 1.0f; // Questo non viene usato nel process, è solo per chiarezza
}


// Struct del plugin
typedef struct {
    // Puntatori ai parametri di controllo
    float* peak_reduction_ptr;
    float* gain_ptr;
    float* meter_ptr;
    float* bypass_ptr;
    float* ms_mode_active_ptr;
    float* ratio_mode_ptr;

    float* sc_lp_on_ptr;
    float* sc_lp_freq_ptr;
    float* sc_lp_q_ptr;
    float* sc_hp_on_ptr;
    float* sc_hp_freq_ptr;
    float* sc_hp_q_ptr;

    // Puntatori per i meter (output del plugin, input per la GUI)
    float* output_rms_ptr;
    float* gain_reduction_meter_ptr;

    // Puntatori ai buffer audio
    const float* audio_in_l_ptr;
    const float* audio_in_r_ptr;
    float* audio_out_l_ptr;
    float* audio_out_r_ptr;

    // Variabili di stato del plugin
    double samplerate;
    double oversampled_samplerate;
    LV2_Log_Log* log;
    LV2_Log_Logger logger;

    // Variabili di stato per l'algoritmo di compressione
    float detector_envelope_M; // Envelope del detector per Mid/Left
    float detector_envelope_S; // Envelope del detector per Side/Right

    float current_gain_M;      // Guadagno attuale per Mid/Left (lineare)
    float current_gain_S;      // Guadagno attuale per Side/Right (lineare)

    // Filtri Sidechain (6° Ordine)
    BiquadFilter sc_lp_filters_M[NUM_BIQUADS_FOR_6TH_ORDER];
    BiquadFilter sc_hp_filters_M[NUM_BIQUADS_FOR_6TH_ORDER];
    BiquadFilter sc_lp_filters_S[NUM_BIQUADS_FOR_6TH_ORDER];
    BiquadFilter sc_hp_filters_S[NUM_BIQUADS_FOR_6TH_ORDER];

    // Parametri di smoothing (alpha) pre-calcolati (variano con la ratio mode)
    float detector_attack_alpha;
    float detector_release_alpha;
    float gain_smooth_alpha; // Smoothing molto veloce per il guadagno applicato
    float rms_meter_alpha;   // Smoothing per il meter RMS di output

    // Cache per i parametri dei filtri (per evitare ricalcoli inutili)
    float last_sc_lp_freq;
    float last_sc_lp_q;
    float last_sc_hp_freq;
    float last_sc_hp_q;

    // Buffer per oversampling (per blocco di input completo)
    float* oversample_buffer_M;
    float* oversample_buffer_S;
    uint32_t
