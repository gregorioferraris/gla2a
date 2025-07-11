#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <lv2/atom/atom.h>         // Per LV2 Atom (gestione delle porte dinamiche)
#include <lv2/atom/atom-forge.h>    // Per creare atomi (se necessario, non strettamente qui)
#include <lv2/urid/urid.h>          // Per mappare URI a ID numerici
#include <lv2/port-groups/port-groups.h> // Per le Port Groups
#include <math.h>
#include <stdlib.h> // Per calloc e free
#include <stdio.h> // Per printf, utile per debug
#include <string.h> // Per strcmp

// URI unico del nostro plugin. E' fondamentale che sia unico a livello mondiale!
#define GLA2A_URI "http://gregorioferraris.github.io/lv2/gla2a"

// Definizione delle porte di controllo (gli indici NON CAMBIANO)
typedef enum {
    GLA2A_CONTROL_GAIN_IN      = 0,
    GLA2A_CONTROL_GAIN_OUT     = 1,
    GLA2A_CONTROL_PEAK_REDUCTION = 2,
    GLA2A_CONTROL_MODE         = 3,
    GLA2A_CONTROL_SIDECHAIN_MODE = 4,
    GLA2A_AUDIO_EXTERNAL_SIDECHAIN_IN = 5 // Questa rimane una porta singola
    // Le porte audio In/Out non hanno più indici fissi qui
} PortIndex;

// Struttura che conterrà lo stato di un'istanza del plugin
typedef struct {
    // Puntatori ai buffer delle porte di controllo
    float* gain_in_port;
    float* gain_out_port;
    float* peak_reduction_port;
    float* mode_port;
    float* sidechain_mode_port;
    const float* external_sidechain_in_port;

    // Puntatori ai buffer audio dinamici (array di puntatori a float)
    // Questi saranno allocati dinamicamente in base al numero di canali
    const float** input_ports;
    float** output_ports;
    uint32_t num_channels; // Numero corrente di canali audio

    // Mapper URI (essenziale per Port Groups)
    LV2_URID_Map* map;
    LV2_URID atom_Path;
    LV2_URID atom_URID;
    LV2_URID lv2_AudioPort;
    LV2_URID lv2_InputPort;
    LV2_URID lv2_OutputPort;
    LV2_URID port_groups_InputGroup;
    LV2_URID port_groups_OutputGroup;
    LV2_URID port_groups_master; // Per identificare la porta "master" nel gruppo (il primo canale)

    // Variabili interne del plugin per la logica di compressione
    float sample_rate; // Frequenza di campionamento
    float avg_gain;    // Guadagno medio corrente del compressore (per evitare click)
    float envelope;    // Valore dell'inviluppo del segnale (per determinare la compressione)
    
    const float attack_time_sec;
    const float release_time_sec; 

    float attack_coeff; 
    float release_coeff;

    float knee_width_db; // Larghezza del ginocchio in dB
    float soft_clip_threshold; // Soglia per il soft clip (valore lineare)
    float soft_clip_factor; // Fattore di "morbidezza" del soft clip

} Gla2a;

// Funzioni di utilità per conversioni dB <-> lineare
static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static float linear_to_db(float linear) {
    if (linear <= 0.0f) return -90.0f;
    return 20.0f * log10f(linear);
}

static void calculate_envelope_coeffs(Gla2a* plugin) {
    plugin->attack_coeff = expf(-1.0f / (plugin->attack_time_sec * plugin->sample_rate));
    plugin->release_coeff = expf(-1.0f / (plugin->release_time_sec * plugin->sample_rate));
}

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
    plugin->sample_rate = rate;

    // Cerca la feature LV2_URID_Map
    const LV2_URID_Map* map = NULL;
    for (int i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            map = (const LV2_URID_Map*)features[i]->data;
            break;
        }
    }
    if (!map) { // Se il mapper non è disponibile, non possiamo continuare
        fprintf(stderr, "Gla2a: Host does not support urid:map. Aborting.\n");
        free(plugin);
        return NULL;
    }
    plugin->map = (LV2_URID_Map*)map; // Cast a non-const per usarlo con urid_map

    // Mappa gli URI necessari in URID numerici
    plugin->atom_Path = plugin->map->map(plugin->map->handle, LV2_ATOM__Path);
    plugin->atom_URID = plugin->map->map(plugin->map->handle, LV2_ATOM__URID);
    plugin->lv2_AudioPort = plugin->map->map(plugin->map->handle, LV2_CORE__AudioPort);
    plugin->lv2_InputPort = plugin->map->map(plugin->map->handle, LV2_CORE__InputPort);
    plugin->lv2_OutputPort = plugin->map->map(plugin->map->handle, LV2_CORE__OutputPort);
    plugin->port_groups_InputGroup = plugin->map->map(plugin->map->handle, LV2_PORT_GROUPS__InputGroup);
    plugin->port_groups_OutputGroup = plugin->map->map(plugin->map->handle, LV2_PORT_GROUPS__OutputGroup);
    plugin->port_groups_master = plugin->map->map(plugin->map->handle, LV2_PORT_GROUPS__master);


    // Inizializza le variabili interne
    plugin->avg_gain = 1.0f;
    plugin->envelope = 0.0f