#include <lv2/ui/ui.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/atom-forge.h>
#include <lv2/urid/urid.h>
#include <lv2/core/lv2.h>
#include <lv2/ui/parent.h>

#include <wx/wx.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/notebook.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/timer.h> // Per animare la valvola
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <cmath>

// URI unico della tua GUI (deve corrispondere a quello nel TTL)
#define GLA2A_GUI_URI "http://gregorioferraris.github.io/lv2/gla2a#x11gui"

// Definizione degli indici delle porte (DEVE essere identica al tuo gla2a.cpp e gla2a.ttl)
typedef enum {
    GLA2A_CONTROL_GAIN_IN           = 0,
    GLA2A_CONTROL_GAIN_OUT          = 1,
    GLA2A_CONTROL_THRESHOLD         = 2, // Rinominato da PEAK_REDUCTION
    GLA2A_CONTROL_MODE              = 3, // Questo non sarà più usato direttamente se integrato in Ratio
    GLA2A_CONTROL_SIDECHAIN_MODE    = 4,
    GLA2A_AUDIO_EXTERNAL_SIDECHAIN_IN = 5,
    GLA2A_CONTROL_RATIO_SELECT      = 6,
    GLA2A_CONTROL_AR_PRESET         = 7,
    GLA2A_CONTROL_TUBE_DRIVE        = 8,
    GLA2A_AUDIO_IN_L                = 9,
    GLA2A_AUDIO_IN_R                = 10,
    GLA2A_AUDIO_OUT_L               = 11,
    GLA2A_AUDIO_OUT_R               = 12,
    GLA2A_CONTROL_OVERSAMPLING_MODE = 13,
    GLA2A_CONTROL_SIDECHAIN_HPF_FREQ = 14,
    GLA2A_CONTROL_SIDECHAIN_LPF_FREQ = 15,
    GLA2A_CONTROL_SIDECHAIN_HPF_Q   = 16,
    GLA2A_CONTROL_SIDECHAIN_LPF_Q   = 17,
    GLA2A_CONTROL_SIDECHAIN_MONITOR_MODE = 18,
    GLA2A_CONTROL_INPUT_ATTENUATOR  = 19,
    GLA2A_CONTROL_GAIN_REDUCTION    = 20, // Nuova manopola Gain Reduction
    GLA2A_PEAK_OUT_L                = 21, // Per i VU meter
    GLA2A_PEAK_IN_L                 = 22, // Per il VU meter I/O
    GLA2A_PEAK_GR                   = 23  // Per il VU meter GR
} PortIndex;


// Colori della GUI
const wxColour LA2A_BEIGE_COLOR(230, 210, 180);
const wxColour LA2A_ORANGE_VUMETER_COLOR(255, 165, 0);
const wxColour LA2A_VALVE_OFF_COLOR(50, 0, 0);
const wxColour LA2A_VALVE_ON_COLOR(255, 100, 0); // Bagliore arancione/rosso

// --- CLASSE CUSTOM PER MANOPOLE (Knob) ---
class CustomKnob : public wxControl {
public:
    CustomKnob(wxWindow* parent, wxWindowID id, float min_val, float max_val, float default_val,
               const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0)
        : wxControl(parent, id, pos, size, style),
          minValue(min_val), maxValue(max_val), currentValue(default_val) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour()); // Usa lo sfondo del pannello padre

        Bind(wxEVT_PAINT, &CustomKnob::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &CustomKnob::OnMouseLeftDown, this);
        Bind(wxEVT_MOTION, &CustomKnob::OnMouseMove, this);
        Bind(wxEVT_LEFT_UP, &CustomKnob::OnMouseLeftUp, this);
    }

    void SetValue(float val) {
        if (val < minValue) val = minValue;
        if (val > maxValue) val = maxValue;
        if (currentValue != val) {
            currentValue = val;
            Refresh();
        }
    }

    float GetValue() const { return currentValue; }

    void SetLabels(const std::vector<wxString>& labels) {
        discreteLabels = labels;
        Refresh();
    }

private:
    float minValue;
    float maxValue;
    float currentValue;
    bool isDragging = false;
    wxPoint lastDragPos;
    std::vector<wxString> discreteLabels;

    void OnPaint(wxPaintEvent& event) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        wxSize clientSize = GetClientSize();
        int cx = clientSize.GetWidth() / 2;
        int cy = clientSize.GetHeight() / 2;
        int radius = std::min(cx, cy) - 5;

        dc.SetBrush(wxBrush(wxColour(50, 50, 50)));
        dc.SetPen(wxPen(wxColour(30, 30, 30), 1));
        dc.DrawCircle(cx, cy, radius);

        float normalizedValue = (currentValue - minValue) / (maxValue - minValue);
        float startAngle = M_PI * 1.75f; // -135 gradi (a ore 7:30)
        float endAngle = M_PI * 0.25f; // 135 gradi (a ore 4:30)
        float angleRange = endAngle - startAngle;
        if (angleRange < 0) angleRange += M_PI * 2.0f;

        float currentAngle = startAngle + (normalizedValue * angleRange);

        dc.SetPen(wxPen(*wxWHITE, 2));
        dc.DrawLine(cx + radius * 0.2 * cos(currentAngle),
                    cy + radius * 0.2 * sin(currentAngle),
                    cx + radius * 0.8 * cos(currentAngle),
                    cy + radius * 0.8 * sin(currentAngle));

        if (!discreteLabels.empty()) {
            dc.SetPen(wxPen(*wxLIGHT_GREY, 1));
            for (size_t i = 0; i < discreteLabels.size(); ++i) {
                float discreteNormalizedValue = (float)i / (discreteLabels.size() - 1);
                float discreteAngle = startAngle + (discreteNormalizedValue * angleRange);
                dc.DrawLine(cx + radius * 0.85 * cos(discreteAngle),
                            cy + radius * 0.85 * sin(discreteAngle),
                            cx + radius * 0.95 * cos(discreteAngle),
                            cy + radius * 0.95 * sin(discreteAngle));
            }
        }
    }

    void OnMouseLeftDown(wxMouseEvent& event) {
        isDragging = true;
        lastDragPos = event.GetPosition();
        CaptureMouse();
    }

    void OnMouseMove(wxMouseEvent& event) {
        if (isDragging && event.Dragging()) {
            wxPoint currentPos = event.GetPosition();
            int deltaY = lastDragPos.y - currentPos.y;
            float sensitivity = (maxValue - minValue) / (GetClientSize().GetHeight() * 1.5f);
            currentValue += deltaY * sensitivity;

            SetValue(currentValue);

            lastDragPos = currentPos;

            wxCommandEvent evt(wxEVT_SLIDER, GetId());
            evt.SetEventObject(this);
            evt.SetDouble(currentValue);
            GetParent()->ProcessWindowEvent(evt);
        }
    }

    void OnMouseLeftUp(wxMouseEvent& event) {
        isDragging = false;
        if (HasCapture()) ReleaseMouse();
    }
};

