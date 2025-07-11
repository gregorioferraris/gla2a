#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <math.h>
#include <stdlib.h> // Per calloc e free

// URI unico del nostro plugin. E' fondamentale che sia unico a livello mondiale!
// Useremo il tuo nome utente GitHub per renderlo specifico.
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
    const float* external_sidechain_in_port; // Nuovo puntatore per sidechain esterno
    const float* input_l_port;
    const float* input_r_port;
    float* output_l_port;
    float* output_r_port;

    // Variabili interne del plugin per la logica di compressione
    float sample_rate; // Frequenza di campionamento
    float avg_gain;    // Guadagno medio corrente del compressore (per evitare click)
    // Aggiunte per il compressore:
    float envelope;    // Valore dell'inviluppo del segnale (per determinare la compressione)
    float attack_coeff; // Coefficiente per il tempo di attacco
    float release_coeff; // Coefficiente per il tempo di rilascio

} Gla2a;

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
    // Inizializza i coefficienti (valori placeholder, saranno calcolati in run())
    plugin->attack_coeff = 0.0f;
    plugin->release_coeff = 0.0f;
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
        case GLA2A_AUDIO_EXTERNAL_SIDECHAIN_IN: // Nuova porta
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

// 3. `activate`: Chiamata prima del processamento audio (non usiamo qui per ora)
static void activate(LV2_Handle instance) {
    // Nulla da fare per ora all'attivazione
}

// 4. `run`: La funzione principale di elaborazione audio
static void run(LV2_Handle instance, uint32_t n_samples) {
    Gla2a* plugin = (Gla2a*)instance;

    // Ottieni i puntatori ai buffer delle porte
    const float* const input_l          = plugin->input_l_port;
    const float* const input_r          = plugin->input_r_port;
    float* const       output_l         = plugin->output_l_port;
    float* const       output_r         = plugin->output_r_port;

    const float* const external_sidechain_in = plugin->external_sidechain_in_port; // Sidechain esterno

    // Ottieni i valori attuali dei controlli (sono puntatori, quindi dereferenziali)
    float input_gain_param_db = *plugin->gain_in_port;
    float output_gain_param_db = *plugin->gain_out_port;
    float peak_reduction_param = *plugin->peak_reduction_port; // Valore 0-100
    int   mode                 = (int)*plugin->mode_port; // 0=Compress, 1=Limit
    int   sidechain_mode       = (int)*plugin->sidechain_mode_port; // 0=Internal, 1=External

    // Converti i guadagni da dB a lineare per i parametri di input/output
    float input_gain_linear = powf(10.0f, input_gain_param_db / 20.0f);
    float output_gain_linear = powf(10.0f, output_gain_param_db / 20.0f);

    // --- Logica Semplificata del Compressore (Placeholder) ---
    // Questo è un placeholder. L'algoritmo reale del LA-2A sarà molto più complesso.
    // Qui applichiamo solo il guadagno di input/output.
    for (uint32_t i = 0; i < n_samples; ++i) {
        float current_input_l = input_l[i];
        float current_input_r = input_r[i];

        // Placeholder per il segnale di sidechain
        float sidechain_signal = 0.0f;
        if (sidechain_mode == 0) { // Internal Sidechain
            // Per ora, useremo il segnale più forte tra L e R per il sidechain interno
            sidechain_signal = fabsf(current_input_l) > fabsf(current_input_r) ?
                               fabsf(current_input_l) : fabsf(current_input_r);
        } else { // External Sidechain
            // Usiamo il segnale del sidechain esterno, se disponibile.
            // LV2 garantisce che external_sidechain_in sia valido se la porta è connessa.
            // Se non è connessa e la modalità è su External, il comportamento sarà "no compressione".
            if (external_sidechain_in) { // Verifica se il puntatore è valido (la porta è connessa)
                 sidechain_signal = fabsf(external_sidechain_in[i]);
            } else {
                 sidechain_signal = 0.0f; // Nessun segnale di sidechain se la porta non è connessa
            }
        }

        // --- Logica placeholder per la compressione ---
        // Questo è il punto dove verrà implementato l'algoritmo LA-2A.
        // Per ora, il "compressore" non fa nulla di reale, passa solo il segnale.
        // La variabile `peak_reduction_param` verrà usata qui per determinare la compressione.
        // La variabile `mode` (Compress/Limit) influenzerà la curva di compressione.

        // Applica i guadagni di input e output
        output_l[i] = current_input_l * input_gain_linear * output_gain_linear;
        output_r[i] = current_input_r * input_gain_linear * output_gain_linear;
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