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
    // Controllo se il mouse è cliccato o trascinato sull'elemento
    bool value_changed = false;
    bool held = ImGui::IsItemActive(); // L'elemento è attivo (cliccato e trascinato)

    // Gestione dell'input (trascinamento verticale)
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // Se il knob non era già attivo, rendilo attivo (per la logica di ImGui)
        if (!held) ImGui::SetActiveID(id, window);
    }

    if (held) {
        float delta_y = ImGui::GetIO().MouseDelta.y;
        float speed = (v_max - v_min) / (knob_size_pixels.y * 2.0f); // Sensibilità basata sull'altezza del knob
        *p_value -= delta_y * speed;
        // Clampa il valore
        *p_value = ImClamp(*p_value, v_min, v_max);
        value_changed = true;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImGui::ClearActiveID(); // Rilascia il controllo
        }
    }
    // Gestione rotella del mouse
    if (hovered && io.MouseWheel != 0.0f) {
        float wheel_speed = (v_max - v_min) / 50.0f; // Sensibilità rotella
        *p_value += io.MouseWheel * wheel_speed;
        *p_value = ImClamp(*p_value, v_min, v_max);
        value_changed = true;
    }


    // Calcola il frame da disegnare
    float normalized_value = (*p_value - v_min) / (v_max - v_min);
    int frame_index = static_cast<int>(normalized_value * (total_frames - 1));
    frame_index = ImClamp(frame_index, 0, total_frames - 1); // Clampa l'indice del frame

    // Calcola le coordinate UV per lo sprite sheet (assumendo i frame siano in una colonna)
    ImVec2 uv0 = ImVec2(0.0f, (float)frame_index / total_frames);
    ImVec2 uv1 = ImVec2(1.0f, (float)(frame_index + 1) / total_frames);

    // Disegna l'immagine del knob
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)texture_id,
                                         bb.Min, ImVec2(bb.Min.x + knob_size_pixels.x, bb.Min.y + knob_size_pixels.y),
                                         uv0, uv1);

    // Disegna il label sotto il knob (centrato)
    ImGui::SetCursorScreenPos(ImVec2(bb.Min.x, bb.Min.y + knob_size_pixels.y + style.ItemInnerSpacing.y));
    ImGui::Text(label);

    // Disegna il valore corrente sotto il label (se specificato)
    char value_buf[64];
    ImFormatString(value_buf, IM_ARRAYSIZE(value_buf), format, *p_value);
    ImGui::SetCursorScreenPos(ImVec2(bb.Min.x, bb.Min.y + knob_size_pixels.y + style.ItemInnerSpacing.y + ImGui::GetTextLineHeight()));
    ImGui::Text(value_buf);

    return value_changed;
}


// =========================================================================
// Struttura dello Stato della UI
// =========================================================================
typedef struct {
    LV2_UI_Write_Function write_function;
    LV2_UI_Controller controller;
    LV2_Log_Logger* logger;
    LV2_URID_Map* map;
    LV2_URID_Unmap* unmap;

    Display* display;
    Window window;
    GLXContext glx_context;

    bool imgui_initialized;

    // --- Valori dei Parametri (sincronizzati con il plugin audio) ---
    float peakReduction_val;
    float gain_val;
    bool bypass_val;
    bool ratioMode_val; // false=Comp, true=Limit
    float valveDrive_val;
    bool inputPad10dB_val;
    bool oversamplingOn_val;
    bool sidechainMode_val;
    bool scLpOn_val;
    float scLpFq_val;
    float scLpQ_val;
    bool scHpOn_val;
    float scHpFq_val;
    float scHpQ_val;

    // --- Valori dei Meter (ricevuti dal plugin, normalizzati 0.0-1.0) ---
    float peakGR_val;
    float peakInL_val;
    float peakInR_val;
    float peakOutL_val;
    float peakOutR_val;

    bool showOutputMeter; // Toggle per mostrare input/output sul meter principale

    // --- Texture IDs per i Knob (GL_TEXTURE_2D IDs) ---
    GLuint knobTextureID_peakReduction;
    GLuint knobTextureID_gain;
    GLuint knobTextureID_valveDrive;
    GLuint knobTextureID_scLpFq;
    GLuint knobTextureID_scHpFq;

    // --- Dimensione e numero di frame per i Knob ---
    // Assumiamo che tutti i knob usino sprite sheets con frame quadrati della stessa dimensione
    int knobFrameWidth;
    int knobFrameHeight; // Sarà uguale a knobFrameWidth se i frame sono quadrati
    int knobTotalFrames; // Numero totale di frame in uno sprite sheet (es. 100)

    // --- Texture IDs per i Pulsanti Toggle (se usi immagini) ---
    GLuint toggleSwitchTextureID_on;
    GLuint toggleSwitchTextureID_off;
    int toggleSwitchWidth;
    int toggleSwitchHeight;


} Gla2aUI;


