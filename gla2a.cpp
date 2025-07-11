#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <lv2/urid/urid.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/atom-forge.h>
#include <lv2/port-groups/port-groups.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// URI unico del tuo plugin
#define GLA2A_URI "http://gregorioferraris.github.io/lv2/gla2a"

// Definizione delle porte del plugin.
// Questi indici DEVONO CORRISPONDERE ESATTAMENTE a quelli definiti in gla2a.ttl!
typedef enum {
    GLA2A_CONTROL_GAIN_IN      = 0,
    GLA2A_CONTROL_GAIN_OUT     = 1,
    GLA2A_CONTROL_PEAK_REDUCTION = 2,
    GLA2A_CONTROL_MODE         = 3,
    GLA2A_CONTROL_SIDECHAIN_MODE = 4,
    GLA2A_AUDIO_EXTERNAL_SIDECHAIN_IN = 5,
    GLA2A_CONTROL_RATIO_SELECT = 6,
    GLA2A_CONTROL_AR_PRESET    = 7,
    GLA2A_CONTROL_TUBE_DRIVE   = 8,
    GLA2A_AUDIO_IN_L           = 9,
    GLA2A_AUDIO_IN_R           = 10,
    GLA2A_AUDIO_OUT_L          = 11,
    GLA2A_AUDIO_OUT_R          = 12,
    GLA2A_CONTROL_OVERSAMPLING_MODE = 13,
    GLA2A_CONTROL_SIDECHAIN_HPF_FREQ = 14,
    GLA2A_CONTROL_SIDECHAIN_LPF_FREQ = 15,
    GLA2A_CONTROL_SIDECHAIN_FILTER_Q = 16,
    GLA2A_CONTROL_SIDECHAIN_MONITOR_MODE = 17,
    GLA2A_CONTROL_INPUT_ATTENUATOR = 18 // NUOVA PORTA: Attenuatore di ingresso
} PortIndex;

// --- Coefficienti dei filtri FIR per 2x Oversampling ---
// Questi coefficienti sono per un filtro passa-basso di Nyquist 2x con 48 tap,
// con frequenza di taglio a 0.25 (rispetto alla frequenza di campionamento più alta).
// Progettati per l'interpolazione (valori 0 negli indici dispari) e decimazione.
// La somma dei coefficienti è normalizzata a 1.0 per il filtro passa-basso standard.
// Per l'upsampling, i coefficienti devono essere raddoppiati.
#define FIR_LEN 48 // Lunghezza del filtro FIR (taps)

const float fir_coeffs[FIR_LEN] = {
    -0.000302795f, 0.0f,  0.001099636f, 0.0f, -0.002821422f, 0.0f,  0.005574363f, 0.0f,
    -0.009382218f, 0.0f,  0.014264661f, 0.0f, -0.019972390f, 0.0f,  0.026135894f, 0.0f,
    -0.032338166f, 0.0f,  0.038167828f, 0.0f, -0.043136279f, 0.0f,  0.046831032f, 0.0f,
    -0.048995325f, 0.0f,  0.049449174f, 0.5f,  0.049449174f, 0.0f, -0.048995325f, 0.0f,
    0.046831032f, 0.0f, -0.043136279f, 0.0f,  0.038167828f, 0.0f, -0.032338166f, 0.0f,
    0.026135894f, 0.0f, -0.019972390f, 0.0f,  0.014264661f, 0.0f, -0.009382218f, 0.0f,
    0.005574363f, 0.0f, -0.002821422f, 0.0f,  0.001099636f, 0.0f, -0.000302795f, 0.0f
};

// Struttura per un filtro FIR
typedef struct {
    float delay_line[FIR_LEN]; // Buffer per lo stato del filtro
    int head;                  // Puntatore al campione più recente
} FIRFilter;

// Inizializza il filtro FIR
static void fir_init(FIRFilter* filter) {
    memset(filter->delay_line, 0, sizeof(filter->delay_line));
    filter->head = 0;
}

// Applica il filtro FIR a un campione
// 'coeffs_scale' permette di scalare i coefficienti (es. per guadagno 2.0 per upsampling)
static float fir_process(FIRFilter* filter, float sample, const float* coeffs, int len, float coeffs_scale) {
    filter->delay_line[filter->head] = sample; // Inserisci il nuovo campione
    float output = 0.0f;
    int tap_idx = 0;
    // Convoluzione: somma prodotto di campione e coefficiente
    for (int i = 0; i < len; ++i) {
        // Calcola l'indice circolare (più recente a FIR_LEN-1, più vecchio a 0)
        int idx = (filter->head - i + len) % len;
        output += filter->delay_line[idx] * coeffs[tap_idx] * coeffs_scale;
        tap_idx++;
    }
    filter->head = (filter->head + 1) % len; // Sposta il puntatore
    return output;
}

