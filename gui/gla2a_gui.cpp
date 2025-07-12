#include <lv2/ui/ui.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/urid/urid.h>
#include <lv2/core/lv2.h>
#include <lv2/log/log.h>
#include <lv2/log/logger.h>

// --- DIPENDENZE IMGUI ---
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include <cmath>     // Per funzioni matematiche come atan2, sin, cos
#include <chrono>    // Per la gestione del tempo in C++11
#include <vector>    // Per std::vector
#include <string>    // Per std::string
#include <iostream>  // Per cout/cerr (debug)

// --- DIPENDENZE STB_IMAGE (per caricare immagini) ---
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" // Assicurati che questo header sia nel tuo INCLUDE path

// --- DIPENDENZE PER IL CONTESTO OPENGL (Linux/X11) ---
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>   // Per XGetWindowAttributes
#include <X11/keysym.h>  // Per la gestione della tastiera (sebbene l'inoltro eventi sia un punto critico)

// --- URID e LOGGING (definizione esterna, come nel tuo codice LV2) ---
LV2_LOG_Logger logger;
LV2_URID_Map urid_map;
LV2_URID_Unmap urid_unmap;

// =========================================================================
// URI dei Parametri (Devono corrispondere esattamente al tuo plugin audio)
// =========================================================================
// URI per i parametri del tuo compressore
static LV2_URID peakReduction_URID;
static LV2_URID gain_URID;
static LV2_URID bypass_URID;
static LV2_URID ratioMode_URID;
static LV2_URID valveDrive_URID;
static LV2_URID inputPad10dB_URID;
static LV2_URID oversamplingOn_URID;
static LV2_URID sidechainMode_URID;
static LV2_URID scLpOn_URID;
static LV2_URID scLpFq_URID;
static LV2_URID scLpQ_URID;
static LV2_URID scHpOn_URID;
static LV2_URID scHpFq_URID;
static LV2_URID scHpQ_URID;

// URI per i valori dei meter (che il plugin audio invierà alla GUI)
static LV2_URID peakGR_URID;
static LV2_URID peakInL_URID;
static LV2_URID peakInR_URID;
static LV2_URID peakOutL_URID;
static LV2_URID peakOutR_URID;


// =========================================================================
// Funzioni Helper per ImGui
// =========================================================================

// Implementazione della funzione di gestione del tempo per ImGui
static double get_time_in_seconds() {
    static auto start_time = std::chrono::high_resolution_clock::now();
    auto current_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = current_time - start_time;
    return elapsed.count();
}

// Helper function per caricare texture con stb_image
GLuint LoadTextureFromFile(const char* filename, int* out_width, int* out_height)
{
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
    if (image_data == NULL) {
        lv2_log_error(&logger, "Error: Could not load texture from file: %s\n", filename);
        return 0;
    }

    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

    stbi_image_free(image_data);

    *out_width = image_width;
    *out_height = image_height;

    return image_texture;
}

// Funzione helper per creare un knob rotativo basato su immagine
// Ritorna true se il valore è stato modificato
bool KnobRotaryImage(const char* label, float* p_value, float v_min, float v_max,
                     GLuint texture_id, int frame_width, int frame_height, int total_frames,
                     ImVec2 knob_size_pixels, const char* format = "%.2f")
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x + knob_size_pixels.x, pos.y + knob_size_pixels.y + ImGui::GetTextLineHeightWithSpacing()));

    ImGui::ItemSize(bb); // Riserva spazio per il knob e il label
    if (!ImGui::ItemAdd(bb, id))
        return false;

    const bool hovered = ImGui::ItemHoverable(bb, id);
    bool value_changed = false;
    bool held = ImGui::IsItemActive();

    // Gestione dell'input (trascinamento verticale)
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!held) ImGui::SetActiveID(id, window);
    }

    if (held) {
        float delta_y = ImGui::GetIO().MouseDelta.y;
        float speed = (v