// =========================================================================
// Callbacks LV2 (instantiate, cleanup, port_event, ui_idle)
// =========================================================================

static LV2_UI_Handle instantiate(const LV2_UI_Descriptor* descriptor,
                                 const char* plugin_uri,
                                 const char* bundle_path,
                                 LV2_UI_Write_Function    write_function,
                                 LV2_UI_Controller        controller,
                                 LV2_UI_Widget_Handle* widget,
                                 const LV2_Feature* const* features) {
    Gla2aUI* ui = (Gla2aUI*)calloc(1, sizeof(Gla2aUI));
    ui->write_function = write_function;
    ui->controller = controller;
    ui->imgui_initialized = false;
    ui->showOutputMeter = true;

    // Inizializza i valori predefiniti dei parametri (devono corrispondere a quelli del plugin)
    ui->peakReduction_val = -20.0f;
    ui->gain_val = 0.0f;
    ui->bypass_val = false;
    ui->ratioMode_val = false;
    ui->valveDrive_val = 0.5f;
    ui->inputPad10dB_val = false;
    ui->oversamplingOn_val = true;
    ui->sidechainMode_val = false;
    ui->scLpOn_val = false;
    ui->scLpFq_val = 2000.0f;
    ui->scLpQ_val = 0.707f;
    ui->scHpOn_val = false;
    ui->scHpFq_val = 100.0f;
    ui->scHpQ_val = 0.707f;

    // Inizializza i valori dei meter (0.0 = -Inf dB, usa -40dB o simile come base)
    ui->peakGR_val = 0.0f;
    ui->peakInL_val = 0.0f;
    ui->peakInR_val = 0.0f;
    ui->peakOutL_val = 0.0f;
    ui->peakOutR_val = 0.0f;

    // Estrai le feature necessarie
    for (int i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            ui->map = (LV2_URID_Map*)features[i]->data;
            urid_map = *ui->map;
        } else if (!strcmp(features[i]->URI, LV2_URID__unmap)) {
            ui->unmap = (LV2_URID_Unmap*)features[i]->data;
            urid_unmap = *ui->unmap;
        } else if (!strcmp(features[i]->URI, LV2_LOG__log)) {
            ui->logger = (LV2_Log_Logger*)features[i]->data;
            lv2_log_logger_set_log_level(ui->logger, LV2_LOG_Trace);
            logger = *ui->logger;
        } else if (!strcmp(features[i]->URI, LV2_UI__parent)) {
            ui->window = (Window)(uintptr_t)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_UI__X11Display)) {
            ui->display = (Display*)features[i]->data;
        }
    }

    if (!ui->map || !ui->display || !ui->window) {
        lv2_log_error(&logger, "Gla2a UI: Missing required features (URID map, X11 Display, Parent window).\n");
        free(ui);
        return NULL;
    }

    // Mappa gli URI dei parametri (DEVONO CORRISPONDERE agli URI nel tuo plugin TTL e codice audio)
    // Sostituisci "http://your-plugin.com/gla2a#" con l'URI effettivo del tuo plugin
    peakReduction_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#peakReduction");
    gain_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#gain");
    bypass_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#bypass");
    ratioMode_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#ratioMode");
    valveDrive_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#valveDrive");
    inputPad10dB_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#inputPad10dB");
    oversamplingOn_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#oversamplingOn");
    sidechainMode_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#sidechainMode");
    scLpOn_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#scLpOn");
    scLpFq_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#scLpFq");
    scLpQ_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#scLpQ");
    scHpOn_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#scHpOn");
    scHpFq_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#scHpFq");
    scHpQ_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#scHpQ");

    peakGR_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#peakGR");
    peakInL_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#peakInL");
    peakInR_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#peakInR");
    peakOutL_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#peakOutL");
    peakOutR_URID = ui->map->map(ui->map->handle, "http://your-plugin.com/gla2a#peakOutR");


    // --- Inizializzazione OpenGL ---
    XWindowAttributes wa;
    XGetWindowAttributes(ui->display, ui->window, &wa);

    GLint att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
    XVisualInfo* vi = glXChooseVisual(ui->display, 0, att);
    if (!vi) {
        lv2_log_error(&logger, "Gla2a UI: No appropriate visual found for OpenGL.\n");
        free(ui);
        return NULL;
    }

    ui->glx_context = glXCreateContext(ui->display, vi, NULL, GL_TRUE);
    if (!ui->glx_context) {
        lv2_log_error(&logger, "Gla2a UI: Failed to create GLX context.\n");
        XFree(vi);
        free(ui);
        return NULL;
    }
    XFree(vi);

    glXMakeCurrent(ui->display, ui->window, ui->glx_context);

    // --- Caricamento delle texture per i Knob e Toggle Switches ---
    // Assicurati che questi path siano corretti rispetto al tuo bundle LV2
    // Esempio: Gla2a.lv2/gui/assets/knob_pr.png
    std::string bundle_str(bundle_path);
    std::string assets_path = bundle_str + "/gui/assets/"; // Assumi che hai una cartella 'assets' dentro 'gui'

    // Carica il primo knob (dimensione del frame viene impostata qui)
    ui->knobTextureID_peakReduction = LoadTextureFromFile((assets_path + "knob_pr.png").c_str(), &ui->knobFrameWidth, &ui->knobFrameHeight);
    if (ui->knobTextureID_peakReduction != 0) {
        ui->knobTotalFrames = ui->knobFrameHeight / ui->knobFrameWidth; // Assumi frame quadrati in colonna
        if (ui->knobTotalFrames == 0) {
             lv2_log_error(&logger, "Gla2a UI: Knob texture 'knob_pr.png' has invalid dimensions (height not multiple of width).\n");
        }
    } else {
         lv2_log_error(&logger, "Gla2a UI: Failed to load knob_pr.png\n");
    }

    // Carica gli altri knob, riusando la stessa dimensione dei frame se sono uguali
    ui->knobTextureID_gain       = LoadTextureFromFile((assets_path + "knob_gain.png").c_str(), &ui->knobFrameWidth, &ui->knobFrameHeight);
    ui->knobTextureID_valveDrive = LoadTextureFromFile((assets_path + "knob_drive.png").c_str(), &ui->knobFrameWidth, &ui->knobFrameHeight);
    ui->knobTextureID_scLpFq     = LoadTextureFromFile((assets_path + "knob_sc_fq.png").c_str(), &ui->knobFrameWidth, &ui->knobFrameHeight);
    ui->knobTextureID_scHpFq     = LoadTextureFromFile((assets_path + "knob_sc_fq.png").c_str(), &ui->knobFrameWidth, &ui->knobFrameHeight);

    // Carica le texture per i toggle switches (es. immagini separate per ON/OFF)
    ui->toggleSwitchTextureID_on = LoadTextureFromFile((assets_path + "toggle_on.png").c_str(), &ui->toggleSwitchWidth, &ui->toggleSwitchHeight);
    ui->toggleSwitchTextureID_off = LoadTextureFromFile((assets_path + "toggle_off.png").c_str(), &ui->toggleSwitchWidth, &ui->toggleSwitchHeight);
    if (ui->toggleSwitchTextureID_on == 0 || ui->toggleSwitchTextureID_off == 0) {
        lv2_log_error(&logger, "Gla2a UI: Failed to load toggle switch textures.\n");
    }


    // --- Inizializzazione ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Richiede un host che gestisce le finestre di ImGui

    // Imposta lo stile di ImGui (personalizzato per un look più "hardware")
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.WindowRounding = 4.0f;
    style.ScrollbarRounding = 9.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.53f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 0.80f, 0.00f, 1.00f); // Spunta verde per checkbox
    style.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f); // Bordo scuro

    ImGui_ImplOpenGL3_Init("#version 130");

    ui->imgui_initialized = true;

    *widget = (LV2_UI_Widget_Handle)ui->window;
    return (LV2_UI_Handle)ui;
}