// Struttura per un filtro Biquad (Second-Order Section)
// Usiamo la forma diretta 1 per semplicità
typedef struct {
    float b0, b1, b2, a1, a2; // Coefficienti
    float z1, z2;             // Stato (delay line)
} BiquadFilter;

// Inizializza il filtro Biquad
static void biquad_init(BiquadFilter* filter) {
    filter->b0 = filter->b1 = filter->b2 = filter->a1 = filter->a2 = 0.0f;
    filter->z1 = filter->z2 = 0.0f;
}

// Calcola i coefficienti per un HPF di 2° ordine con Q regolabile
static void biquad_set_hpf(BiquadFilter* filter, float sample_rate, float cutoff_freq, float Q) {
    float omega = 2.0f * M_PI * cutoff_freq / sample_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * Q);

    float a0 = 1.0f + alpha;

    filter->b0 = (1.0f + cs) / (2.0f * a0);
    filter->b1 = - (1.0f + cs) / a0;
    filter->b2 = (1.0f + cs) / (2.0f * a0);
    filter->a1 = (-2.0f * cs) / a0;
    filter->a2 = (1.0f - alpha) / a0;
}

// Calcola i coefficienti per un LPF di 2° ordine con Q regolabile
static void biquad_set_lpf(BiquadFilter* filter, float sample_rate, float cutoff_freq, float Q) {
    float omega = 2.0f * M_PI * cutoff_freq / sample_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * Q);

    float a0 = 1.0f + alpha;

    filter->b0 = (1.0f - cs) / (2.0f * a0);
    filter->b1 = (1.0f - cs) / a0;
    filter->b2 = (1.0f - cs) / (2.0f * a0);
    filter->a1 = (-2.0f * cs) / a0;
    filter->a2 = (1.0f - alpha) / a0;
}

// Applica il filtro Biquad a un campione
static float biquad_process(BiquadFilter* filter, float sample) {
    float output = filter->b0 * sample + filter->b1 * filter->z1 + filter->b2 * filter->z2 -
                   filter->a1 * filter->z1 - filter->a2 * filter->z2;
    filter->z2 = filter->z1;
    filter->z1 = output;
    return output;
}

// Struttura principale del plugin
typedef struct {
    // Puntatori ai buffer delle porte
    float* gain_in_port;
    float* gain_out_port;
    float* peak_reduction_port;
    float* mode_port;
    float* sidechain_mode_port;
    const float* external_sidechain_in_port;
    float* ratio_select_port;
    float* ar_preset_port;
    float* tube_drive_port;
    float* oversampling_mode_port;
    float* sidechain_hpf_freq_port;
    float* sidechain_lpf_freq_port;
    float* sidechain_filter_q_port;
    float* sidechain_monitor_mode_port;
    float* input_attenuator_port; // NUOVO: Puntatore alla porta attenuatore

    // Puntatori ai buffer audio (stereo fisso per ora)
    const float* input_l_port;
    const float* input_r_port;
    float* output_l_port;
    float* output_r_port;

    // Mapper URI
    LV2_URID_Map* map;
    LV2_URID atom_Path; LV2_URID atom_URID; LV2_URID lv2_AudioPort;
    LV2_URID lv2_InputPort; LV2_URID lv2_OutputPort; LV2_URID port_groups_InputGroup;
    LV2_URID port_groups_OutputGroup; LV2_URID port_groups_master;

    // Variabili interne del plugin
    float sample_rate;         // Frequenza di campionamento dell'host
    float internal_sample_rate; // Frequenza di campionamento interna (con OS)
    int   current_oversampling_factor;  // Fattore di oversampling (1x o 2x)
    int   last_oversampling_mode_param; // Per rilevare il cambio della manopola OS

    float avg_gain;
    float envelope;

    float current_attack_time_sec;
    float current_release_time_sec;
    float attack_coeff;
    float release_coeff;

    float knee_width_db;
    float soft_clip_threshold;
    float soft_clip_factor;

    float tube_saturation_factor;
    float last_tube_output_l;
    float last_tube_output_r;
    float tube_lpf_coeff;

    // Filtri FIR per Oversampling (per ogni canale)
    FIRFilter upsample_filter_l;
    FIRFilter upsample_filter_r;
    FIRFilter downsample_filter_l;
    FIRFilter downsample_filter_r;

    // Filtri Biquad per Sidechain (un set per la mono sidechain)
    BiquadFilter sidechain_hpf;
    BiquadFilter sidechain_lpf;

    float last_hpf_freq; // Per rilevare il cambio param
    float last_lpf_freq; // Per rilevare il cambio param
    float last_filter_q; // Per rilevare il cambio param
    int   last_monitor_mode; // Per rilevare il cambio param

    // Buffer per i campioni oversampled prima e dopo l'elaborazione interna
    // Alloca per il blocco massimo di n_samples * oversampling_factor (max 4096 * 2)
    float* oversampled_input_l_buf;
    float* oversampled_input_r_buf;
    float* oversampled_output_l_buf;
    float* oversampled_output_r_buf;

    float last_input_l_val;
    float last_input_r_val;

    float attenuator_gain_linear; // Valore pre-calcolato per l'attenuatore
} Gla2a;