// --- CLASSE CUSTOM PER VU METER ---
class CustomVUMeter : public wxControl {
public:
    CustomVUMeter(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0)
        : wxControl(parent, id, pos, size, style), meterValue(0.0f) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour());
        Bind(wxEVT_PAINT, &CustomVUMeter::OnPaint, this);
    }

    void SetMeterValue(float val) {
        if (meterValue != val) {
            meterValue = val;
            Refresh();
        }
    }

private:
    float meterValue;

    void OnPaint(wxPaintEvent& event) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        wxSize clientSize = GetClientSize();
        int cx = clientSize.GetWidth() / 2;
        int cy = clientSize.GetHeight() * 0.7; // Centro più in basso per lasciare spazio alla scala
        int radius = std::min(clientSize.GetWidth() / 2, (int)(clientSize.GetHeight() * 0.6)) - 5;

        wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
        if (gc) {
            gc->SetPen(wxPen(*wxBLACK, 2));
            gc->SetBrush(wxBrush(LA2A_ORANGE_VUMETER_COLOR));
            gc->DrawEllipse(cx - radius, cy - radius, radius * 2, radius * 2);

            gc->SetBrush(wxBrush(GetBackgroundColour()));
            gc->SetPen(wxTransparentPen);
            gc->DrawRectangle(cx - radius - 1, cy, radius * 2 + 2, radius + 2);
            delete gc;
        } else {
            dc.SetBrush(wxBrush(LA2A_ORANGE_VUMETER_COLOR));
            dc.SetPen(wxPen(*wxBLACK, 2));
            dc.DrawCircle(cx, cy, radius);
        }

        // Scala del VU meter (es. -20dB a +10dB)
        float minDb = -20.0f;
        float maxDb = 10.0f;
        float rangeDb = maxDb - minDb;

        float startAngleRad = M_PI * 0.75f; // Inizio scala (ore 10:30 circa)
        float endAngleRad = M_PI * 0.25f;  // Fine scala (ore 1:30 circa)
        float totalAngleRange = startAngleRad - endAngleRad;

        dc.SetTextForeground(*wxBLACK);
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));

        float dbMarks[] = {-20.0f, -10.0f, -5.0f, 0.0f, 5.0f, 10.0f};
        for (float db : dbMarks) {
            float normalized = (db - minDb) / rangeDb;
            float angle = startAngleRad - (normalized * totalAngleRange);

            int markLength = (db == 0.0f) ? 12 : 8; // Tacca più lunga per lo 0dB
            wxPoint p1(cx + (int)(radius * cos(angle)), cy - (int)(radius * sin(angle)));
            wxPoint p2(cx + (int)((radius - markLength) * cos(angle)), cy - (int)((radius - markLength) * sin(angle)));
            dc.SetPen(wxPen(*wxBLACK, 1));
            dc.DrawLine(p1, p2);

            wxString dbText = wxString::Format("%.0f", db);
            wxSize textSize = dc.GetTextExtent(dbText);
            // Posiziona il testo un po' più lontano dalla tacca
            wxPoint textPos(cx + (int)((radius - markLength - textSize.GetWidth() / 2 - 5) * cos(angle)) - textSize.GetWidth() / 2,
                            cy - (int)((radius - markLength - textSize.GetHeight() / 2 - 5) * sin(angle)) - textSize.GetHeight() / 2);

            dc.DrawText(dbText, textPos);
        }

        // Lancetta
        float normalizedMeterValue = (meterValue - minDb) / rangeDb;
        if (normalizedMeterValue < 0) normalizedMeterValue = 0;
        if (normalizedMeterValue > 1) normalizedMeterValue = 1;
        float needleAngle = startAngleRad - (normalizedMeterValue * totalAngleRange);

        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.DrawLine(cx, cy,
                    cx + radius * 0.9 * cos(needleAngle),
                    cy - radius * 0.9 * sin(needleAngle));

        // Perno centrale
        dc.SetBrush(wxBrush(*wxBLACK));
        dc.DrawCircle(cx, cy, 5);
    }
};

// --- CLASSE CUSTOM PER TOGGLE SWITCH (per Oversampling, Sidechain Source) ---
class CustomToggleSwitch : public wxControl {
public:
    CustomToggleSwitch(wxWindow* parent, wxWindowID id, const wxString& label, bool initialState = false,
                       const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize)
        : wxControl(parent, id, pos, size, wxNO_BORDER),
          isOn(initialState),
          textLabel(label) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour());
        Bind(wxEVT_PAINT, &CustomToggleSwitch::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &CustomToggleSwitch::OnMouseLeftDown, this);
    }

    bool IsOn() const { return isOn; }
    void SetOn(bool state) {
        if (isOn != state) {
            isOn = state;
            Refresh();
        }
    }

private:
    bool isOn;
    wxString textLabel;

    void OnPaint(wxPaintEvent& event) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        wxSize clientSize = GetClientSize();
        int width = clientSize.GetWidth();
        int height = clientSize.GetHeight();

        int rectHeight = height / 2;
        int rectY = (height - rectHeight) / 2;
        int roundness = 5;

        dc.SetPen(wxPen(wxColour(80, 80, 80), 1));
        dc.SetBrush(wxBrush(wxColour(120, 120, 120)));
        dc.DrawRoundedRectangle(0, rectY, width, rectHeight, roundness);

        int leverWidth = width * 0.6;
        int leverHeight = rectHeight * 0.8;
        int leverX = (width - leverWidth) / 2;
        int leverY;

        if (isOn) {
            leverY = rectY + rectHeight - leverHeight - 2;
        } else {
            leverY = rectY + 2;
        }

        dc.SetBrush(wxBrush(wxColour(60, 60, 60)));
        dc.DrawRoundedRectangle(leverX, leverY, leverWidth, leverHeight, roundness / 2);

        dc.SetBrush(*wxWHITE_BRUSH);
        dc.DrawCircle(leverX + leverWidth / 2, leverY + leverHeight / 2, 3);

        dc.SetTextForeground(*wxBLACK);
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        wxSize textSize = dc.GetTextExtent(textLabel);
        dc.DrawText(textLabel, (width - textSize.GetWidth()) / 2, (isOn ? 2 : height - textSize.GetHeight() - 2));
    }

    void OnMouseLeftDown(wxMouseEvent& event) {
        SetOn(!isOn);
        wxCommandEvent evt(wxEVT_CHECKBOX, GetId());
        evt.SetEventObject(this);
        evt.SetInt(isOn ? 1 : 0);
        GetParent()->ProcessWindowEvent(evt);
    }
};

// --- CLASSE CUSTOM PER VALVOLA LUMINOSA ---
class ValveLightDisplay : public wxControl {
public:
    ValveLightDisplay(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxNO_BORDER)
        : wxControl(parent, id, pos, size, style), tubeDriveValue(0.0f) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour());
        Bind(wxEVT_PAINT, &ValveLightDisplay::OnPaint, this);
    }

    void SetTubeDriveValue(float value) {
        if (tubeDriveValue != value) {
            tubeDriveValue = value;
            Refresh();
        }
    }