static void cleanup(LV2_UI_Handle ui_handle) {
    Gla2aUI* ui = (Gla2aUI*)ui_handle;

    if (ui->imgui_initialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }

    // Libera le texture OpenGL
    if (ui->knobTextureID_peakReduction) glDeleteTextures(1, &ui->knobTextureID_peakReduction);
    if (ui->knobTextureID_gain) glDeleteTextures(1, &ui->knobTextureID_gain);
    if (ui->knobTextureID_valveDrive) glDeleteTextures(1, &ui->knobTextureID_valveDrive);
    if (ui->knobTextureID_scLpFq) glDeleteTextures(1, &ui->knobTextureID_scLpFq);
    if (ui->knobTextureID_scHpFq) glDeleteTextures(1, &ui->knobTextureID_scHpFq);
    if (ui->toggleSwitchTextureID_on) glDeleteTextures(1, &ui->toggleSwitchTextureID_on);
    if (ui->toggleSwitchTextureID_off) glDeleteTextures(1, &ui->toggleSwitchTextureID_off);


    if (ui->glx_context) {
        glXMakeCurrent(ui->display, None, NULL);
        glXDestroyContext(ui->display, ui->glx_context);
    }
    free(ui);
}

// Funzione di gestione degli eventi X11 per ImGui
// QUESTA È LA PARTE PIÙ CRITICA: un host LV2 DEVE inoltrare gli eventi.
// Questa implementazione assume un polling manuale degli eventi se l'host non li inoltra.
// Una vera implementazione LV2 dovrebbe usare LV2_UI__X11_Widget_Event se supportato.
static int handle_xevent(Gla2aUI* ui, XEvent* event) {
    ImGuiIO& io = ImGui::GetIO();

    switch (event->type) {
        case MotionNotify:
            io.AddMousePosEvent((float)event->xmotion.x, (float)event->xmotion.y);
            break;
        case ButtonPress:
        case ButtonRelease:
            if (event->xbutton.button >= 1 && event->xbutton.button <= 5) {
                io.AddMouseButtonEvent(event->xbutton.button - 1, event->xbutton.type == ButtonPress);
            }
            break;
        case KeyPress:
        case KeyRelease:
            // Una mappatura Keysym a ImGuiKey è necessaria per gestire tutti i tasti.
            // Per brevità, gestiamo solo i modificatori e alcuni tasti di base.
            io.AddKeyEvent(ImGuiMod_Ctrl, event->xkey.state & ControlMask);
            io.AddKeyEvent(ImGuiMod_Shift, event->xkey.state & ShiftMask);
            io.AddKeyEvent(ImGuiMod_Alt, event->xkey.state & Mod1Mask);
            io.AddKeyEvent(ImGuiMod_Super, event->xkey.state & Mod4Mask); // Super/Windows key

            KeySym key_sym = XLookupKeysym(&event->xkey, 0);
            // Esempio di mappatura di un tasto specifico (A)
            if (key_sym == XK_A) {
                io.AddKeyEvent(ImGuiKey_A, event->xkey.type == KeyPress);
            }
            // Altri tasti...
            // io.AddKeyEvent(ImGuiKey_Space, key_sym == XK_space);
            // io.AddKeyEvent(ImGuiKey_Enter, key_sym == XK_Return);
            break;
        case ConfigureNotify: // Finestra ridimensionata
            // Aggiorna la dimensione del display in ImGui
            io.DisplaySize = ImVec2((float)event->xconfigure.width, (float)event->xconfigure.height);
            break;
        default:
            return 0; // Evento non gestito
    }
    return 1; // Evento gestito (o parzialmente gestito)
}

