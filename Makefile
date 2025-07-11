# Definisci il nome del plugin (senza estensione .so)
PLUGIN_NAME = gla2a

# Trova i flags di compilazione e link dalle librerie LV2
# pkg-config è fondamentale per trovare le directory di include e le librerie necessarie.
LV2_CFLAGS := $(shell pkg-config --cflags lv2)
LV2_LIBS := $(shell pkg-config --libs lv2)

# Il compilatore C++ da usare
CXX = g++

# Flags di compilazione
# -fPIC: Necessario per creare librerie condivise
# -DPIC: Convenzione per indicare che si sta compilando codice indipendente dalla posizione
# -Wall: Abilita tutti i warning (buona pratica per trovare errori)
# -O2: Livello di ottimizzazione 2 (buon compromesso tra velocità di compilazione e performance)
# -g: Include informazioni di debug (utile per risolvere problemi)
# -std=c++11: Usa lo standard C++11 (o superiore, se preferisci)
CFLAGS = -fPIC -DPIC -Wall -O2 -g -std=c++11

# Flags di link
# -shared: Crea una libreria condivisa (.so)
LDFLAGS = -shared

# --- Regole di Compilazione ---

# Regola predefinita: compila il plugin
all: $(PLUGIN_NAME).so

# Compilazione del file sorgente .cpp in un oggetto .o
# $<: Il primo prerequisito (gla2a.cpp)
# $@: Il target (gla2a.o)
$(PLUGIN_NAME).o: $(PLUGIN_NAME).cpp
    $(CXX) $(CFLAGS) $(LV2_CFLAGS) -c $< -o $@

# Linkaggio dell'oggetto .o nella libreria condivisa .so
$(PLUGIN_NAME).so: $(PLUGIN_NAME).o
    $(CXX) $(LDFLAGS) $< -o $@ $(LV2_LIBS) -lm

# Regola per "pulire" i file generati dalla compilazione
clean:
    rm -f *.o *.so