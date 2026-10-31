# Mothion

Software di animazione 2D frame-by-frame per animatori singoli e piccoli studi.

**Stato: prototipo.** Questo passo contiene solo la shell dell'interfaccia
(provvisoria) e l'input da tavoletta grafica con un pennello di prova.
Non ci sono modello dati, timeline funzionante né salvataggio.

## Build

Requisiti: CMake ≥ 3.21, compilatore C++20, Qt ≥ 6.5 (Core, Gui, Widgets; Test per i test).
Qt va linkato **dinamicamente** (LGPL): CMake si ferma con un errore se trova un Qt statico.

### Linux (Ubuntu / Debian)

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-wayland
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build          # test unitari
./build/src/mothion
```

Per provare il percorso X11 da una sessione Wayland (via XWayland):
`QT_QPA_PLATFORM=xcb ./build/src/mothion`.

### Windows (MSVC)

1. Installa Visual Studio 2022 (workload "Sviluppo di applicazioni desktop con C++") e,
   con il Qt Online Installer, Qt 6.x per **MSVC 2022 64-bit**.
2. Da un "x64 Native Tools Command Prompt for VS 2022":

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.10.2\msvc2022_64
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo
C:\Qt\6.10.2\msvc2022_64\bin\windeployqt.exe build\src\RelWithDebInfo\mothion.exe
build\src\RelWithDebInfo\mothion.exe
```

`windeployqt` copia accanto all'eseguibile le DLL e i plugin di Qt (linking dinamico).

### Opzioni CMake

| Opzione | Default | Effetto |
|---|---|---|
| `MOTHION_BUILD_TESTS` | ON | Compila i test (QtTest). |
| `MOTHION_ENABLE_WINTAB` | ON | Solo Windows: abilita `--wintab`. Usa un'API **privata** di Qt (`Qt6::GuiPrivate`); se un aggiornamento di Qt la rompe, impostala a OFF. |

### Opzioni da riga di comando

- `--wintab` (Windows): usa Wintab invece di Windows Ink. Richiede `wintab32.dll` del driver della tavoletta.

## Architettura

```
src/
├─ core/     C++ puro, NESSUNA dipendenza da Qt (lo impone CMake: mothion_core non linka Qt)
│            PenSample, PressureCurve, StrokeSmoother, RateMeter
│  └─ document/ modello del progetto: TiledImage (tasselli 64×64 copy-on-write), Drawing,
│              Timing (esposizioni stile Harmony), Layer, Document, Command + commands/, History
├─ document/ DocumentController: possiede Document e History, livello/frame correnti,
│            esegue i comandi e li traduce in segnali Qt (unico accesso dell'UI al modello)
├─ input/    unico punto che conosce QTabletEvent/QMouseEvent → PenSample
│            TabletInputAdapter, ProximityFilter, TabletBackend(_wintab|_nowintab)
├─ canvas/   CanvasView (widget), ViewTransform (zoom/pan/rotazione/specchiatura), NavigationGesture,
│            DabSpacer (dab equidistanti), StrokeSession (tratto dal vivo su un TiledImage),
│            FrameRenderer (composizione dei livelli visibili nel frame), TileQt (ponte tasselli↔QImage),
│            DebugOverlay (F12)
├─ tools/    ToolState: strumento attivo, dimensione, opacità, colori, curva, stabilizzazione;
│            BrushPreset: preset predefiniti ("sotto-strumenti")
├─ ui/       MainWindow, DockLayout (disposizione e persistenza), PanelDock, Theme, IconFactory
│  └─ panels/ Strumenti, Sotto-strumento (preset con anteprima), Proprietà strumento,
│             slider verticali Dimensione/Opacità, Ruota colore, Set di colori,
│             Navigatore, Livelli; timeline/ = timeline delle esposizioni (stile Harmony)
└─ app/      main.cpp
```

Librerie: `mothion_core` (statica, senza Qt) ← `mothion_gui` (statica, Qt Widgets) ← `mothion` (eseguibile, solo `main`).

**Flusso dell'input**

```
QTabletEvent / QMouseEvent ──input::toPenSample()──▶ PenSample
   │                                                    │
   │  (prossimità: solo a QApplication → ProximityFilter)│
   ▼                                                    ▼
CanvasView::handleSample ──▶ NavigationGesture (mano / zoom / rotazione)
                         └─▶ StrokeSmoother → PressureCurve → DabSpacer → StrokeSession
                              (sul disegno del livello/frame corrente; a fine tratto
                               PaintCommand → History tramite DocumentController)
```

Dopo `toPenSample()` nessuna parte del programma tocca eventi Qt di input.

**Regole di dipendenza**
- I pannelli non conoscono il canvas: parlano solo con `ToolState` (segnali/slot).
  Unica eccezione: il Navigatore, che è una vista della vista e usa l'API pubblica di `CanvasView`.