static void draw_ui(Gla2aUI* ui) {
    if (!ui->imgui_initialized) return;

    glXMakeCurrent(ui->display, ui->window, ui->glx_context);

    ImGuiIO& io = ImGui::GetIO();

    // Aggiorna il tempo e la dimensione del display prima di iniziare un nuovo frame
    io.DeltaTime = (float)(get_time_in_seconds() - io.CurrentCaptureLifetime);
    io.CurrentCaptureLifetime = get_time_in_seconds();

    // Gestisci gli eventi X11 in coda (polling). Questa è una soluzione di ripiego.
    // L'host dovrebbe idealmente inoltrare gli eventi tramite LV2_UI__X11_Widget_Event.
    XEvent event;
    while (XPending(ui->display)) {
        XNextEvent(ui->display, &event);
        if (event.xany.window == ui->window) {
            handle_xevent(ui, &event);
        }
    }

    // Inizia un nuovo frame ImGui
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // Ottieni le dimensioni della finestra per il layout
    XWindowAttributes wa;
    XGetWindowAttributes(ui->display, ui->window, &wa);
    float window_width = (float)wa.width;
    float window_height = (float)wa.height;

    // Imposta la finestra principale di ImGui (che copre l'intera UI)
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height));
    ImGui::Begin("Gla2a Compressor", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);

    // =====================================================================
    // Layout della UI con Tabs
    // =====================================================================
    if (ImGui::BeginTabBar("MyTabs"))
    {
        // --- Tab Principale ---
        if (ImGui::BeginTabItem("Main"))
        {
            ImGui::Columns(2, "MainLayout", false);
            ImGui::SetColumnWidth(0, window_width * 0.6f);
            ImGui::Text("Main Controls");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10)); // Spazio

            ImVec2 knob_img_size = ImVec2((float)ui->knobFrameWidth, (float)ui->knobFrameWidth); // Assumi knob quadrati

            // Knob per Peak Reduction
            ImGui::PushID("PeakReduction");
            if (KnobRotaryImage("Peak Reduction", &ui->peakReduction_val, -60.0f, -10.0f,
                                ui->knobTextureID_peakReduction, ui->knobFrameWidth, ui->knobFrameWidth,
                                ui->knobTotalFrames, knob_img_size, "%.1f dB")) {
                ui->write_function(ui->controller, peakReduction_URID, sizeof(float), 0, &ui->peakReduction_val);
            }
            ImGui::PopID();
            ImGui::SameLine(0, 20); // Spazio tra i knob

            // Knob per Gain Out
            ImGui::PushID("Gain");
            if (KnobRotaryImage("Gain Out", &ui->gain_val, 0.0f, 12.0f,
                                ui->knobTextureID_gain, ui->knobFrameWidth, ui->knobFrameWidth,
                                ui->knobTotalFrames, knob_img_size, "%.1f dB")) {
                ui->write_function(ui->controller, gain_URID, sizeof(float), 0, &ui->gain_val);
            }
            ImGui::PopID();

            ImGui::Dummy(ImVec2(0, 20)); // Spazio

            // Toggle Button (Levette) per Input Pad
            ImGui::Text("Input Pad -10dB");
            ImGui::SameLine();
            ImGui::PushID("InputPad");
            ImTextureID toggle_tex_id = ui->inputPad10dB_val ? (ImTextureID)(intptr_t)ui->toggleSwitchTextureID_on : (ImTextureID)(intptr_t)ui->toggleSwitchTextureID_off;
            if (ImGui::ImageButton("##InputPadBtn", toggle_tex_id, ImVec2((float)ui->toggleSwitchWidth, (float)ui->toggleSwitchHeight))) {
                ui->inputPad10dB_val = !ui->inputPad10dB_val;
                float val = ui->inputPad10dB_val ? 1.0f : 0.0f;
                ui->write_function(ui->controller, inputPad10dB_URID, sizeof(float), 0, &val);
            }
            ImGui::PopID();

            // Toggle Button per Ratio Mode
            ImGui::Text("Ratio Mode");
            ImGui::SameLine();
            ImGui::PushID("RatioMode");
            toggle_tex_id = ui->ratioMode_val ? (ImTextureID)(intptr_t)ui->toggleSwitchTextureID_on : (ImTextureID)(intptr_t)ui->toggleSwitchTextureID_off;
            if (ImGui::ImageButton("##RatioModeBtn", toggle_tex_id, ImVec2((float)ui->toggleSwitchWidth, (float)ui->toggleSwitchHeight))) {
                ui->ratioMode_val = !ui->ratioMode_val;
                float val = ui->ratioMode_val ? 1.0f : 0.0f;
                ui->write_function(ui->controller, ratioMode_URID, sizeof(float), 0, &val);
            }
            ImGui::SameLine(); ImGui::Text(ui->ratioMode_val ? "(Limit)" : "(Comp)");
            ImGui::PopID();

            ImGui::Dummy(ImVec2(0, 20)); // Spazio

            // Knob per Valve Drive
            ImGui::PushID("ValveDrive");
            if (KnobRotaryImage("Valve Drive", &ui->valveDrive_val, 0.0f, 1.0f,
                                ui->knobTextureID_valveDrive, ui->knobFrameWidth, ui->knobFrameWidth,
                                ui->knobTotalFrames, knob_img_size, "%.2f")) {
                ui->write_function(ui->controller, valveDrive_URID, sizeof(float), 0, &ui->valveDrive_val);
            }
            ImGui::PopID();

            ImGui::Dummy(ImVec2(0, 20)); // Spazio

            // Pulsante normale per Bypass
            ImGui::PushID("Bypass");
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetColumnWidth(0) - ImGui::CalcTextSize("Bypass").x - ImGui::CalcTextSize("OFF").x - ImGui::GetStyle().ItemSpacing.x * 2 - ImGui::GetStyle().FramePadding.x * 2) / 2); // Centra il pulsante
            if (ImGui::Button(ui->bypass_val ? "BYPASS ON" : "BYPASS OFF", ImVec2(100, 30))) {
                ui->bypass_val = !ui->bypass_val;
                float val = ui->bypass_val ? 1.0f : 0.0f;
                ui->write_function(ui->controller, bypass_URID, sizeof(float), 0, &val);
            }
            ImGui::PopID();


            ImGui::NextColumn(); // Passa alla seconda colonna (Meters)

            // Colonna 2: Meter e I/O
            ImGui::SetColumnWidth(1, window_width * 0.4f);
            ImGui::Text("Meters");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10)); // Spazio

            // VU Meter di Gain Reduction
            ImGui::Text("Gain Reduction (dB)");
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Rosso per GR
            // Normalizza il valore GR da dB a 0.0-1.0 per la progress bar
            // Assumi che il plugin invii GR in dB (es. 0 a -30), converti per la barra (0.0 a 1.0)
            float normalized_gr = 1.0f - ImClamp(ui->peakGR_val / -30.0f, 0.0f, 1.0f); // 0dB = 0, -30dB = 1
            ImGui::ProgressBar(normalized_gr, ImVec2(window_width * 0.35f, 100), ""); // Altezza fissa per meter
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 20)); // Spazio


            // Switch I/O per il meter principale (testo e checkbox standard)
            ImGui::Text("Show Output Meter");
            ImGui::SameLine();
            if (ImGui::Checkbox("##ShowOutputMeter", &ui->showOutputMeter)) {
                // Non c'è un parametro LV2 per questo, è solo un toggle della UI
            }
            ImGui::SameLine(); ImGui::Text(ui->showOutputMeter ? "(Output)" : "(Input)");

            // VU Meter I/O principale (Input L/R o Output L/R)
            ImGui::Text("Input/Output Peak (dB)");
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.0f, 0.8f, 0.0f, 1.0f)); // Verde
            float currentIOMeterValL = ui->showOutputMeter ? ui->peakOutL_val : ui->peakInL_val;
            float currentIOMeterValR = ui->showOutputMeter ? ui->peakOutR_val : ui->peakInR_val;
            // Converti dB a 0.0-1.0 (es. -60dB a 0dB)
            float normalized_in_out_L = ImClamp((currentIOMeterValL + 60.0f) / 60.0f, 0.0f, 1.0f);
            float normalized_in_out_R = ImClamp((currentIOMeterValR + 60.0f) / 60.0f, 0.0f, 1.0f);

            ImGui::ProgressBar(normalized_in_out_L, ImVec2(window_width * 0.35f, 50), "L"); // "L" come overlay testuale
            ImGui::ProgressBar(normalized_in_out_R, ImVec2(window_width * 0.35f, 50), "R"); // "R" come overlay testuale
            ImGui::PopStyleColor();


            ImGui::Columns(1); // Torna a una singola colonna
            ImGui::EndTabItem();
        }

        // --- Tab Sidechain ---
        if (ImGui::BeginTabItem("Sidechain"))
        {
            ImGui::Text("Sidechain Controls");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10)); // Spazio

            ImVec2 knob_img_size_small = ImVec2((float)ui->knobFrameWidth * 0.7f, (float)ui->knobFrameWidth * 0.7f); // Knob più piccoli

            // Oversampling
            if (ImGui::Checkbox("Oversampling On", &ui->oversamplingOn_val)) {
                float val = ui->oversamplingOn_val ? 1.0f : 0.0f;
                ui->write_function(ui->controller, oversamplingOn_URID, sizeof(float), 0, &val);
            }

            // Sidechain Mode
            if (ImGui::Checkbox("External Sidechain", &ui->sidechainMode_val)) {
                float val = ui->sidechainMode_val ? 1.0f : 0.0f;
                ui->write_function(ui->controller, sidechainMode_URID, sizeof(float), 0, &val);
            }

            ImGui::Dummy(ImVec2(0, 20)); // Spazio

            // Filtri Sidechain (HP/LP)
            ImGui::Columns(2, "SidechainFilters", false);

            ImGui::Text("HP Filter");
            if (ImGui::Checkbox("HP On", &ui->scHpOn_val)) {
                float val = ui->scHpOn_val ? 1.0f : 0.0f;
                ui->write_function(ui->controller, scHpOn_URID, sizeof(float), 0, &val);
            }
            ImGui::PushID("HpFreq");
            if (KnobRotaryImage("Freq", &ui->scHpFq_val, 20.0f, 20000.0f,
                                ui->knobTextureID_scHpFq, ui->knobFrameWidth, ui->knobFrameWidth,
                                ui->knobTotalFrames, knob_img_size_small, "%.0f Hz")) {
                ui->write_function(ui->controller, scHpFq_URID, sizeof(float), 0, &ui->scHpFq_val);
            }
            ImGui::PopID();
            ImGui::PushID("HpQ");
            if (KnobRotaryImage("Q", &ui->scHpQ_val, 0.1f, 10.0f,
                                ui->knobTextureID_scHpQ, ui->knobFrameWidth, ui->knobFrameWidth,
                                ui->knobTotalFrames, knob_img_size_small, "%.2f")) {
                ui->write_function(ui->controller, scHpQ_URID, sizeof(float), 0, &ui->scHpQ_val);
            }
            ImGui::PopID();

            ImGui::NextColumn();

            ImGui::Text("LP Filter");
            if (ImGui::Checkbox("LP On", &ui->scLpOn_val)) {
                float val = ui->scLpOn_val ? 1.0f : 0.0f;
                ui->write_function(ui->controller, scLpOn_URID, sizeof(float), 0, &val);
            }
            ImGui::PushID("LpFreq");
            if (KnobRotaryImage("Freq", &ui->scLpFq_val, 20.0f, 20000.0f,
                                ui->knobTextureID_scLpFq, ui->knobFrameWidth, ui->knobFrameWidth,
                                ui->knobTotalFrames, knob_img_size_small, "%.0f Hz")) {
                ui->write_function(ui->controller, scLpFq_URID, sizeof(float), 0, &ui->scLpFq_val);
            }
            ImGui::PopID();
            ImGui::PushID("LpQ");
            if (KnobRotaryImage("Q", &ui->scLpQ_val, 0.1f, 10.0f,
                                ui->knobTextureID_scLpQ, ui->knobFrameWidth, ui->knobFrameWidth,
                                ui->knobTotalFrames, knob_img_size_small, "%.2f")) {
                ui->write_function(ui->controller, scLpQ_URID, sizeof(float), 0, &ui->scLpQ_val);
            }
            ImGui::PopID();


            ImGui::Columns(1);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End(); // Fine della finestra principale di ImGui

    // Rendering ImGui
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f); // Sfondo grigio scuro
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Se usi viewports multipli (per finestre popup o docking)
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    glXSwapBuffers(ui->display, ui->window); // Swap del buffer per mostrare il frame
}