private:
    float tubeDriveValue; // Valore da 0.0 a 1.0

    void OnPaint(wxPaintEvent& event) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        wxSize clientSize = GetClientSize();
        int width = clientSize.GetWidth();
        int height = clientSize.GetHeight();

        // Disegna la fessura esterna (telaio metallico)
        int frameThickness = 3;
        dc.SetBrush(wxBrush(wxColour(100, 100, 100))); // Grigio scuro per il telaio
        dc.SetPen(wxPen(wxColour(80, 80, 80), 1));
        dc.DrawRoundedRectangle(0, 0, width, height, 5);

        // Disegna la "finestra" interna (leggermente rientrata)
        dc.SetBrush(wxBrush(LA2A_VALVE_OFF_COLOR)); // Sfondo scuro per la valvola spenta
        dc.DrawRoundedRectangle(frameThickness, frameThickness,
                                width - 2 * frameThickness, height - 2 * frameThickness, 3);

        // Calcola il colore del bagliore in base al tubeDriveValue
        // Da LA2A_VALVE_OFF_COLOR a LA2A_VALVE_ON_COLOR
        int r = LA2A_VALVE_OFF_COLOR.Red() + (LA2A_VALVE_ON_COLOR.Red() - LA2A_VALVE_OFF_COLOR.Red()) * tubeDriveValue;
        int g = LA2A_VALVE_OFF_COLOR.Green() + (LA2A_VALVE_ON_COLOR.Green() - LA2A_VALVE_OFF_COLOR.Green()) * tubeDriveValue;
        int b = LA2A_VALVE_OFF_COLOR.Blue() + (LA2A_VALVE_ON_COLOR.Blue() - LA2A_VALVE_OFF_COLOR.Blue()) * tubeDriveValue;

        // Disegna la "valvola" che si illumina
        wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
        if (gc) {
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

            // Crea un gradiente per il bagliore (simula il calore)
            wxPoint2DDouble startPt(width / 2, height);
            wxPoint2DDouble endPt(width / 2, 0);
            wxGraphicsBrush brush = gc->CreateLinearGradientBrush(
                startPt.m_x, startPt.m_y, endPt.m_x, endPt.m_y,
                wxColour(r, g, b, 0), // Trasparente alla base
                wxColour(r, g, b, (unsigned char)(255 * tubeDriveValue * 0.8)) // Pieno in cima, con alfa in base al valore
            );
            gc->SetBrush(brush);
            gc->SetPen(wxTransparentPen);

            // Disegna una forma che simuli una valvola che si illumina
            wxGraphicsPath path = gc->CreatePath();
            path.AddRoundedRectangle(frameThickness + 2, frameThickness + 2,
                                     width - 2 * frameThickness - 4, height - 2 * frameThickness - 4, 3);
            gc->FillPath(path);

            delete gc;
        } else {
            // Fallback (meno effetti, ma mostra il colore)
            dc.SetBrush(wxBrush(wxColour(r, g, b)));
            dc.DrawRoundedRectangle(frameThickness + 2, frameThickness + 2,
                                     width - 2 * frameThickness - 4, height - 2 * frameThickness - 4, 3);
        }
    }
};


// Struttura principale della tua GUI
struct Gla2aUI {
    LV2_URID_Map* map;
    LV2_URID_Unmap* unmap;
    LV2_UI_Write_Function write_function;
    LV2_UI_Controller    controller;

    LV2_URID atom_Float;
    LV2_URID atom_Int;

    // Riferimenti ai widget wxWidgets
    wxFrame* frame;
    wxNotebook* notebook;
    wxPanel* mainPanel;
    wxPanel* sidechainPanel;

    // Controlli sul mainPanel
    wxButton* inputAttenuatorButton; // -10dB Pad come button
    CustomKnob* inputGainKnob;        // Manopola Input Gain (BIG)
    CustomKnob* thresholdKnob;        // Manopola Threshold (BIG)
    CustomKnob* gainOutKnob;          // Manopola Gain Out (BIG)
    CustomKnob* gainReductionKnob;    // Nuova manopola Gain Reduction (BIG)

    CustomVUMeter* ioVUMeter;         // VU Meter I/O (BIG)
    wxButton* ioVUMeterSwitchButton;  // Pulsante per commutare il VU meter I/O
    CustomVUMeter* grVUMeter;         // VU Meter Gain Reduction (BIG)

    CustomKnob* arPresetKnob;         // Manopola AR Preset (Small)
    CustomKnob* ratioKnob;            // Manopola Ratio/Mode (Small)
    CustomKnob* tubeDriveKnob;        // Manopola Tube Drive (Small)
    ValveLightDisplay* valveLightDisplay; // Visualizzazione valvola

    // Controlli sul sidechainPanel
    CustomToggleSwitch* sidechainSourceSwitch; // Internal/External Key (Switch)
    CustomToggleSwitch* oversamplingSwitch;    // Upsampling X2 (Switch)
    CustomKnob* hpfFreqKnob;
    CustomKnob* hpfQKnob;
    CustomKnob* lpfFreqKnob;
    CustomKnob* lpfQKnob;
    CustomToggleSwitch* sidechainMonitorToggle; // Lasciato come switch per coerenza visiva

    // Etichette numeriche
    wxStaticText* inputGainValueText;
    wxStaticText* thresholdValueText;
    wxStaticText* gainOutValueText;
    wxStaticText* gainReductionValueText; // Nuova etichetta
    wxStaticText* arPresetValueText;
    wxStaticText* ratioValueText;
    wxStaticText* tubeDriveValueText;
    wxStaticText* hpfFreqValueText;
    wxStaticText* hpfQValueText;
    wxStaticText* lpfFreqValueText;
    wxStaticText* lpfQValueText;

    // Flag per il VU meter mode
    bool ioVUMeterShowsInput = true; // true = Input, false = Output. Default su Input.

    // Mappa per memorizzare i valori delle porte
    std::vector<float> port_values;

    wxApp* wx_app_instance;
    LV2_UI_Idle_Interface* idle_iface;

    // Metodi helper per aggiornare i controlli
    void UpdateKnobValue(CustomKnob* knob, float value, wxStaticText* text_label, const char* unit = "");
    void UpdateSwitchValue(CustomToggleSwitch* toggle, int value);
};

// --- Implementazione dei metodi helper per Gla2aUI ---
void Gla2aUI::UpdateKnobValue(CustomKnob* knob, float value, wxStaticText* text_label, const char* unit) {
    knob->SetValue(value);
    if (text_label) {
        wxString text;
        if (knob == arPresetKnob) {
            // AR Preset (Fast=0, Medium=1, Slow=2)
            int intValue = static_cast<int>(round(value));
            switch(intValue) {
                case 0: text = "Fast"; break;
                case 1: text = "Medium"; break;
                case 2: text = "Slow"; break;
                default: text = "Unknown"; break;
            }
        } else if (knob == ratioKnob) {
            // Ratio / Mode (2:1=0, 4:1=1, 8:1=2, Limit=3)
            int intValue = static_cast<int>(round(value));
            switch(intValue) {
                case 0: text = "2:1"; break;
                case 1: text = "4:1"; break;
                case 2: text = "8:1"; break;
                case 3: text = "LIMIT"; break;
                default: text = "Unknown"; break;
            }
        } else {
            text = wxString::FromDouble(value, 1); // 1 cifra decimale di default
            if (strlen(unit) > 0) text += " " + wxString(unit);
        }
        text_label->SetLabel(text);
    }
}

