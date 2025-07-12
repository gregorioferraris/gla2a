#ifndef GLA2A_H
#define GLA2A_H

#include <lv2/core/lv2.h> // Include standard LV2 headers, if needed by other parts of your .h

// Definizione dell'URI del plugin (DEVE corrispondere a gla2a.ttl e gla2a.cpp)
#define GLA2A_URI "http://moddevices.com/plugins/mod-devel/gla2a"

// Enum degli indici delle porte
// Questi indici DEVONO corrispondere esattamente agli indici in gla2a.ttl
typedef enum {
    GLA2A_PEAK_REDUCTION = 0,
    GLA2A_GAIN = 1,
    GLA2A_METER = 2,
    GLA2A_LIMITER = 3,
    GLA2A_HI_FREQ = 4,
    GLA2A_OUTPUT_RMS = 5,
    GLA2A_GAIN_REDUCTION_METER = 6,
    GLA2A_BYPASS = 7,         // NUOVO
    GLA2A_MS_MODE_ACTIVE = 8, // NUOVO
    GLA2A_AUDIO_IN_L = 9,
    GLA2A_AUDIO_IN_R = 10,
    GLA2A_AUDIO_OUT_L = 11,
    GLA2A_AUDIO_OUT_R = 12
} GLA2A_PortIndex;

// Puoi aggiungere qui altre definizioni o forward declarations se necessario
// Ad esempio, la definizione della struttura Gla2a potrebbe andare qui,
// ma per ora la lasciamo in .cpp per semplicità e per evitare dipendenze reciproche.

#endif // GLA2A_H