static void port_event(LV2_UI_Handle handle, LV2_URID port_urid, uint32_t buffer_size, uint32_t format, const void* buffer) {
    Gla2aUI* ui = (Gla2aUI*)handle;

    // Se stai usando LV2_Atom, qui dovresti deserializzare gli atom.
    // Per ora, assumiamo che i float vengano inviati direttamente per i parametri e i meter.
    if (format == 0) { // LV2_Atom_Port_Float (formato predefinito per float in LV2 UI)
        // Aggiorna lo stato locale della UI quando il plugin invia un aggiornamento
        if (port_urid == peakReduction_URID) {
            ui->peakReduction_val = *(const float*)buffer;
        } else if (port_urid == gain_URID) {
            ui->gain_val = *(const float*)buffer;
        } else if (port_urid == bypass_URID) {
            ui->bypass_val = (*(const float*)buffer != 0.0f);
        } else if (port_urid == ratioMode_URID) {
            ui->ratioMode_val = (*(const float*)buffer != 0.0f);
        } else if (port_urid == valveDrive_URID) {
            ui->valveDrive_val = *(const float*)buffer;
        } else if (port_urid == inputPad10dB_URID) {
            ui->inputPad10dB_val = (*(const float*)buffer != 0.0f);
        } else if (port_urid == oversamplingOn_URID) {
            ui->oversamplingOn_val = (*(const float*)buffer != 0.0f);
        } else if (port_urid == sidechainMode_URID) {
            ui->sidechainMode_val = (*(const float*)buffer != 0.0f);
        } else if (port_urid == scLpOn_URID) {
            ui->scLpOn_val = (*(const float*)buffer != 0.0f);
        } else if (port_urid == scLpFq_URID) {
            ui->scLpFq_val = *(const float*)buffer;
        } else if (port_urid == scLpQ_URID) {
            ui->scLpQ_val = *(const float*)buffer;
        } else if (port_urid == scHpOn_URID) {
            ui->scHpOn_val = (*(const float*)buffer != 0.0f);
        } else if (port_urid == scHpFq_URID) {
            ui->scHpFq_val = *(const float*)buffer;
        } else if (port_urid == scHpQ_URID) {
            ui->scHpQ_val = *(const float*)buffer;
        }
        // Aggiorna i valori dei meter (il plugin dovrebbe inviarli regolarmente)
        else if (port_urid == peakGR_URID) {
            ui->peakGR_val = *(const float*)buffer;
        } else if (port_urid == peakInL_URID) {
            ui->peakInL_val = *(const float*)buffer;
        } else if (port_urid == peakInR_URID) {
            ui->peakInR_val = *(const float*)buffer;
        } else if (port_urid == peakOutL_URID) {
            ui->peakOutL_val = *(const float*)buffer;
        } else if (port_urid == peakOutR_URID) {
            ui->peakOutR_val = *(const float*)buffer;
        }
    }
    // Richiede un ridisegno della UI dopo ogni aggiornamento del parametro
    draw_ui(ui);
}