void Gla2aUI::UpdateSwitchValue(CustomToggleSwitch* toggle, int value) {
    toggle->SetOn(value == 1);
}

// --- Funzioni di Callback di wxWidgets per gli eventi UI ---
void OnKnobChanged(wxCommandEvent& event) {
    CustomKnob* knob = static_cast<CustomKnob*>(event.GetEventObject());
    Gla2aUI* ui = static_cast<Gla2aUI*>(knob->GetParent()->GetClientData());
    float value = knob->GetValue();

    if (knob == ui->inputGainKnob) {
        ui->write_function(ui->controller, GLA2A_CONTROL_GAIN_IN, sizeof(float), ui->atom_Float, &value);
        ui->UpdateKnobValue(ui->inputGainKnob, value, ui->inputGainValueText, "dB");
    } else if (knob == ui->thresholdKnob) { // Ora Threshold
        ui->write_function(ui->controller, GLA2A_CONTROL_THRESHOLD, sizeof(float), ui->atom_Float, &value);
        ui->UpdateKnobValue(ui->thresholdKnob, value, ui->thresholdValueText, "dB");
    } else if (knob == ui->gainOutKnob) {
        ui->write_function(ui->controller, GLA2A_CONTROL_GAIN_OUT, sizeof(float), ui->atom_Float, &value);
        ui->UpdateKnobValue(ui->gainOutKnob, value, ui->gainOutValueText, "dB");
    } else if (knob == ui->gainReductionKnob) { // Nuova manopola Gain Reduction
        ui->write_function(ui->controller, GLA2A_CONTROL_GAIN_REDUCTION, sizeof(float), ui->atom_Float, &value);
        ui->UpdateKnobValue(ui->gainReductionKnob, value, ui->gainReductionValueText, "%"); // O un'altra unità
    } else if (knob == ui->arPresetKnob) {
        int intValue = static_cast<int>(round(value));
        ui->write_function(ui->controller, GLA2A_CONTROL_AR_PRESET, sizeof(int), ui->atom_Int, &intValue);
        ui->UpdateKnobValue(ui->arPresetKnob, value, ui->arPresetValueText);
    } else if (knob == ui->ratioKnob) {
        int intValue = static_cast<int>(round(value));
        ui->write_function(ui->controller, GLA2A_CONTROL_RATIO_SELECT, sizeof(int), ui->atom_Int, &intValue);
        ui->UpdateKnobValue(ui->ratioKnob, value, ui->ratioValueText);
    } else if (knob == ui->tubeDriveKnob) {
        ui->write_function(ui->controller, GLA2A_CONTROL_TUBE_DRIVE, sizeof(float), ui->atom_Float, &value);
        ui->UpdateKnobValue(ui->tubeDriveKnob, value, ui->tubeDriveValueText, "");
        if (ui->valveLightDisplay) ui->valveLightDisplay->SetTubeDriveValue(value);
    } else if (knob == ui->hpfFreqKnob) {
        ui->write_function(ui->controller, GLA2A_CONTROL_SIDECHAIN_HPF_FREQ, sizeof(float), ui->atom_Float, &value);
        ui->UpdateKnobValue(ui->hpfFreqKnob, value, ui->hpfFreqValueText, "Hz");
    } else if (knob == ui->hpfQKnob) {
        ui->write_function(ui->controller, GLA2A_CONTROL_SIDECHAIN_HPF_Q, sizeof(float), ui->atom_Float, &value);
        ui->UpdateKnobValue(ui->hpfQKnob, value, ui->hpfQValueText, "");
    } else if (knob == ui->lpfFreqKnob) {
        ui->write_function(ui->controller, GLA2A_CONTROL_SIDECHAIN_LPF_FREQ, sizeof(float), ui->atom_Float, &value);
        ui->UpdateKnobValue(ui->lpfFreqKnob, value, ui->lpfFreqValueText, "Hz");
    } else if (knob == ui->lpfQKnob) {
        ui->write_function(ui->controller, GLA2A_CONTROL_SIDECHAIN_LPF_Q, sizeof(float), ui->atom_Float, &value);
        ui->UpdateKnobValue(ui->lpfQKnob, value, ui->lpfQValueText, "");
    }
}

void OnSwitchChanged(wxCommandEvent& event) {
    CustomToggleSwitch* toggle = static_cast<CustomToggleSwitch*>(event.GetEventObject());
    Gla2aUI* ui = static_cast<Gla2aUI*>(toggle->GetParent()->GetClientData());
    int value = toggle->IsOn() ? 1 : 0;

    if (toggle == ui->sidechainSourceSwitch) {
        ui->write_function(ui->controller, GLA2A_CONTROL_SIDECHAIN_MODE, sizeof(int), ui->atom_Int, &value);
    } else if (toggle == ui->oversamplingSwitch) {
        ui->write_function(ui->controller, GLA2A_CONTROL_OVERSAMPLING_MODE, sizeof(int), ui->atom_Int, &value);
    } else if (toggle == ui->sidechainMonitorToggle) {
        ui->write_function(ui->controller, GLA2A_CONTROL_SIDECHAIN_MONITOR_MODE, sizeof(int), ui->atom_Int, &value);
    }
}

void OnInputAttenuatorButtonClicked(wxCommandEvent& event) {
    wxButton* button = static_cast<wxButton*>(event.GetEventObject());
    Gla2aUI* ui = static_cast<Gla2aUI*>(button->GetClientData());
    int currentValue = (int)ui->port_values[GLA2A_CONTROL_INPUT_ATTENUATOR];
    int newValue = (currentValue == 0) ? 1 : 0; // Toggle 0/1

    ui->write_function(ui->controller, GLA2A_CONTROL_INPUT_ATTENUATOR, sizeof(int), ui->atom_Int, &newValue);
    ui->port_values[GLA2A_CONTROL_INPUT_ATTENUATOR] = (float)newValue; // Update local state
    // Aggiorna l'aspetto del pulsante, ad es. cambiando colore o testo se desiderato
    if (newValue == 1) {
        button->SetBackgroundColour(*wxRED); // Esempio: rosso quando attivo
    } else {
        button->SetBackgroundColour(wxNullColour); // Torna al colore predefinito
    }
    button->Refresh();
}

void OnIOVUMeterSwitchButtonClicked(wxCommandEvent& event) {
    Gla2aUI* ui = static_cast<Gla2aUI*>(event.GetClientData());
    ui->ioVUMeterShowsInput = !ui->ioVUMeterShowsInput;
    ui->ioVUMeterSwitchButton->SetLabel(ui->ioVUMeterShowsInput ? "Input" : "Output");
    ui->ioVUMeter->Refresh(); // Forziamo il ridisegno per riflettere il cambio
}


