#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <math.h>
#include <stdlib.h> // Per calloc e free

// URI unico del nostro plugin. E' fondamentale che sia unico a livello mondiale!
// Useremo il tuo nome utente GitHub per renderlo specifico.
#define GLA2A_URI "http://gregorioferraris.github.io/lv2/gla2a"

// Definizione delle porte del plugin
// Ogni controllo o connessione audio ha un suo indice numerico.
typedef enum {
    GLA2A_CONTROL_GAIN_IN  = 0, // Controllo dell'input gain
    GLA2A_CONTROL_THRESHOLD = 1, // Soglia per la compressione
    GLA2A_CONTROL_RATIO    = 2, // Rapporto di compressione
    GLA2A_CONTROL_ATTACK   = 3, // Tempo di attacco
    GLA2A_CONTROL_RELEASE  = 4, // Tempo di rilascio
    GLA2A_AUDIO_IN_L       = 5, // Ingresso audio sinistro
    GLA2A_AUDIO_IN_R       = 6, // Ingresso audio destro
    GLA2A_AUDIO_OUT_L      = 7, // Uscita audio sinistro
    GLA2A_AUDIO_OUT_R      = 8  // Uscita audio destro
} PortIndex;

// Struttura che conterrà lo stato di un'istanza del plugin
typedef struct {
    // Puntatori ai buffer delle porte, aggiornati dall'host
    float* gain_in_port;
    float* threshold_port;
    float* ratio_port;
    float* attack_port;
    float* release_port;
    const float* input_l_port;
    const float* input_r_port;
    float* output_l_port;
    float* output_r_port;

    // Variabili interne del plugin per la logica di compressione
    float sample_rate; // Frequenza di campionamento
    float avg_gain;    // Guadagno medio corrente del compressore (per evitare click)
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
    return (LV2_Handle)plugin;
}

// 2. `connect_port`: Connette una porta del plugin a un buffer di memoria
static void connect_port(LV2_Handle instance, uint32_t port, void* data_location) {
    Gla2a* plugin = (Gla2a*)instance;

    switch ((PortIndex)port) {
        case GLA2A_CONTROL_GAIN_IN:
            plugin->gain_in_port = (float*)data_location;
            break;
        case GLA2A_CONTROL_THRESHOLD:
            plugin->threshold_port = (float*)data_location;
            break;
        case GLA2A_CONTROL_RATIO:
            plugin->ratio_port = (float*)data_location;
            break;
        case GLA2A_CONTROL_ATTACK:
            plugin->attack_port = (float*)data_location;
            break;
        case GLA2A_CONTROL_RELEASE:
            plugin->release_port = (float*)data_location;
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

    const float* const input_l  = plugin->input_l_port;
    const float* const input_r  = plugin->input_r_port;
    float* const       output_l = plugin->output_l_port;
    float* const       output_r = plugin->output_r_port;

    float input_gain_db = *plugin->gain_in_port;
    float threshold_db  = *plugin->threshold_port;
    float ratio         = *plugin->ratio_port;
    float attack_ms     = *plugin->attack_port;
    float release_ms    = *plugin->release_port;

    float input_gain_linear = powf(10.0f, input_gain_db / 20.0f);

    for (uint32_t i = 0; i < n_samples; ++i) {
        output_l[i] = input_l[i] * input_gain_linear;
        output_r[i] = input_r[i] * input_gain_linear;
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