static int ui_extension_data(const char* uri) {
    if (!strcmp(uri, LV2_UI__idleInterface)) {
        return 0; // Supporta l'interfaccia idle
    }
    // Se supporti l'inoltro diretto degli eventi X11 da parte dell'host, lo dichiereresti qui:
    // if (!strcmp(uri, LV2_UI__X11_Widget_Event)) {
    //     return 0;
    // }
    return 0;
}

static void ui_idle(LV2_UI_Handle handle) {
    Gla2aUI* ui = (Gla2aUI*)handle;
    draw_ui(ui); // Ridisegna la UI in modalità idle
}

static const LV2_UI_Idle_Interface idle_iface = { ui_idle };

// Funzione per LV2_UI__X11_Widget_Event (se l'host la supporta)
// Non implementata qui per semplicità, se la tua DAW la supporta, è da preferire.
// static int ui_x11_event(LV2_UI_Handle handle, XEvent* event) {
//    Gla2aUI* ui = (Gla2aUI*)handle;
//    return handle_xevent(ui, event); // Passa l'evento all'handler di ImGui
// }
// static const LV2_UI_X11_Widget_Event_Interface x11_event_iface = { ui_x11_event };


static const void* ui_extension_data_interface(const char* uri) {
    if (!strcmp(uri, LV2_UI__idleInterface)) {
        return &idle_iface;
    }
    // if (!strcmp(uri, LV2_UI__X11_Widget_Event)) {
    //     return &x11_event_iface;
    // }
    return NULL;
}


// =========================================================================
// Descrittori della UI
// =========================================================================
static const LV2_UI_Descriptor descriptors[] = {
    {
        "http://your-plugin.com/gla2a-ui", // URI della UI (DEVE ESSERE DIVERSO dal plugin audio)
        instantiate,
        cleanup,
        port_event,
        ui_extension_data_interface // Puntatore alla funzione di estensione
    }
};

const LV2_UI_Descriptor* lv2_ui_descriptor(uint32_t index) {
    if (index == 0) return &descriptors[0];
    return NULL;
}
