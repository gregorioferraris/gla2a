#include "gla2a.h" // <--- Assicurati che sia la prima cosa e che il percorso sia corretto
#include <lv2/core/lv2.h>
#include <lv2/log/logger.h>
#include <lv2/log/log.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// A questo punto, NON devi più avere la definizione di GLA2A_PortIndex qui,
// perché viene importata da gla2a.h

// Struct del plugin (Questa è già stata aggiornata nella mia risposta precedente)
typedef struct {
    // ... puntatori ai parametri esistenti
    float* peak_reduction_ptr;
    float* gain_ptr;
    float* meter_ptr;
    float* limiter_ptr;
    float* hi_freq_ptr;
    float* output_rms_ptr;
    float* gain_reduction_meter_ptr;

    float* bypass_ptr;         // NUOVO
    float* ms_mode_active_ptr; // NUOVO

    const float* audio_in_l_ptr;
    const float* audio_in_r_ptr;
    float* audio_out_l_ptr;
    float* audio_out_r_ptr;

    double samplerate;
    LV2_Log_Log* log;
    LV2_Log_Logger logger;

    float current_output_rms;
    float current_gain_reduction;
    float rms_alpha;

} Gla2a;

// ... il resto del codice di gla2a.cpp (funzioni instantiate, connect_port, run, etc.)
// che ti ho fornito in precedenza. Assicurati che i case nello switch di connect_port
// usino i valori dell'enum di GLA2A_PortIndex.

// Il resto del codice di gla2a.cpp rimane come te l'ho dato.