- `DockLayout` è l'unico posto che conosce la disposizione dei pannelli: rifare la UX
  significa cambiare lì (e in `Theme`/`dark.qss`/`IconFactory` per l'aspetto).
- Il codice che usa API private di Qt è confinato in `input/TabletBackend_wintab.cpp`.

## Comandi (provvisori)

| Azione | Comando |
|---|---|
| Pennello / Gomma / Mano / Zoom | B / E / H / Z |
| Pan | Spazio + trascina, tasto centrale, pulsante laterale della penna, strumento Mano |
| Zoom | rotella, Spazio+Ctrl+trascina, strumento Zoom, Ctrl + / Ctrl − / Ctrl 0 (adatta) |
| Rotazione | Maiusc + rotella, Spazio+Maiusc+trascina, Ctrl , / Ctrl . / Ctrl / (azzera) |
| Annulla / Ripeti | Ctrl+Z / Ctrl+Maiusc+Z o Ctrl+Y (fino a 500 passi o 1 GB) |
| Overlay debug input | F12 |
| Timeline | clic/trascina: frame e livello correnti; ← → Home Fine, ↑ ↓ livello; Ctrl+rotella = zoom frame |
| Hold | trascina il bordo destro di un blocco (i frame successivi si spostano) |
| Disegni | disegnare su un frame vuoto crea un disegno; tasto destro sulla timeline: nuovo disegno, svuota frame, inserisci/elimina frame, elimina disegno |
| Livelli | + / cestino / frecce; occhio = visibilità, lucchetto = blocco; doppio clic = rinomina |
| Navigatore | clic/trascina sulla miniatura per spostare la vista; slider zoom/rotazione; specchia O/V |
| Layout | Finestra → Ripristina layout predefinito |

## Note su Qt 6 e tavolette (verificate su documentazione e sorgenti Qt 6.10)

- **Windows**: per default Qt 6 usa **Windows Ink** (messaggi `WM_POINTER`), senza configurazione.
  Wintab si attiva solo da codice con `QWindowsApplication::setWinTabEnabled()` (API privata),
  da noi esposto come `--wintab`. L'opzione `-platform windows:nowmpointer` citata in vecchia
  documentazione non esiste più.
- **Compressione**: `Qt::AA_CompressTabletEvents` è false per default (lo ribadiamo in `main.cpp`):
  su Windows Qt consegna anche i campioni storici (`GetPointerPenInfoHistory`), su X11 non comprime
  i movimenti del tablet. Gli eventi tablet vanno sempre **accettati**, altrimenti Qt li trasforma
  in eventi mouse, che possono essere compressi.
- **Prossimità**: `TabletEnterProximity`/`TabletLeaveProximity` arrivano solo a `QApplication`.
- **X11**: Qt riconosce penna/gomma dal **nome** del device XInput ("stylus", "pen", "eraser", ...).
  Con driver che espongono un solo device la gomma può non essere distinguibile.
- **Wayland**: protocollo `tablet-unstable-v2`, il tipo gomma arriva dal compositor; nessuna
  compressione lato Qt, posizione in passi di 1/256 px. Serve un compositor che lo supporti
  (GNOME, KDE Plasma, Sway).

Log utili: `QT_LOGGING_RULES="qt.qpa.input*=true" ./mothion` mostra i device rilevati da Qt.

## Come testare la tavoletta

Avvia Mothion, premi **F12** e verifica:

1. **Dispositivo**: tipo `Stylus`, punta `penna`. Se compare `Mouse` mentre usi la penna, il driver
   è in modalità mouse oppure Qt non riconosce il device.
2. **Prossimità**: avvicinando la penna senza toccare → "dentro", allontanandola → "fuori";
   il cerchio del pennello segue la penna in hover.
3. **Posizione subpixel**: i decimali della posizione cambiano in modo continuo.
4. **Pressione**: da ~0 a 1.000; il punto arancione si muove sulla curva di pressione e lo spessore
   del tratto segue. Prova a modificare la curva e a disegnare di nuovo.
5. **Inclinazione**: X/Y tra −60° e +60° inclinando la penna (se la penna la supporta).
6. **Rotazione**: diversa da 0 solo con penne che la supportano (es. Wacom Art Pen).
7. **Gomma**: girando la penna la punta diventa `gomma` e il tratto cancella anche con il pennello
   selezionato.
8. **Pulsanti laterali**: compaiono in "pulsanti"; premuti senza toccare la superficie e trascinando
   spostano la vista.
9. **Frequenza**: durante un tratto continuo, tipicamente 100–300 Hz secondo modello e driver.
   Valori molto più bassi (es. ~60 Hz) indicano compressione o fallback al mouse.

### Wacom
- **Windows**: driver Wacom ufficiale. Default = Windows Ink: in "Proprietà tavoletta Wacom" →
  Mappatura deve essere attivo "Usa Windows Ink", altrimenti non arrivano eventi penna.
  Prova anche `--wintab` e confronta latenza e frequenza.
- **Linux**: supportata dal kernel (`wacom`). X11: `xinput list` deve mostrare device separati
  "... Pen stylus" ed "... Pen eraser". Wayland: configurazione in Impostazioni → Tavoletta Wacom
  (GNOME) o Tavoletta grafica (KDE).

### Huion
- **Windows**: driver HuionTablet; nelle impostazioni del driver attiva/disattiva "Windows Ink"
  coerentemente con il backend che stai provando (`--wintab` richiede il driver Huion installato).
- **Linux**: molti modelli funzionano con il driver di kernel `hid-uclogic` senza installare nulla.
  Molte penne Huion non hanno la gomma fisica: in quel caso il punto 7 non è applicabile.

### XP-Pen
- **Windows**: driver "Pentablet" di XP-Pen; anche qui c'è l'opzione Windows Ink nel pannello del
  driver. Se la pressione non arriva con Windows Ink, prova `--wintab`.
- **Linux**: i modelli recenti sono supportati da `hid-uclogic`; in alternativa il driver XP-Pen.
  Su X11, se l'overlay mostra `Mouse`, controlla con `xinput list` il nome del device: Qt lo
  riconosce come tavoletta solo da certi nomi (vedi note sopra).

## Licenze

Qt è usato sotto LGPL v3 con linking dinamico. Nessuna dipendenza GPL.
Icone, nomi e grafica sono originali del progetto (le icone sono disegnate in codice in `IconFactory`).