// Funzioni di utilità
static float db_to_linear(float db) { return powf(10.0f, db / 20.0f); }
static float linear_to_db(float linear) {
    if (linear <= 0.0f) return -90.0f;
    return 20.0f * log10f(linear);
}
static void calculate_envelope_coeffs(Gla2a* plugin) {
    plugin->attack_coeff = expf(-1.0f / (plugin->current_attack_time_sec * plugin->internal_sample_rate));
    plugin->release_coeff = expf(-1.0f / (plugin->current_release_time_sec * plugin->internal_sample_rate));
}
static float soft_clip(float sample, float threshold, float factor) {
    if (sample > threshold) {
        return threshold + (1.0f / factor) * atanf(factor * (sample - threshold));
    } else if (sample < -threshold) {
        return -threshold + (1.0f / factor) * atanf(factor * (sample + threshold));
    }
    return sample;
}
static float tube_model(float sample, float drive, float lpf_coeff, float* last_output) {
    float saturated = tanhf(sample * (1.0f + drive * 5.0f));
    float output = saturated * (1.0f - lpf_coeff) + (*last_output * lpf_coeff);
    *last_output = output;
    return output;
}

// --- Funzioni del Plugin LV2 ---

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                              double                    rate,
                              const char* bundle_path,
                              const LV2_Feature* const* features) {
    Gla2a* plugin = (Gla2a*)calloc(1, sizeof(Gla2a));
    plugin->sample_rate = rate;

    // Inizializzazione fattori di campionamento
    plugin->current_oversampling_factor = 1; // Inizia in modalità Off
    plugin->internal_sample_rate = plugin->sample_rate; // Inizialmente uguale a sample_rate
    plugin->last_oversampling_mode_param = 0; // Inizialmente Off

    plugin->avg_gain = 1.0f;
    plugin->envelope = 0.0f;

    plugin->current_attack_time_sec = 0.010f;
    plugin->current_release_time_sec = 0.060f;
    calculate_envelope_coeffs(plugin); // Calcola con sample_rate iniziale

    plugin->knee_width_db = 10.0f;
    plugin->soft_clip_threshold = 0.8f;
    plugin->soft_clip_factor = 3.0f;

    plugin->tube_saturation_factor = 0.0f;
    plugin->last_tube_output_l = 0.0f;
    plugin->last_tube_output_r = 0.0f;
    float cutoff_freq_lpf_tube = 5000.0f; // 5 kHz
    plugin->tube_lpf_coeff = expf(-2.0f * M_PI * cutoff_freq_lpf_tube / plugin->internal_sample_rate);

    // Inizializza i filtri FIR
    fir_init(&plugin->upsample_filter_l);
    fir_init(&plugin->upsample_filter_r);
    fir_init(&plugin->downsample_filter_l);
    fir_init(&plugin->downsample_filter_r);

    // Inizializza i filtri Biquad per il sidechain
    biquad_init(&plugin->sidechain_hpf);
    biquad_init(&plugin->sidechain_lpf);
    plugin->last_hpf_freq = 20.0f;    // Valore default, no filtering
    plugin->last_lpf_freq = 20000.0f; // Valore default, no filtering
    plugin->last_filter_q = 0.707f; // Valore default (Butterworth)
    plugin->last_monitor_mode = 0;  // Valore default (Off)

    // Calcola i coefficienti iniziali per i filtri sidechain (con valori di default se le porte non sono ancora connesse)
    biquad_set_hpf(&plugin->sidechain_hpf, plugin->internal_sample_rate, plugin->last_hpf_freq, plugin->last_filter_q);
    biquad_set_lpf(&plugin->sidechain_lpf, plugin->internal_sample_rate, plugin->last_lpf_freq, plugin->last_filter_q);

    // Calcola il valore lineare per l'attenuatore (-10dB)
    plugin->attenuator_gain_linear = db_to_linear(-10.0f);


    // Alloca i buffer per i campioni oversampled (massimo n_samples * 2)
    // Usiamo una dimensione fissa basata su un tipico blocco di LV2 (es. 4096)
    // Allocare il buffer una volta sola in instantiate è più efficiente.
    uint32_t max_block_size = 4096; // Questo dovrebbe essere il max_block_size per l'host LV2
    plugin->oversampled_input_l_buf  = (float*)calloc(max_block_size * 2, sizeof(float));
    plugin->oversampled_input_r_buf  = (float*)calloc(max_block_size * 2, sizeof(float));
    plugin->oversampled_output_l_buf = (float*)calloc(max_block_size * 2, sizeof(float));
    plugin->oversampled_output_r_buf = (float*)calloc(max_block_size * 2, sizeof(float));

    if (!plugin->oversampled_input_l_buf || !plugin->oversampled_input_r_buf ||
        !plugin->oversampled_output_l_buf || !plugin->oversampled_output_r_buf) {
        fprintf(stderr, "Gla2a: Failed to allocate oversampling buffers.\n");
        // Libera qualsiasi memoria già allocata prima di uscire con errore
        free(plugin->oversampled_input_l_buf);
        free(plugin->oversampled_input_r_buf);
        free(plugin->oversampled_output_l_buf);
        free(plugin->oversampled_output_r_buf);
        free(plugin);
        return NULL;
    }

    plugin->last_input_l_val = 0.0f; // Inizializza per l'interpolazione lineare
    plugin->last_input_r_val = 0.0f; // Inizializza per l'interpolazione lineare

    const LV2_URID_Map* map = NULL;
    for (int i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            map = (const LV2_URID_Map*)features[i]->data;
            break;
        }
    }
    if (map) {
        plugin->map = (LV2_URID_Map*)map;
        plugin->atom_Path = plugin->map->map(plugin->map->handle, LV2_ATOM__Path);
        plugin->atom_URID = plugin->map->map(plugin->map->handle, LV2_ATOM__URID);
        plugin->lv2_AudioPort = plugin->map->map(plugin->map->handle, LV2_CORE__AudioPort);
        plugin->lv2_InputPort = plugin->map->map(plugin->map->handle, LV2_CORE__InputPort);
        plugin->lv2_OutputPort = plugin->map->map(plugin->map->handle, LV2_CORE__OutputPort);
        plugin->port_groups_InputGroup = plugin->map->map(plugin->map->handle, LV2_PORT_GROUPS__InputGroup);
        plugin->port_groups_OutputGroup = plugin->map->map(plugin->map->handle, LV2_PORT_GROUPS__OutputGroup);
        plugin->port_groups_master = plugin->map->map(plugin->map->handle, LV2_PORT_GROUPS__master);
    } else {
        fprintf(stderr, "Gla2a: Host does not support urid:map. Advanced features may not work.\n");
        plugin->map = NULL;
    }

    return (LV2_Handle)plugin;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location) {
    Gla2a* plugin = (Gla2a*)instance;

    switch ((PortIndex)port) {
        case GLA2A_CONTROL_GAIN_IN: plugin->gain_in_port = (float*)data_location; break;
        case GLA2A_CONTROL_GAIN_OUT: plugin->gain_out_port = (float*)data_location; break;
        case GLA2A_CONTROL_PEAK_REDUCTION: plugin->peak_reduction_port = (float*)data_location; break;
        case GLA2A_CONTROL_MODE: plugin->mode_port = (float*)data_location; break;
        case GLA2A_CONTROL_SIDECHAIN_MODE: plugin->sidechain_mode_port = (float*)data_location; break;
        case GLA2A_AUDIO_EXTERNAL_SIDECHAIN_IN: plugin->external_sidechain_in_port = (const float*)data_location; break;
        case GLA2A_CONTROL_RATIO_SELECT: plugin->ratio_select_port = (float*)data_location; break;
        case GLA2A_CONTROL_AR_PRESET: plugin->ar_preset_port = (float*)data_location; break;
        case GLA2A_CONTROL_TUBE_DRIVE: plugin->tube_drive_port = (float*)data_location; break;
        case GLA2A_CONTROL_OVERSAMPLING_MODE: plugin->oversampling_mode_port = (float*)data_location; break;
        case GLA2A_CONTROL_SIDECHAIN_HPF_FREQ: plugin->sidechain_hpf_freq_port = (float*)data_location; break;
        case GLA2A_CONTROL_SIDECHAIN_LPF_FREQ: plugin->sidechain_lpf_freq_port = (float*)data_location; break;
        case GLA2A_CONTROL_SIDECHAIN_FILTER_Q: plugin->sidechain_filter_q_port = (float*)data_location; break;
        case GLA2A_CONTROL_SIDECHAIN_MONITOR_MODE: plugin->sidechain_monitor_mode_port = (float*)data_location; break;
        case GLA2A_CONTROL_INPUT_ATTENUATOR: plugin->input_attenuator_port = (float*)data_location; break; // NUOVO
        case GLA2A_AUDIO_IN_L: plugin->input_l_port = (const float*)data_location; break;
        case GLA2A_AUDIO_IN_R: plugin->input_r_port = (const float*)data_location; break;
        case GLA2A_AUDIO_OUT_L: plugin->output_l_port = (float*)data_location; break;
        case GLA2A_AUDIO_OUT_R: plugin->output_r_port = (float*)data_location; break;
    }
}