// --- Funzioni dell'Interfaccia LV2 UI ---

static LV2_UI_Handle instantiate(const LV2_UI_Descriptor* descriptor,
                                 const char* plugin_uri,
                                 const char* bundle_path,
                                 LV2_UI_Write_Function write_function,
                                 LV2_UI_Controller controller,
                                 LV2_UI_Widget* widget,
                                 const LV2_Feature* const* features) {
    Gla2aUI* ui = (Gla2aUI*)calloc(1, sizeof(Gla2aUI));
    if (!ui) return NULL;

    ui->write_function = write_function;
    ui->controller     = controller;
    ui->port_values.resize(24, 0.0f); // Adatta la dimensione al numero massimo di porte LV2, includendo i nuovi VU meters

    const LV2_URID_Map* map_feature = NULL;
    LV2_UI_Idle_Interface* idle_iface_feature = NULL;
    const LV2_UI_Parent* parent_feature = NULL;

    for (int i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            map_feature = (const LV2_URID_Map*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_UI__idleInterface)) {
            idle_iface_feature = (LV2_UI_Idle_Interface*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_UI__parent)) {
            parent_feature = (const LV2_UI_Parent*)features[i]->data;
        }
    }

    if (!map_feature) {
        fprintf(stderr, "Gla2aUI: Host does not provide URID Map feature.\n");
        free(ui);
        return NULL;
    }
    ui->map = (LV2_URID_Map*)map_feature;
    ui->unmap = (LV2_URID_Unmap*)lv2_feature_data(features, LV2_URID__unmap);

    ui->atom_Float = ui->map->map(ui->map->handle, LV2_ATOM__Float);
    ui->atom_Int   = ui->map->map(ui->map->handle, LV2_ATOM__Int);

    ui->idle_iface = idle_iface_feature;

    wxInitialize();

    ui->frame = new wxFrame(NULL, wxID_ANY, "GLA2A Compressor", wxPoint(100, 100), wxSize(800, 500),
                            wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX));
    ui->frame->SetBackgroundColour(LA2A_BEIGE_COLOR);
    ui->frame->SetExtraStyle(wxWS_EX_PROCESS_IDLE);

    *widget = (LV2_UI_Widget)ui->frame->GetHandle();

    ui->notebook = new wxNotebook(ui->frame, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
    ui->notebook->SetBackgroundColour(LA2A_BEIGE_COLOR);

    // --- Crea il Pannello Principale ("Main" Tab) ---
    ui->mainPanel = new wxPanel(ui->notebook, wxID_ANY);
    ui->mainPanel->SetBackgroundColour(LA2A_BEIGE_COLOR);
    ui->mainPanel->SetClientData(ui);
    ui->notebook->AddPage(ui->mainPanel, "Main", true);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    ui->mainPanel->SetSizer(mainSizer);

    // **Riga Superiore: Pad, Input Gain, Threshold, Gain Out**
    wxBoxSizer* topControlsSizer = new wxBoxSizer(wxHORIZONTAL);
    mainSizer->Add(topControlsSizer, 0, wxEXPAND | wxALL, 5);

    // -10dB PAD Button
    wxBoxSizer* padSizer = new wxBoxSizer(wxVERTICAL);
    ui->inputAttenuatorButton = new wxButton(ui->mainPanel, wxID_ANY, "-10dB PAD", wxDefaultPosition, wxSize(70, 40));
    ui->inputAttenuatorButton->SetClientData(ui);
    ui->inputAttenuatorButton->Bind(wxEVT_BUTTON, &OnInputAttenuatorButtonClicked);
    padSizer->Add(ui->inputAttenuatorButton, 0, wxALIGN_CENTER_HORIZONTAL);
    topControlsSizer->Add(padSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // Input Gain Knob (BIG)
    wxBoxSizer* inputGainSizer = new wxBoxSizer(wxVERTICAL);
    ui->inputGainKnob = new CustomKnob(ui->mainPanel, wxID_ANY, -20.0f, 20.0f, 0.0f, wxDefaultPosition, wxSize(80, 100));
    ui->inputGainKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    inputGainSizer->Add(ui->inputGainKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->inputGainValueText = new wxStaticText(ui->mainPanel, wxID_ANY, "0.0 dB");
    inputGainSizer->Add(ui->inputGainValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    inputGainSizer->Add(new wxStaticText(ui->mainPanel, wxID_ANY, "INPUT GAIN"), 0, wxALIGN_CENTER_HORIZONTAL);
    topControlsSizer->Add(inputGainSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // Threshold Knob (BIG)
    wxBoxSizer* thresholdSizer = new wxBoxSizer(wxVERTICAL);
    ui->thresholdKnob = new CustomKnob(ui->mainPanel, wxID_ANY, -40.0f, 0.0f, -20.0f, wxDefaultPosition, wxSize(80, 100));
    ui->thresholdKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    thresholdSizer->Add(ui->thresholdKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->thresholdValueText = new wxStaticText(ui->mainPanel, wxID_ANY, "-20.0 dB");
    thresholdSizer->Add(ui->thresholdValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    thresholdSizer->Add(new wxStaticText(ui->mainPanel, wxID_ANY, "THRESHOLD"), 0, wxALIGN_CENTER_HORIZONTAL);
    topControlsSizer->Add(thresholdSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    topControlsSizer->AddStretchSpacer(); // Spazio per il titolo

    // Plugin Title
    wxStaticText* titleText = new wxStaticText(ui->mainPanel, wxID_ANY, "GLA2A Compressor", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    topControlsSizer->Add(titleText, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);

    topControlsSizer->AddStretchSpacer(); // Spazio

    // Gain Out Knob (BIG)
    wxBoxSizer* gainOutSizer = new wxBoxSizer(wxVERTICAL);
    ui->gainOutKnob = new CustomKnob(ui->mainPanel, wxID_ANY, -20.0f, 20.0f, 0.0f, wxDefaultPosition, wxSize(80, 100));
    ui->gainOutKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    gainOutSizer->Add(ui->gainOutKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->gainOutValueText = new wxStaticText(ui->mainPanel, wxID_ANY, "0.0 dB");
    gainOutSizer->Add(ui->gainOutValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    gainOutSizer->Add(new wxStaticText(ui->mainPanel, wxID_ANY, "GAIN OUT"), 0, wxALIGN_CENTER_HORIZONTAL);
    topControlsSizer->Add(gainOutSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // **Riga Centrale: Gain Reduction Knob e VU Meters**
    wxBoxSizer* middleSectionSizer = new wxBoxSizer(wxHORIZONTAL);
    mainSizer->Add(middleSectionSizer, 1, wxEXPAND | wxALL, 5); // 1 = espandibile verticalmente

    middleSectionSizer->AddStretchSpacer();

    // Gain Reduction Knob (BIG)
    wxBoxSizer* gainReductionKnobSizer = new wxBoxSizer(wxVERTICAL);
    ui->gainReductionKnob = new CustomKnob(ui->mainPanel, wxID_ANY, 0.0f, 100.0f, 50.0f, wxDefaultPosition, wxSize(80, 100)); // Adatta range e default
    ui->gainReductionKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    gainReductionKnobSizer->Add(ui->gainReductionKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->gainReductionValueText = new wxStaticText(ui->mainPanel, wxID_ANY, "50.0 %");
    gainReductionKnobSizer->Add(ui->gainReductionValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    gainReductionKnobSizer->Add(new wxStaticText(ui->mainPanel, wxID_ANY, "GAIN REDUCTION"), 0, wxALIGN_CENTER_HORIZONTAL);
    middleSectionSizer->Add(gainReductionKnobSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);

    middleSectionSizer->AddStretchSpacer();

    // I/O VU Meter (BIG)
    wxBoxSizer* ioVUMeterSizer = new wxBoxSizer(wxVERTICAL);
    ui->ioVUMeter = new CustomVUMeter(ui->mainPanel, wxID_ANY, wxDefaultPosition, wxSize(150, 180));
    ioVUMeterSizer->Add(ui->ioVUMeter, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->ioVUMeterSwitchButton = new wxButton(ui->mainPanel, wxID_ANY, "Input"); // Default su "Input"
    ui->ioVUMeterSwitchButton->SetClientData(ui);
    ui->ioVUMeterSwitchButton->Bind(wxEVT_BUTTON, &OnIOVUMeterSwitchButtonClicked);
    ioVUMeterSizer->Add(ui->ioVUMeterSwitchButton, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 5);
    ioVUMeterSizer->Add(new wxStaticText(ui->mainPanel, wxID_ANY, "I/O METER"), 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 2);
    middleSectionSizer->Add(ioVUMeterSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);

    middleSectionSizer->AddStretchSpacer();

    // Gain Reduction VU Meter (BIG)
    // Non hai specificato un VU meter di GR separato nel nuovo layout, ma lo tengo se vuoi ripristinarlo.
    // Se la manopola "Gain Reduction" e il VU meter "I/O" sono gli unici grandi elementi centrali,
    // allora il layout dovrà essere adeguato per riempirli.
    // Per ora, assumo che il tuo "Big I/O switchable meter" sia l'unico VU meter.
    // Se invece vuoi anche un VU meter GR separato, dovremmo aggiungerlo qui.
    // Nel tuo ultimo layout hai detto "Big I/O switchable meter", e basta.
    // Se vuoi un secondo VU meter, devo ricrearlo e posizionarlo.

    // Aggiungo uno spazio che terrebbe il posto di un secondo VU meter o semplicemente per spaziatura
    middleSectionSizer->AddStretchSpacer();


    // **Riga Inferiore: AR Preset, Ratio/Mode, Tube Drive, Valvola**
    wxBoxSizer* bottomControlsSizer = new wxBoxSizer(wxHORIZONTAL);
    mainSizer->Add(bottomControlsSizer, 0, wxEXPAND | wxALL, 5);

    bottomControlsSizer->AddStretchSpacer();

    // AR Preset Knob (Small)
    wxBoxSizer* arPresetSizer = new wxBoxSizer(wxVERTICAL);
    ui->arPresetKnob = new CustomKnob(ui->mainPanel, wxID_ANY, 0.0f, 2.0f, 1.0f, wxDefaultPosition, wxSize(60, 80));
    ui->arPresetKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    arPresetSizer->Add(ui->arPresetKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->arPresetValueText = new wxStaticText(ui->mainPanel, wxID_ANY, "Medium");
    arPresetSizer->Add(ui->arPresetValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    arPresetSizer->Add(new wxStaticText(ui->mainPanel, wxID_ANY, "A/R PRESET"), 0, wxALIGN_CENTER_HORIZONTAL);
    bottomControlsSizer->Add(arPresetSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    bottomControlsSizer->AddStretchSpacer();

    // Ratio/Mode Knob (Small)
    wxBoxSizer* ratioSizer = new wxBoxSizer(wxVERTICAL);
    ui->ratioKnob = new CustomKnob(ui->mainPanel, wxID_ANY, 0.0f, 3.0f, 0.0f, wxDefaultPosition, wxSize(60, 80));
    ui->ratioKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    ui->ratioKnob->SetLabels({"2:1", "4:1", "8:1", "LIMIT"});
    ratioSizer->Add(ui->ratioKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->ratioValueText = new wxStaticText(ui->mainPanel, wxID_ANY, "2:1");
    ratioSizer->Add(ui->ratioValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    ratioSizer->Add(new wxStaticText(ui->mainPanel, wxID_ANY, "RATIO/MODE"), 0, wxALIGN_CENTER_HORIZONTAL);
    bottomControlsSizer->Add(ratioSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    bottomControlsSizer->AddStretchSpacer();

    // Tube Drive Knob (Small)
    wxBoxSizer* tubeDriveSizer = new wxBoxSizer(wxVERTICAL);
    ui->tubeDriveKnob = new CustomKnob(ui->mainPanel, wxID_ANY, 0.0f, 1.0f, 0.0f, wxDefaultPosition, wxSize(60, 80));
    ui->tubeDriveKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    tubeDriveSizer->Add(ui->tubeDriveKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->tubeDriveValueText = new wxStaticText(ui->mainPanel, wxID_ANY, "0.0");
    tubeDriveSizer->Add(ui->tubeDriveValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    tubeDriveSizer->Add(new wxStaticText(ui->mainPanel, wxID_ANY, "VALVE DRIVE"), 0, wxALIGN_CENTER_HORIZONTAL);
    bottomControlsSizer->Add(tubeDriveSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // Valve Light Display
    ui->valveLightDisplay = new ValveLightDisplay(ui->mainPanel, wxID_ANY, wxDefaultPosition, wxSize(50, 80));
    bottomControlsSizer->Add(ui->valveLightDisplay, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    bottomControlsSizer->AddStretchSpacer();


    // --- Crea il Pannello Sidechain & Advanced ("Sidechain" Tab) ---
    ui->sidechainPanel = new wxPanel(ui->notebook, wxID_ANY);
    ui->sidechainPanel->SetBackgroundColour(LA2A_BEIGE_COLOR);
    ui->sidechainPanel->SetClientData(ui);
    ui->notebook->AddPage(ui->sidechainPanel, "Sidechain & Advanced", false);

    wxBoxSizer* sidechainSizer = new wxBoxSizer(wxVERTICAL);
    ui->sidechainPanel->SetSizer(sidechainSizer);

    // Riga 1: Sidechain Source Switch e Oversampling Switch
    wxBoxSizer* topSidechainSizer = new wxBoxSizer(wxHORIZONTAL);
    sidechainSizer->Add(topSidechainSizer, 0, wxEXPAND | wxALL, 10);

    topSidechainSizer->AddStretchSpacer();
    // Sidechain Source Switch
    ui->sidechainSourceSwitch = new CustomToggleSwitch(ui->sidechainPanel, wxID_ANY, "EXTERNAL KEY", false, wxDefaultPosition, wxSize(80, 40));
    ui->sidechainSourceSwitch->Bind(wxEVT_CHECKBOX, &OnSwitchChanged);
    topSidechainSizer->Add(ui->sidechainSourceSwitch, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    topSidechainSizer->Add(new wxStaticText(ui->sidechainPanel, wxID_ANY, "SIDECHAIN SOURCE"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);

    topSidechainSizer->AddStretchSpacer();

    // Oversampling Switch (Default ON)
    ui->oversamplingSwitch = new CustomToggleSwitch(ui->sidechainPanel, wxID_ANY, "2X OS", true, wxDefaultPosition, wxSize(80, 40)); // Default TRUE
    ui->oversamplingSwitch->Bind(wxEVT_CHECKBOX, &OnSwitchChanged);
    topSidechainSizer->Add(ui->oversamplingSwitch, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    topSidechainSizer->Add(new wxStaticText(ui->sidechainPanel, wxID_ANY, "UPSAMPLING x2"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    topSidechainSizer->AddStretchSpacer();


    // Riga 2: HPF Freq & Q, LPF Freq & Q
    wxBoxSizer* filterControlsSizer = new wxBoxSizer(wxHORIZONTAL);
    sidechainSizer->Add(filterControlsSizer, 0, wxEXPAND | wxALL, 5);

    filterControlsSizer->AddStretchSpacer();
    // HPF Freq
    wxBoxSizer* hpfFreqSizer = new wxBoxSizer(wxVERTICAL);
    ui->hpfFreqKnob = new CustomKnob(ui->sidechainPanel, wxID_ANY, 20.0f, 20000.0f, 20.0f, wxDefaultPosition, wxSize(60, 80));
    ui->hpfFreqKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    hpfFreqSizer->Add(ui->hpfFreqKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->hpfFreqValueText = new wxStaticText(ui->sidechainPanel, wxID_ANY, "20 Hz");
    hpfFreqSizer->Add(ui->hpfFreqValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    hpfFreqSizer->Add(new wxStaticText(ui->sidechainPanel, wxID_ANY, "HPF FREQ"), 0, wxALIGN_CENTER_HORIZONTAL);
    filterControlsSizer->Add(hpfFreqSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // HPF Q
    wxBoxSizer* hpfQSizer = new wxBoxSizer(wxVERTICAL);
    ui->hpfQKnob = new CustomKnob(ui->sidechainPanel, wxID_ANY, 0.1f, 5.0f, 0.707f, wxDefaultPosition, wxSize(60, 80));
    ui->hpfQKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    hpfQSizer->Add(ui->hpfQKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->hpfQValueText = new wxStaticText(ui->sidechainPanel, wxID_ANY, "0.7");
    hpfQSizer->Add(ui->hpfQValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    hpfQSizer->Add(new wxStaticText(ui->sidechainPanel, wxID_ANY, "HPF Q"), 0, wxALIGN_CENTER_HORIZONTAL);
    filterControlsSizer->Add(hpfQSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    filterControlsSizer->AddStretchSpacer();

    // LPF Freq
    wxBoxSizer* lpfFreqSizer = new wxBoxSizer(wxVERTICAL);
    ui->lpfFreqKnob = new CustomKnob(ui->sidechainPanel, wxID_ANY, 20.0f, 20000.0f, 20000.0f, wxDefaultPosition, wxSize(60, 80));
    ui->lpfFreqKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    lpfFreqSizer->Add(ui->lpfFreqKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->lpfFreqValueText = new wxStaticText(ui->sidechainPanel, wxID_ANY, "20000 Hz");
    lpfFreqSizer->Add(ui->lpfFreqValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    lpfFreqSizer->Add(new wxStaticText(ui->sidechainPanel, wxID_ANY, "LPF FREQ"), 0, wxALIGN_CENTER_HORIZONTAL);
    filterControlsSizer->Add(lpfFreqSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // LPF Q
    wxBoxSizer* lpfQSizer = new wxBoxSizer(wxVERTICAL);
    ui->lpfQKnob = new CustomKnob(ui->sidechainPanel, wxID_ANY, 0.1f, 5.0f, 0.707f, wxDefaultPosition, wxSize(60, 80));
    ui->lpfQKnob->Bind(wxEVT_SLIDER, &OnKnobChanged);
    lpfQSizer->Add(ui->lpfQKnob, 0, wxALIGN_CENTER_HORIZONTAL);
    ui->lpfQValueText = new wxStaticText(ui->sidechainPanel, wxID_ANY, "0.7");
    lpfQSizer->Add(ui->lpfQValueText, 0, wxALIGN_CENTER_HORIZONTAL);
    lpfQSizer->Add(new wxStaticText(ui->sidechainPanel, wxID_ANY, "LPF Q"), 0, wxALIGN_CENTER_HORIZONTAL);
    filterControlsSizer->Add(lpfQSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    filterControlsSizer->AddStretchSpacer();

    // Riga 3: Sidechain Monitor (Se richiesto, altrimenti rimosso)
    // Non menzionato esplicitamente nell'ultimo layout, ma lascio qui se vuoi riaggiungerlo.
    wxBoxSizer* bottomSidechainSizer = new wxBoxSizer(wxHORIZONTAL);
    sidechainSizer->Add(bottomSidechainSizer, 0, wxEXPAND | wxALL, 5);
    bottomSidechainSizer->AddStretchSpacer();
    ui->sidechainMonitorToggle = new CustomToggleSwitch(ui->sidechainPanel, wxID_ANY, "MONITOR", false, wxDefaultPosition, wxSize(60, 40));
    ui->sidechainMonitorToggle->Bind(wxEVT_CHECKBOX, &OnSwitchChanged);
    bottomSidechainSizer->Add(ui->sidechainMonitorToggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    bottomSidechainSizer->Add(new wxStaticText(ui->sidechainPanel, wxID_ANY, "SIDECHAIN MONITOR"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    bottomSidechainSizer->AddStretchSpacer();


    ui->frame->SetSizerAndFit(new wxBoxSizer(wxVERTICAL));
    ui->frame->GetSizer()->Add(ui->notebook, 1, wxEXPAND | wxALL, 0);

    ui->frame->Layout();
    ui->frame->Show(true);

    // Inizializza i valori UI
    ui->UpdateKnobValue(ui->inputGainKnob, 0.0f, ui->inputGainValueText, "dB");
    ui->UpdateKnobValue(ui->thresholdKnob, -20.0f, ui->thresholdValueText, "dB");
    ui->UpdateKnobValue(ui->gainOutKnob, 0.0f, ui->gainOutValueText, "dB");
    ui->UpdateKnobValue(ui->gainReductionKnob, 50.0f, ui->gainReductionValueText, "%"); // Default per Gain Reduction
    // Il pulsante -10dB PAD non ha un metodo UpdateToggleValue per CustomToggleSwitch, gestito in port_event.
    // Impostiamo lo stato iniziale del pulsante se necessario
    // ui->inputAttenuatorButton->SetBackgroundColour(wxNullColour); // Default spento
    ui->UpdateKnobValue(ui->arPresetKnob, 1.0f, ui->arPresetValueText);
    ui->UpdateKnobValue(ui->ratioKnob, 0.0f, ui->ratioValueText);
    ui->UpdateKnobValue(ui->tubeDriveKnob, 0.0f, ui->tubeDriveValueText);
    ui->valveLightDisplay->SetTubeDriveValue(0.0f); // Valvola spenta all'inizio

    ui->UpdateSwitchValue(ui->sidechainSourceSwitch, 0); // Internal Key
    ui->UpdateSwitchValue(ui->oversamplingSwitch, 1);    // Default ON per Oversampling
    ui->UpdateKnobValue(ui->hpfFreqKnob, 20.0f, ui->hpfFreqValueText, "Hz");
    ui->UpdateKnobValue(ui->hpfQKnob, 0.707f, ui->hpfQValueText);
    ui->UpdateKnobValue(ui->lpfFreqKnob, 20000.0f, ui->lpfFreqValueText, "Hz");
    ui->UpdateKnobValue(ui->lpfQKnob, 0.707f, ui->lpfQValueText);
    ui->UpdateSwitchValue(ui->sidechainMonitorToggle, 0);


    return (LV2_UI_Handle)ui;
}

static void port_event(LV2_UI_Handle handle,
                       uint32_t      port_index,
                       uint32_t      buffer_size,
                       uint32_t      format,
                       const void* buffer) {
    Gla2aUI* ui = (Gla2aUI*)handle;

    if (port_index >= ui->port_values.size()) {
        fprintf(stderr, "Gla2aUI: port_event received for invalid port index %d\n", port_index);
        return;
    }

    if (format == ui->atom_Float) {
        float value = *(const float*)buffer;
        ui->port_values[port_index] = value;

        switch ((PortIndex)port_index) {
            case GLA2A_CONTROL_GAIN_IN:
                ui->UpdateKnobValue(ui->inputGainKnob, value, ui->inputGainValueText, "dB");
                break;
            case GLA2A_CONTROL_THRESHOLD: // Aggiornato a Threshold
                ui->UpdateKnobValue(ui->thresholdKnob, value, ui->thresholdValueText, "dB");
                break;
            case GLA2A_CONTROL_GAIN_OUT:
                ui->UpdateKnobValue(ui->gainOutKnob, value, ui->gainOutValueText, "dB");
                break;
            case GLA2A_CONTROL_GAIN_REDUCTION: // Nuova manopola GR
                ui->UpdateKnobValue(ui->gainReductionKnob, value, ui->gainReductionValueText, "%");
                break;
            case GLA2A_CONTROL_TUBE_DRIVE:
                 ui->UpdateKnobValue(ui->tubeDriveKnob, value, ui->tubeDriveValueText);
                 if (ui->valveLightDisplay) ui->valveLightDisplay->SetTubeDriveValue(value);
                 break;
            case GLA2A_CONTROL_SIDECHAIN_HPF_FREQ:
                ui->UpdateKnobValue(ui->hpfFreqKnob, value, ui->hpfFreqValueText, "Hz");
                break;
            case GLA2A_CONTROL_SIDECHAIN_HPF_Q:
                ui->UpdateKnobValue(ui->hpfQKnob, value, ui->hpfQValueText, "");
                break;
            case GLA2A_CONTROL_SIDECHAIN_LPF_FREQ:
                ui->UpdateKnobValue(ui->lpfFreqKnob, value, ui->lpfFreqValueText, "Hz");
                break;
            case GLA2A_CONTROL_SIDECHAIN_LPF_Q:
                ui->UpdateKnobValue(ui->lpfQKnob, value, ui->lpfQValueText, "");
                break;
            case GLA2A_PEAK_IN_L: // Aggiornamento VU Meter I/O (Input)
                if (ui->ioVUMeterShowsInput) {
                     ui->ioVUMeter->SetMeterValue(value);
                }
                break;
            case GLA2A_PEAK_OUT_L: // Aggiornamento VU Meter I/O (Output)
                if (!ui->ioVUMeterShowsInput) {
                     ui->ioVUMeter->SetMeterValue(value);
                }
                break;
            case GLA2A_PEAK_GR: // Aggiornamento VU Meter Gain Reduction (se ripristinato)
                // Se hai solo un VU Meter I/O, questa linea sarà commentata o rimossa.
                // ui->grVUMeter->SetMeterValue(value);
                break;
            default:
                break;
        }
    } else if (format == ui->atom_Int) {
        int value = *(const int*)buffer;
        ui->port_values[port_index] = (float)value;

        switch ((PortIndex)port_index) {
            case GLA2A_CONTROL_INPUT_ATTENUATOR:
                // Aggiorna l'aspetto del pulsante "-10dB PAD"
                if (value == 1) {
                    ui->inputAttenuatorButton->SetBackgroundColour(*wxRED);
                } else {
                    ui->inputAttenuatorButton->SetBackgroundColour(wxNullColour);
                }
                ui->inputAttenuatorButton->Refresh();
                break;
            case GLA2A_CONTROL_RATIO_SELECT:
                ui->UpdateKnobValue(ui->ratioKnob, (float)value, ui->ratioValueText);
                break;
            case GLA2A_CONTROL_AR_PRESET:
                ui->UpdateKnobValue(ui->arPresetKnob, (float)value, ui->arPresetValueText);
                break;
            case GLA2A_CONTROL_SIDECHAIN_MODE:
                ui->UpdateSwitchValue(ui->sidechainSourceSwitch, value);
                break;
            case GLA2A_CONTROL_SIDECHAIN_MONITOR_MODE:
                ui->UpdateSwitchValue(ui->sidechainMonitorToggle, value);
                break;
            case GLA2A_CONTROL_OVERSAMPLING_MODE:
                ui->UpdateSwitchValue(ui->oversamplingSwitch, value);
                break;
            default:
                break;
        }
    }
}

static int idle(LV2_UI_Handle handle) {
    Gla2aUI* ui = (Gla2aUI*)handle;
    if (wxTheApp) {
        wxTheApp->Yield(true);
    }
    return 0;
}

static void cleanup(LV2_UI_Handle handle) {
    Gla2aUI* ui = (Gla2aUI*)handle;
    if (ui) {
        if (ui->frame) {
            ui->frame->Destroy();
        }
        wxUninitialize();
        free(ui);
    }
}

static const void* extension_data(const char* uri) {
    if (!strcmp(uri, LV2_UI__idleInterface)) {
        static LV2_UI_Idle_Interface idle_iface = { idle };
        return &idle_iface;
    }
    return NULL;
}

static const LV2_UI_Descriptor gla2aUiDescriptor = {
    GLA2A_GUI_URI,
    instantiate,
    cleanup,
    port_event,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_UI_Descriptor* lv2ui_descriptor(uint32_t index) {
    switch (index) {
        case 0:
            return &gla2aUiDescriptor;
        default:
            return NULL;
    }
}

class MyApp : public wxApp {
public:
    virtual bool OnInit() {
        return true;
    }
    virtual int OnExit() {
        return 0;
    }
};

IMPLEMENT_APP_NO_MAIN(MyApp);
