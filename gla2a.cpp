#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <math.h>
#include <stdlib.h> // Per calloc e free

// URI unico del nostro plugin. E' fondamentale che sia unico a livello mondiale!
// Useremo il tuo tuo nome utente GitHub per renderlo specifico.
#define GLA2A_URI "http://gregorioferraris.github.io/lv2/gla2a"

// Definizione delle porte del plugin.
// Questi indici DEVONO CORRISPONDERE ESATTAMENTE a quelli definiti in gla2a.ttl!
typedef enum {
    GLA2A_CONTROL_GAIN_IN      = 0, // Controllo dell'input gain (per LA-2A: più gain = più compressione)
    GLA2A_CONTROL_GAIN_OUT     = 1, // Controllo dell'output gain (make-up gain)
    GLA2A_CONTROL_PEAK_REDUCTION = 2, // Quantità di riduzione del picco
    GLA2A_CONTROL_MODE         = 3, // Compressore / Limiter
    GLA2A_CONTROL_SIDECHAIN_MODE = 4, // Sidechain interno / esterno
    GLA2A_AUDIO_EXTERNAL_SIDECHAIN_IN = 5, // Ingresso audio per sidechain esterno (mono)
    GLA2A_AUDIO_IN_L           = 6, // Ingresso audio sinistro
    GLA2A_AUDIO_IN_R           = 7, // Ingresso audio destro
    GLA2A_AUDIO_OUT_L          = 8, // Uscita audio sinistro
    GLA2A_AUDIO_OUT_R          = 9  // Uscita audio destro
} PortIndex;

// Struttura che conterrà lo stato di un'istanza del plugin
typedef struct {
    // Puntatori ai buffer delle porte, aggiornati dall'host
    float* gain_in_port;
    float* gain_out_port;
    float* peak_reduction_port;
    float* mode_port;
    float* sidechain_mode_port;
    const float* external_sidechain_in_port; // Ingresso audio per sidechain esterno (mono)
    const float* input_l_port;
    const float* input_r_port;
    float* output_l_port;
    float* output_r_port;

    // Variabili interne del plugin per la logica di compressione
    float sample_rate; // Frequenza di campionamento
    float avg_gain;    // Guadagno medio corrente del compressore (per evitare click)
    float envelope;    // Valore dell'inviluppo del segnale (per determinare la compressione)
    
    // Tempi nominali per l'LA-2A (questi non sono parametri utente)
    // Attack è di circa 10ms, Release è dipendente dal programma:
    // circa 60ms per il 50%, poi 0.5s - 5s per il resto.
    // Useremo costanti per semplificare, l'utente li controlla indirettamente con Peak Reduction.
    // Questi sono valori in secondi, non millisecondi.
    const float attack_time_sec;
    const float release_time_sec; 

    // Coefficienti per il calcolo dell'inviluppo (ricalcati se sample_rate cambia)
    float attack_coeff; 
    float release_coeff;

    // Parametri per il soft knee
    float knee_width_db; // Larghezza del ginocchio in dB
    float soft_clip_threshold; // Soglia per il soft clip (valore lineare)
    float soft_clip_factor; // Fattore di "morbidezza" del soft clip

} Gla2a;

// Funzioni di utilità per conversioni dB <-> lineare
static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static float linear_to_db(float linear) {
    if (linear <= 0.0f) return -90.0f; // Impedisce log(0) e valori negativi
    return 20.0f * log10f(linear);
}

// Funzione per ricalcolare i coefficienti di attacco/rilascio
static void calculate_envelope_coeffs(Gla2a* plugin) {
    // Coefficienti basati su un filtro RC per smoothing
    // coeff = exp(-1.0f / (time_in_seconds * sample_rate))
    // NOTA: Per LA-2A, questi sono "interni" e non regolabili dall'utente
    plugin->attack_coeff = expf(-1.0f / (plugin->attack_time_sec * plugin->sample_rate));
    plugin->release_coeff = expf(-1.0f / (plugin->release_time_sec * plugin->sample_rate));
}

