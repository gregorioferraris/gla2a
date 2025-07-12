#ifndef GLA2A_H
#define GLA2A_H

#include <lv2/core/lv2.h>

// Definizione dell'URI del plugin.
#define GLA2A_URI "http://moddevices.com/plugins/mod-devel/gla2a" // URI specifico per GLA2A

// Definizione dell'URI della GUI (se ci sarà, altrimenti usa lo stesso URI del plugin)
#define GLA2A_GUI_URI "http://moddevices.com/plugins/mod-devel/gla2a_ui"

// Enum degli indici delle porte del plugin (stessi del GLA3A per coerenza, solo nome URI cambia)
typedef enum {
    GLA2A_PEAK_REDUCTION = 0,
    GLA2A_GAIN = 1,
    GLA2A_METER = 2,
    GLA2A_BYPASS = 3,
    GLA2A_MS_MODE_ACTIVE = 4,
    GLA2A_RATIO_MODE = 5,
    GLA2A_SC_LP_ON = 6,
    GLA2A_SC_LP_FREQ = 7,
    GLA2A_SC_LP_Q = 8,
    GLA2A_SC_HP_ON = 9,
    GLA2A_SC_HP_FREQ = 10,
    GLA2A_SC_HP_Q = 11,
    GLA2A_OUTPUT_RMS = 12,
    GLA2A_GAIN_REDUCTION_METER = 13,
    GLA2A_AUDIO_IN_L = 14,
    GLA2A_AUDIO_IN_R = 15,
    GLA2A_AUDIO_OUT_L = 16,
    GLA2A_AUDIO_OUT_R = 17
} GLA2A_PortIndex;

// Enum per le modalità di ratio (per chiarezza nel codice C++)
typedef enum {
    GLA2A_RATIO_3_TO_1 = 0,
    GLA2A_RATIO_6_TO_1 = 1,
    GLA2A_RATIO_9_TO_1 = 2,
    GLA2A_RATIO_LIMIT  = 3
} GLA2A_RatioMode;

#endif // GLA2A_H