static void activate(LV2_Handle instance) {
    Gla2a* plugin = (Gla2a*)instance;
    plugin->avg_gain = 1.0f;
    plugin->envelope = 0.0f;
    plugin->last_tube_output_l = 0.0f;
    plugin->last_tube_output_r = 0.0f;
    plugin->last_input_l_val = 0.0f; // Reset per l'interpolazione lineare
    plugin->last_input_r_val = 0.0f; // Reset per l'interpolazione lineare

    // Reinizializza anche i filtri FIR al reset del plugin
    fir_init(&plugin->upsample_filter_l);
    fir_init(&plugin->upsample_filter_r);
    fir_init(&plugin->downsample_filter_l);
    fir_init(&plugin->downsample_filter_r);

    // Reinizializza i filtri Biquad del sidechain
    biquad_init(&plugin->sidechain_hpf);
    biquad_init(&plugin->sidechain_lpf);
    // Ricalcola i coefficienti con i valori attuali delle porte (se già connessi)
    // Altrimenti, usa i valori di default inizializzati in instantiate
    biquad_set_hpf(&plugin->sidechain_hpf, plugin->internal_sample_rate, *plugin->sidechain_hpf_freq_port, *plugin->sidechain_filter_q_port);
    biquad_set_lpf(&plugin->sidechain_lpf, plugin->internal_sample_rate, *plugin->sidechain_lpf_freq_port, *plugin->sidechain_filter_q_port);


    // Ricalcola i coefficienti (dipendono da internal_sample_rate)
    // internal_sample_rate sarà aggiornato in run() se il parametro di OS cambia.
    // Qui usiamo il valore corrente al momento dell'attivazione.
    calculate_envelope_coeffs(plugin);
    float cutoff_freq_lpf_tube = 5000.0f;
    plugin->tube_lpf_coeff = expf(-2.0f * M_PI * cutoff_freq_lpf_tube / plugin->internal_sample_rate);
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    Gla2a* plugin = (Gla2a*)instance;

    const float* const input_l          = plugin->input_l_port;
    const float* const input_r          = plugin->input_r_port;
    float* const       output_l         = plugin->output_l_port;
    float* const       output_r         = plugin->output_r_port;
    const float* const external_sidechain_in = plugin->external_sidechain_in_port;

    float input_gain_param_db = *plugin->gain_in_port;
    float output_gain_param_db = *plugin->gain_out_port;
    float peak_reduction_param = *plugin->peak_reduction_port;
    int   mode                 = (int)*plugin->mode_port;
    int   sidechain_mode       = (int)*plugin->sidechain_mode_port;
    int   ratio_select         = (int)*plugin->ratio_select_port;
    int   ar_preset            = (int)*plugin->ar_preset_port;
    float tube_drive_param     = *plugin->tube_drive_port;
    int   oversampling_mode_param = (int)*plugin->oversampling_mode_port;

    // NUOVI PARAMETRI
    float sidechain_hpf_freq = *plugin->sidechain_hpf_freq_port;
    float sidechain_lpf_freq = *plugin->sidechain_lpf_freq_port;
    float sidechain_filter_q = *plugin->sidechain_filter_q_port;
    int   sidechain_monitor_mode = (int)*plugin->sidechain_monitor_mode_port; // 0=Off, 1=On
    int   input_attenuator_on = (int)*plugin->input_attenuator_port; // 0=Off, 1=On

    // Calcola il guadagno dell'attenuatore, se attivo
    float current_input_attenuation = 1.0f; // Nessuna attenuazione di default
    if (input_attenuator_on == 1) {
        current_input_attenuation = plugin->attenuator_gain_linear; // -10dB
    }

    // --- Gestione del cambio di Oversampling Mode ---
    if (oversampling_mode_param != plugin->last_oversampling_mode_param) {
        plugin->last_oversampling_mode_param = oversampling_mode_param;
        if (oversampling_mode_param == 1) { // Oversampling 2x
            plugin->current_oversampling_factor = 2;
        } else { // Oversampling Off
            plugin->current_oversampling_factor = 1;
        }
        plugin->internal_sample_rate = plugin->sample_rate * plugin->current_oversampling_factor;
        // Ricalcola tutti i coefficienti che dipendono dalla frequenza di campionamento interna
        calculate_envelope_coeffs(plugin);
        float cutoff_freq_lpf_tube = 5000.0f;
        plugin->tube_lpf_coeff = expf(-2.0f * M_PI * cutoff_freq_lpf_tube / plugin->internal_sample_rate);

        // Reset dei filtri FIR per evitare artefatti al cambio di modalità
        fir_init(&plugin->upsample_filter_l);
        fir_init(&plugin->upsample_filter_r);
        fir_init(&plugin->downsample_filter_l);
        fir_init(&plugin->downsample_filter_r);
        // Reset degli stati per l'interpolazione lineare (se usata)
        plugin->last_input_l_val = 0.0f;
        plugin->last_input_r_val = 0.0f;

        // Ricalcola i coefficienti dei filtri sidechain con la nuova internal_sample_rate
        biquad_set_hpf(&plugin->sidechain_hpf, plugin->internal_sample_rate, sidechain_hpf_freq, sidechain_filter_q);
        biquad_set_lpf(&plugin->sidechain_lpf, plugin->internal_sample_rate, sidechain_lpf_freq, sidechain_filter_q);
    }

    // --- Gestione del cambio dei parametri dei filtri sidechain ---
    // Ricalcola i coefficienti dei filtri sidechain SOLO se i parametri sono cambiati
    // o se la frequenza di campionamento interna è cambiata.
    if (sidechain_hpf_freq != plugin->last_hpf_freq ||
        sidechain_lpf_freq != plugin->last_lpf_freq ||
        sidechain_filter_q != plugin->last_filter_q) {

        biquad_set_hpf(&plugin->sidechain_hpf, plugin->internal_sample_rate, sidechain_hpf_freq, sidechain_filter_q);
        biquad_set_lpf(&plugin->sidechain_lpf, plugin->internal_sample_rate, sidechain_lpf_freq, sidechain_filter_q);

        plugin->last_hpf_freq = sidechain_hpf_freq;
        plugin->last_lpf_freq = sidechain_lpf_freq;
        plugin->last_filter_q = sidechain_filter_q;
    }

    const int os_factor = plugin->current_oversampling_factor;
    const uint32_t os_n_samples = n_samples * os_factor;

    // Converti i guadagni da dB a lineare
    float input_gain_linear = db_to_linear(input_gain_param_db);
    float output_gain_linear = db_to_linear(output_gain_param_db);

    float selected_ratio;
    switch (ratio_select) {
        case 0: selected_ratio = 2.0f; break;
        case 1: selected_ratio = 4.0f; break;
        case 2: selected_ratio = 8.0f; break;
        default: selected_ratio = 4.0f; break;
    }

    float new_attack_time_sec;
    float new_release_time_sec;
    switch (ar_preset) {
        case 0: new_attack_time_sec = 0.005f; new_release_time_sec = 0.050f; break;
        case 1: new_attack_time_sec = 0.010f; new_release_time_sec = 0.060f; break;
        case 2: new_attack_time_sec = 0.020f; new_release_time_sec = 0.150f; break;
        default: new_attack_time_sec = 0.010f; new_release_time_sec = 0.060f; break;
    }

    if (new_attack_time_sec != plugin->current_attack_time_sec ||
        new_release_time_sec != plugin->current_release_time_sec) {
        plugin->current_attack_time_sec = new_attack_time_sec;
        plugin->current_release_time_sec = new_release_time_sec;
        calculate_envelope_coeffs(plugin);
    }

    float base_implicit_threshold_db = 20.0f - (peak_reduction_param * 0.6f);
    float attack_coeff = plugin->attack_coeff;
    float release_coeff = plugin->release_coeff;

    // Buffer temporaneo per il segnale sidechain monitorato (solo per il caso monitor)
    float* sidechain_monitor_buf_l = (float*)calloc(os_n_samples, sizeof(float));
    float* sidechain_monitor_buf_r = (float*)calloc(os_n_samples, sizeof(float));

    // --- Fase 1: Upsampling (se attivo) o copia diretta (se non attivo) ---
    if (os_factor == 2) { // Oversampling 2x attivo
        for (uint32_t i = 0; i < n_samples; ++i) {
            // Applica l'attenuatore qui, prima dell'upsampling e della compressione
            float attenuated_input_l = input_l[i] * current_input_attenuation;
            float attenuated_input_r = input_r[i] * current_input_attenuation;

            plugin->oversampled_input_l_buf[i * 2] = attenuated_input_l;
            plugin->oversampled_input_r_buf[i * 2] = attenuated_input_r;

            float interpolated_l = 0.5f * (plugin->last_input_l_val + attenuated_input_l);
            float interpolated_r = 0.5f * (plugin->last_input_r_val + attenuated_input_r);
            if (i == 0) {
                interpolated_l = attenuated_input_l;
                interpolated_r = attenuated_input_r;
            }
            plugin->oversampled_input_l_buf[i * 2 + 1] = interpolated_l;
            plugin->oversampled_input_r_buf[i * 2 + 1] = interpolated_r;

            plugin->last_input_l_val = attenuated_input_l;
            plugin->last_input_r_val = attenuated_input_r;
        }
        for (uint32_t i = 0; i < os_n_samples; ++i) {
            plugin->oversampled_input_l_buf[i] = fir_process(&plugin->upsample_filter_l, plugin->oversampled_input_l_buf[i], fir_coeffs, FIR_LEN, 2.0f);
            plugin->oversampled_input_r_buf[i] = fir_process(&plugin->upsample_filter_r, plugin->oversampled_input_r_buf[i], fir_coeffs, FIR_LEN, 2.0f);
        }
    } else { // Oversampling Off (1x)
        for (uint32_t i = 0; i < n_samples; ++i) {
            // Applica l'attenuatore qui
            plugin->oversampled_input_l_buf[i] = input_l[i] * current_input_attenuation;
            plugin->oversampled_input_r_buf[i] = input_r[i] * current_input_attenuation;
        }
    }

    // --- Fase 2: Elaborazione Interna (al sample_rate interno) ---
    for (uint32_t i = 0; i < os_n_samples; ++i) {
        float current_input_l_os = plugin->oversampled_input_l_buf[i];
        float current_input_r_os = plugin->oversampled_input_r_buf[i];

        // 1. Ottieni il segnale di sidechain GREZZO
        float raw_sidechain_sample_l; // Useremo entrambi i canali per monitoraggio
        float raw_sidechain_sample_r;
        float detection_sample; // Sample usato per la rilevazione dell'inviluppo

        if (sidechain_mode == 0) { // Internal Sidechain
            // Il segnale di sidechain interno dovrebbe già essere stato attenuato se l'attenuatore è attivo
            raw_sidechain_sample_l = current_input_l_os * input_gain_linear;
            raw_sidechain_sample_r = current_input_r_os * input_gain_linear;
            detection_sample = fmaxf(fabsf(raw_sidechain_sample_l), fabsf(raw_sidechain_sample_r));
        } else { // External Sidechain
            // Per il sidechain esterno, dobbiamo prendere il campione dal buffer originale
            // e possibilmente interpolarlo per gli indici oversampled. L'attenuatore NON si applica qui.
            if (external_sidechain_in && (i / os_factor < n_samples)) {
                 raw_sidechain_sample_l = external_sidechain_in[i / os_factor]; // Usiamo solo il mono esterno per entrambi L/R in monitor mode
                 raw_sidechain_sample_r = external_sidechain_in[i / os_factor];
                 detection_sample = fabsf(external_sidechain_in[i / os_factor]);
            } else {
                raw_sidechain_sample_l = 0.0f;
                raw_sidechain_sample_r = 0.0f;
                detection_sample = 0.0f;
            }
        }

        // --- APPLICA I FILTRI AL SEGNALE DI SIDECHAIN QUI ---
        float filtered_sidechain_l = biquad_process(&plugin->sidechain_hpf, raw_sidechain_sample_l);
        filtered_sidechain_l = biquad_process(&plugin->sidechain_lpf, filtered_sidechain_l);

        float filtered_sidechain_r = biquad_process(&plugin->sidechain_hpf, raw_sidechain_sample_r); // I filtri sono gli stessi per L e R
        filtered_sidechain_r = biquad_process(&plugin->sidechain_lpf, filtered_sidechain_r);

        // Il campione per la rilevazione dell'inviluppo viene dal segnale filtrato
        // Usiamo l'ampiezza massima (peak) dei canali filtrati per la rilevazione.
        detection_sample = fmaxf(fabsf(filtered_sidechain_l), fabsf(filtered_sidechain_r));

        // 2. Rilevamento dell'inviluppo (a internal_sample_rate)
        if (detection_sample > plugin->envelope) {
            plugin->envelope = (detection_sample * (1.0f - attack_coeff)) + (plugin->envelope * attack_coeff);
        } else {
            plugin->envelope = (detection_sample * (1.0f - release_coeff)) + (plugin->envelope * release_coeff);
        }
        if (plugin->envelope < 0.0f) plugin->envelope = 0.0f;

        // Se la modalità monitor è attiva, copia il segnale sidechain filtrato nel buffer temporaneo
        if (sidechain_monitor_mode == 1) {
            sidechain_monitor_buf_l[i] = filtered_sidechain_l;
            sidechain_monitor_buf_r[i] = filtered_sidechain_r;
            // Nel monitor mode, il compressore non deve agire sul segnale
            plugin->oversampled_output_l_buf[i] = 0.0f; // Verrà sovrascritto
            plugin->oversampled_output_r_buf[i] = 0.0f; // Verrà sovrascritto
        } else {
            // 3. Calcolo del Guadagno di Riduzione (Gain Reduction) con Soft Knee
            // ... (logica di compressione rimane uguale, usa plugin->envelope)
            float current_gain_reduction_db = 0.0f;
            float envelope_db = linear_to_db(plugin->envelope);

            float threshold_lower = base_implicit_threshold_db - (plugin->knee_width_db / 2.0f);
            float threshold_upper = base_implicit_threshold_db + (plugin->knee_width_db / 2.0f);

            float current_ratio_for_logic = (mode == 0) ? selected_ratio : 20.0f;

            if (envelope_db > threshold_upper) {
                float overshoot_db = envelope_db - base_implicit_threshold_db;
                current_gain_reduction_db = - (overshoot_db / current_ratio_for_logic);
            } else if (envelope_db > threshold_lower) {
                float x = (envelope_db - threshold_lower) / plugin->knee_width_db;
                float ratio_start = 1.0f;
                float current_ratio_in_knee = ratio_start + (current_ratio_for_logic - ratio_start) * x;
                float effective_overshoot_db = envelope_db - threshold_lower;
                current_gain_reduction_db = - (effective_overshoot_db / current_ratio_in_knee);
            }

            // 4. Smoothing del Guadagno (Gain Smoothing)
            float target_gain_linear = db_to_linear(current_gain_reduction_db);

            if (target_gain_linear < plugin->avg_gain) {
                plugin->avg_gain = (target_gain_linear * (1.0f - attack_coeff)) + (plugin->avg_gain * attack_coeff);
            } else {
                plugin->avg_gain = (target_gain_linear * (1.0f - release_coeff)) + (plugin->avg_gain * release_coeff);
            }

            if (plugin->avg_gain > 1.0f) plugin->avg_gain = 1.0f;
            if (plugin->avg_gain < 0.0001f) plugin->avg_gain = 0.0001f;

            // 5. Applica il guadagno di compressione e il make-up gain
            float processed_l = current_input_l_os * input_gain_linear * plugin->avg_gain * output_gain_linear;
            float processed_r = current_input_r_os * input_gain_linear * plugin->avg_gain * output_gain_linear;

            // 6. Applica la Simulazione di Valvola (a internal_sample_rate)
            float tube_processed_l = tube_model(processed_l, tube_drive_param, plugin->tube_lpf_coeff, &plugin->last_tube_output_l);
            float tube_processed_r = tube_model(processed_r, tube_drive_param, plugin->tube_lpf_coeff, &plugin->last_tube_output_r);

            // 7. Applica il Soft Clip (a internal_sample_rate)
            plugin->oversampled_output_l_buf[i] = soft_clip(tube_processed_l, plugin->soft_clip_threshold, plugin->soft_clip_factor);
            plugin->oversampled_output_r_buf[i] = soft_clip(tube_processed_r, plugin->soft_clip_threshold, plugin->soft_clip_factor);
        }
    }

    // --- Fase 3: Downsampling (se attivo) o copia diretta (se non attivo) ---
    if (os_factor == 2) { // Oversampling 2x attivo
        for (uint32_t i = 0; i < n_samples; ++i) {
            if (sidechain_monitor_mode == 1) {
                // Downsample il segnale sidechain monitorato
                output_l[i] = fir_process(&plugin->downsample_filter_l, sidechain_monitor_buf_l[i * 2], fir_coeffs, FIR_LEN, 1.0f);
                output_r[i] = fir_process(&plugin->downsample_filter_r, sidechain_monitor_buf_r[i * 2], fir_coeffs, FIR_LEN, 1.0f);
            } else {
                output_l[i] = fir_process(&plugin->downsample_filter_l, plugin->oversampled_output_l_buf[i * 2], fir_coeffs, FIR_LEN, 1.0f);
                output_r[i] = fir_process(&plugin->downsample_filter_r, plugin->oversampled_output_r_buf[i * 2], fir_coeffs, FIR_LEN, 1.0f);
            }
        }
    } else { // Oversampling Off (1x)
        if (sidechain_monitor_mode == 1) {
            // Copia direttamente il buffer monitorato
            memcpy(output_l, sidechain_monitor_buf_l, n_samples * sizeof(float));
            memcpy(output_r, sidechain_monitor_buf_r, n_samples * sizeof(float));
        } else {
            // Copia direttamente il buffer compresso
            memcpy(output_l, plugin->oversampled_output_l_buf, n_samples * sizeof(float));
            memcpy(output_r, plugin->oversampled_output_r_buf, n_samples * sizeof(float));
        }
    }

    // Libera i buffer temporanei allocati solo per questa run
    free(sidechain_monitor_buf_l);
    free(sidechain_monitor_buf_r);
}

static void deactivate(LV2_Handle instance) {
    // Nulla da fare qui
}

static void cleanup(LV2_Handle instance) {
    Gla2a* plugin = (Gla2a*)instance;
    // Libera i buffer allocati in instantiate
    free(plugin->oversampled_input_l_buf);
    free(plugin->oversampled_input_r_buf);
    free(plugin->oversampled_output_l_buf);
    free(plugin->oversampled_output_r_buf);
    free(instance);
}

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