// Funzione di Soft Clip
// Utilizza una funzione sigmoide o arctan per arrotondare i picchi
static float soft_clip(float sample, float threshold, float factor) {
    if (sample > threshold) {
        return threshold + (1.0f / factor) * atanf(factor * (sample - threshold));
    } else if (sample < -threshold) {
        return -threshold + (1.0f / factor) * atanf(factor * (sample + threshold));
    }
    return sample;
}


// --- Funzioni del Plugin LV2 ---

// 1. `instantiate`: Crea una nuova istanza del plugin
static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                              double                    rate,
                              const char* bundle_path,
                              const LV2_Feature* const* features) {
    Gla2a* plugin = (Gla2a*)calloc(1, sizeof(Gla2a));
    plugin->sample_rate = rate; // Salva la frequenza di campionamento
    plugin->avg_gain = 1.0f;    // Inizializza il guadagno medio a 1.0 (0 dB)
    plugin->envelope = 0.0f;    // Inizializza l'inviluppo
    
    // Inizializza i tempi di attack/release nominali per LA-2A style
    ((Gla2a*)plugin)->attack_time_sec = 0.010f; // ~10 ms attack
    ((Gla2a*)plugin)->release_time_sec = 0.060f; // ~60 ms per il 50% di rilascio, il resto è molto più lento

    calculate_envelope_coeffs(plugin); // Calcola i coefficienti iniziali

    // Parametri per soft knee e soft clip
    plugin->knee_width_db = 10.0f; // Larghezza del ginocchio, es. +/- 5 dB dalla soglia
    plugin->soft_clip_threshold = 0.8f; // Inizia a clippare a 0.8 del valore massimo
    plugin->soft_clip_factor = 3.0f; // Quanto è "morbido" il clip (più alto = più morbido)

    return (LV2_Handle)plugin;
}

// 2. `connect_port`: Connette una porta del plugin a un buffer di memoria
static void connect_port(LV2_Handle instance, uint32_t port, void* data_location) {
    Gla2a* plugin = (Gla2a*)instance;

    switch ((PortIndex)port) {
        case GLA2A_CONTROL_GAIN_IN:
            plugin->gain_in_port = (float*)data_location;
            break;
        case GLA2A_CONTROL_GAIN_OUT:
            plugin->gain_out_port = (float*)data_location;
            break;
        case GLA2A_CONTROL_PEAK_REDUCTION:
            plugin->peak_reduction_port = (float*)data_location;
            break;
        case GLA2A_CONTROL_MODE:
            plugin->mode_port = (float*)data_location;
            break;
        case GLA2A_CONTROL_SIDECHAIN_MODE:
            plugin->sidechain_mode_port = (float*)data_location;
            break;
        case GLA2A_AUDIO_EXTERNAL_SIDECHAIN_IN:
            plugin->external_sidechain_in_port = (const float*)data_location;
            break;
        case GLA2A_AUDIO_IN_L:
            plugin->input_l_port = (const float*)data_location;
            break;
        case GLA2A_AUDIO_IN_R:
            plugin->input_r_port = (const float*)data_location;
            break;
        case GLA2A_AUDIO_OUT_L:
            plugin->output_l_port = (float*)data_location;
            break;
        case GLA2A_AUDIO_OUT_R:
            plugin->output_r_port = (float*)data_location;
            break;
    }
}

// 3. `activate`: Chiamata prima del processamento audio
static void activate(LV2_Handle instance) {
    // Re-inizializza lo stato rilevante all'attivazione
    Gla2a* plugin = (Gla2a*)instance;
    plugin->avg_gain = 1.0f; // Reset del guadagno medio
    plugin->envelope = 0.0f; // Reset dell'inviluppo
}

// 4. `run`: La funzione principale di elaborazione audio
static void run(LV2_Handle instance, uint32_t n_samples) {
    Gla2a* plugin = (Gla2a*)instance;

    // Ottieni i puntatori ai buffer delle porte
    const float* const input_l          = plugin->input_l_port;
    const float* const input_r          = plugin->input_r_port;
    float* const       output_l         = plugin->output_l_port;
    float* const       output_r         = plugin->output_r_port;

    const float* const external_sidechain_in = plugin->external_sidechain_in_port;

    // Ottieni i valori attuali dei controlli (sono puntatori, quindi dereferenziali)
    float input_gain_param_db = *plugin->gain_in_port;
    float output_gain_param_db = *plugin->gain_out_port;
    float peak_reduction_param = *plugin->peak_reduction_port; // Valore 0-100
    int   mode                 = (int)*plugin->mode_port; // 0=Compress, 1=Limit
    int   sidechain_mode       = (int)*plugin->sidechain_mode_port; // 0=Internal, 1=External

    // Converti i guadagni da dB a lineare
    float input_gain_linear = db_to_linear(input_gain_param_db);
    float output_gain_linear = db_to_linear(output_gain_param_db);

    // Mappa Peak Reduction a una "soglia" implicita
    // La "Threshold" implicita qui è controllata da `peak_reduction_param`.
    // Ad esempio: peak_reduction=0 -> soglia alta (es. +20dB, no compressione)
    //             peak_reduction=100 -> soglia bassa (es. -40dB, molta compressione)
    // Questa è un'interpretazione, l'LA-2A non ha una soglia esplicita, ma risponde
    // al livello di input controllato da Peak Reduction.
    float base_implicit_threshold_db = 20.0f - (peak_reduction_param * 0.6f); // Es. da +20dB a -40dB

    // Calcola i coefficienti di smoothing per l'inviluppo
    float attack_coeff = plugin->attack_coeff;
    float release_coeff = plugin->release_coeff;

    for (uint32_t i = 0; i < n_samples; ++i) {
        float current_input_l = input_l[i];
        float current_input_r = input_r[i];

        // 1. Ottieni il segnale di sidechain (input per la rilevazione del guadagno)
        float sidechain_sample;
        if (sidechain_mode == 0) { // Internal Sidechain
            // Prende il valore assoluto massimo tra L e R per il sidechain interno
            sidechain_sample = fmaxf(fabsf(current_input_l * input_gain_linear), fabsf(current_input_r * input_gain_linear));
        } else { // External Sidechain
            if (external_sidechain_in) { // Assicurati che la porta sia connessa
                sidechain_sample = fabsf(external_sidechain_in[i]);
            } else {
                sidechain_sample = 0.0f; // Nessun segnale di sidechain se la porta non è connessa
            }
        }

        // 2. Rilevamento dell'inviluppo (Peak Detection con Smoothing)
        // Aggiorna l'inviluppo usando i coefficienti di attacco/rilascio
        if (sidechain_sample > plugin->envelope) {
            plugin->envelope = (sidechain_sample * (1.0f - attack_coeff)) + (plugin->envelope * attack_coeff);
        } else {
            plugin->envelope = (sidechain_sample * (1.0f - release_coeff)) + (plugin->envelope * release_coeff);
        }
        if (plugin->envelope < 0.0f) plugin->envelope = 0.0f; // Assicurati che non sia mai negativo


        // 3. Calcolo del Guadagno di Riduzione (Gain Reduction) con Soft Knee
        float current_gain_reduction_db = 0.0f;
        float envelope_db = linear_to_db(plugin->envelope);

        // Calcola la soglia effettiva per il soft knee
        float threshold_lower = base_implicit_threshold_db - (plugin->knee_width_db / 2.0f);
        float threshold_upper = base_implicit_threshold_db + (plugin->knee_width_db / 2.0f);

        if (envelope_db > threshold_upper) {
            // Fuori dal ginocchio, sopra la soglia: compressione lineare
            float overshoot_db = envelope_db - base_implicit_threshold_db;
            float ratio_val = (mode == 0) ? 4.0f : 20.0f; // Esempio: 4:1 compress, 20:1 limit
            current_gain_reduction_db = - (overshoot_db / ratio_val);
        } else if (envelope_db > threshold_lower) {
            // All'interno del ginocchio: compressione graduale (soft knee)
            float x = (envelope_db - threshold_lower) / plugin->knee_width_db; // 0 a 1 all'interno del ginocchio
            float ratio_start = 1.0f; // 1:1 all'inizio del ginocchio
            float ratio_end = (mode == 0) ? 4.0f : 20.0f; // Ratio finale nel ginocchio

            float current_ratio_in_knee = ratio_start + (ratio_end - ratio_start) * x; // Interpola il ratio
            float effective_overshoot_db = envelope_db - threshold_lower; // Quanto siamo sopra l'inizio del ginocchio
            current_gain_reduction_db = - (effective_overshoot_db / current_ratio_in_knee);
            // Questo modello di soft knee è semplificato, la matematica reale è più complessa.
            // L'LA-2A ha un ginocchio molto dolce, spesso modellato con curve esponenziali/logaritmiche.
        }
        // Se envelope_db <= threshold_lower, current_gain_reduction_db rimane 0.0f

        // Converti la riduzione del guadagno da dB a lineare
        float target_gain_linear = db_to_linear(current_gain_reduction_db);

        // 4. Smoothing del Guadagno (Gain Smoothing)
        // Interpoliamo il guadagno applicato per evitare click.
        // Utilizza gli stessi coefficienti di attacco/rilascio dell'inviluppo.
        if (target_gain_linear < plugin->avg_gain) { // Se il guadagno deve scendere (compressione)
            plugin->avg_gain = (target_gain_linear * (1.0f - attack_coeff)) + (plugin->avg_gain * attack_coeff);
        } else { // Se il guadagno deve salire (rilascio)
            plugin->avg_gain = (target_gain_linear * (1.0f - release_coeff)) + (plugin->avg_gain * release_coeff);
        }
        
        // Clamping del guadagno per stabilità
        if (plugin->avg_gain > 1.0f) plugin->avg_gain = 1.0f;
        if (plugin->avg_gain < 0.0001f) plugin->avg_gain = 0.0001f;

        // 5. Applica il guadagno di compressione e il make-up gain
        // Il guadagno di input_gain_linear è applicato all'inizio della catena.
        // output_gain_linear è il make-up gain alla fine.
        float processed_l = current_input_l * input_gain_linear * plugin->avg_gain * output_gain_linear;
        float processed_r = current_input_r * input_gain_linear * plugin->avg_gain * output_gain_linear;

        // 6. Applica il Soft Clip
        // Questo simula la saturazione delle valvole.
        output_l[i] = soft_clip(processed_l, plugin->soft_clip_threshold, plugin->soft_clip_factor);
        output_r[i] = soft_clip(processed_r, plugin->soft_clip_threshold, plugin->soft_clip_factor);
    }
}

// 5. `deactivate`: Chiamata dopo il processamento audio (non usiamo qui per ora)
static void deactivate(LV2_Handle instance) {
    // Nulla da fare
}

// 6. `cleanup`: Libera le risorse allocate dall'istanza del plugin
static void cleanup(LV2_Handle instance) {
    free(instance);
}

// 7. `extension_data`: Per funzionalità avanzate (non usiamo qui)
static const void* extension_data(const char* uri) {
    return NULL;
}

// Struttura LV2_Descriptor che descrive il nostro plugin
static const LV2_Descriptor gla2aDescriptor = {
    GLA2A_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

// La funzione exports: il punto di ingresso per gli host LV2
LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    switch (index) {
        case 0:
            return &gla2aDescriptor;
        default:
            return NULL;
    }
}