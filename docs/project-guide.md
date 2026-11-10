# Guida al progetto Mothion

Questa guida accompagna chi vuole capire, usare o sviluppare Mothion. Non sostituisce
i commenti dei sorgenti: spiega il quadro d'insieme, i concetti fondamentali e il
percorso seguito dai dati mentre l'applicazione funziona.

Mothion è un'applicazione desktop C++20 e Qt 6 per disegno e animazione 2D
fotogramma per fotogramma. Il progetto è dichiaratamente un prototipo: alcune parti
sono già ampie e integrate (documenti, pennelli, timeline, compositing, animazione,
import/export), ma le API interne e l'esperienza utente possono ancora evolvere.

## Come orientarsi

- Per installazione, opzioni degli script, comandi rapidi e controlli dell'interfaccia,
  partire dal [README](../README.md).
- Per la griglia prospettica, vedere [Griglia prospettica](perspective-layer.md).
- Per i dettagli dell'algoritmo di deformazione, vedere [Puppet warp](puppet-warp.md).
- Per un riferimento tecnico più fitto a classi e gruppi di sorgenti, vedere la
  sezione [Riferimento tecnico dei moduli](#riferimento-tecnico-dei-moduli) di questa guida.
- Per un elenco dei moduli del codice, continuare con la sezione
  [Mappa dei sorgenti](#mappa-dei-sorgenti).

## Indice

- [Architettura e mappa dei sorgenti](#quadro-generale)
- [Modello del documento, livelli, timing e cronologia](#il-modello-del-documento)
- [Input, strumenti e pennellata](#dal-pennino-ai-pixel-sul-canvas)
- [Composizione, rendering e animazione](#composizione-e-visualizzazione)
- [Interfaccia e stato condiviso](#interfaccia-e-stato-condiviso)
- [File, import/export e autosalvataggio](#file-importazione-ed-esportazione)
- [Collegamento Blender](#integrazione-con-blender)
- [Build e test](#compilazione-e-dipendenze)
- [Indicazioni per estendere il progetto](#estendere-il-progetto)
- [Riferimento tecnico: convenzioni e moduli](#riferimento-tecnico-dei-moduli)
- [Catalogo del core](#catalogo-del-core)
- [Input, canvas e strumenti](#input-e-canvas)
- [Timeline e interfaccia](#timeline-e-riproduzione)
- [Compositing e nodi](#compositing)
- [Persistenza, audio e integrazioni](#persistenza-importexport-e-diagnostica)
- [Test e checklist per modifiche trasversali](#test-come-scegliere-il-bersaglio)

## Quadro generale

Il progetto è organizzato in tre livelli principali:

```text
┌─────────────────────────────────────────────────────────────┐
│ mothion: app/applicazione desktop                           │
│ main(), avvio Qt, finestra principale                       │
└────────────────────────────┬────────────────────────────────┘
                             │ usa
┌────────────────────────────▼────────────────────────────────┐
│ mothion_gui: UI, input Qt, controller, rendering e I/O       │
│ CanvasView, pannelli, DocumentController, ProjectFile, ecc. │
└────────────────────────────┬────────────────────────────────┘
                             │ usa
┌────────────────────────────▼────────────────────────────────┐
│ mothion_core: modello e algoritmi indipendenti da Qt         │
│ Document, tile, comandi, pennello, warp, tween, geometria    │
└─────────────────────────────────────────────────────────────┘
```

Il livello `core` descrive i dati e gli algoritmi, non l'interfaccia. `mothion_gui`
adatta quel nucleo a Qt: riceve eventi, visualizza immagini, gestisce file e presenta
i pannelli. L'eseguibile avvia l'applicazione e crea la finestra principale. Questa
separazione permette di testare molti algoritmi senza avviare l'intera UI e impedisce
al modello del documento di dipendere da widget o eventi Qt.

Un esempio concreto del flusso di una modifica:

```text
azione dell'utente
  → widget o strumento
  → DocumentController
  → Command applicato al Document
  → ChangeSet
  → segnali Qt per aggiornare pannelli e canvas
  → FrameRenderer ricompone l'immagine interessata
```

Durante una pennellata i pixel vengono aggiornati dal vivo; al termine la modifica
viene registrata come un singolo comando annullabile. Quindi "aggiornamento visivo
immediato" e "registrazione nella cronologia" sono fasi collegate, ma non identiche.

## Mappa dei sorgenti

| Percorso | Responsabilità |
|---|---|
| [`src/app/`](../src/app/) | Punto d'ingresso dell'applicazione: attributi Qt, opzioni da riga di comando, tema e avvio della finestra. |
| [`src/core/`](../src/core/) | Modello e algoritmi C++ senza Qt: pennello, documento, timing, undo/redo, geometria, tween e prospettiva. |
| [`src/document/`](../src/document/) | `DocumentController`, ponte tra modello e UI; coordinamento delle azioni, stato corrente e notifiche. |
| [`src/input/`](../src/input/) | Conversione degli eventi Qt da mouse e tavoletta nel tipo comune `PenSample`; backend tablet. |
| [`src/canvas/`](../src/canvas/) | Widget di disegno, rendering della scena, strumenti, trasformazioni della vista e sessioni di tratto. |
| [`src/tools/`](../src/tools/) | Stato condiviso degli strumenti, preferenze, definizioni/preset di pennello, librerie di pennelli e palette colori. |
| [`src/compositing/`](../src/compositing/) | Grafo dei nodi, tipi di nodo, valutazione delle immagini e operazioni sui pixel. |
| [`src/io/`](../src/io/) | File di progetto, autosalvataggio, export, cache e protocollo del collegamento a Blender. |
| [`src/audio/`](../src/audio/) | Riproduzione e analisi della traccia audio con Qt Multimedia. |
| [`src/ui/`](../src/ui/) | Finestra, menu, pannelli, timeline, dialoghi, layout, tema e finestre specializzate. |
| [`src/diagnostics/`](../src/diagnostics/) | Log, gestione dei crash e rilevamento dei blocchi. |
| [`src/shell/`](../src/shell/) | Provider opzionale di miniature `.moth` per Esplora file di Windows. |
| [`blender/mothion_link/`](../blender/mothion_link/) | Add-on installabile in Blender che comunica con Mothion. |
| [`resources/`](../resources/) | Icone, font e foglio di stile inclusi nell'applicazione. |
| [`tests/`](../tests/) | Test QtTest del core, dell'I/O, del canvas e delle aree UI. |
| [`cmake/`](../cmake/) | Dipendenze e avvisi del compilatore condivisi dalla configurazione CMake. |
| [`scripts/`](../scripts/) | Script di build e avvio per Linux e Windows; gli script di radice li richiamano. |
| [`docs/`](./) | Note tecniche e questa guida. |

La configurazione del progetto è in [`CMakeLists.txt`](../CMakeLists.txt) e
[`src/CMakeLists.txt`](../src/CMakeLists.txt). In breve, `mothion_core` è una
libreria statica senza Qt, `mothion_gui` è la libreria Qt, `mothion` è l'eseguibile.
Su Windows si può inoltre compilare `mothion_thumbnails`; il tool facoltativo
`mothion_brushsheet` viene incluso solo attivando `MOTHION_BUILD_DEVTOOLS`.

## Il modello del documento

Il documento è il modello persistente del lavoro, non la schermata che lo mostra.
La classe [`doc::Document`](../src/core/document/Document.h) contiene dimensioni,
frequenza e durata della scena, colore del foglio, traccia audio, camera, marcatori,
rete di compositing, livelli temporali e identificatori. Il modello organizza i
contenuti in questa gerarchia:

```text
Document
└── Cel (riga/elemento della timeline)
    ├── Timing: quali disegni sono esposti sui fotogrammi
    ├── Drawing (un'immagine logica riutilizzabile)
    │   └── Layer (dal basso verso l'alto)
    │       ├── immagine raster sparsa
    │       └── maschera opzionale
    └── Motion: trasformazione del cel nel tempo
```

### Cel, disegno ed esposizione

Un **cel** è una riga della timeline. Contiene disegni e un oggetto `Timing`, che
associa intervalli di fotogrammi ai disegni. Un **disegno** è indipendente dal tempo:
può essere mostrato una volta, mantenuto per più fotogrammi o riutilizzato più avanti.
Un'esposizione è un intervallo semiaperto `[inizio, fine)`, con indici di fotogramma
interni a partire da zero. Gli intervalli non si sovrappongono; le esposizioni
contigue dello stesso disegno vengono unite. I fotogrammi senza esposizione sono
vuoti. Questa distinzione permette di allungare un hold senza duplicare i pixel.

Esempio: se il disegno A è esposto sui fotogrammi `[0, 4)` e il disegno B su `[4, 6)`,
A viene mostrato per quattro fotogrammi e B per due. La UI presenta i fotogrammi
all'utente in numerazione a partire da 1; il modello e le API usano in prevalenza
indici a partire da 0.

Le esposizioni e le operazioni di inserimento, rimozione e ridimensionamento dei
fotogrammi sono implementate in [`Timing`](../src/core/document/Timing.h). La durata
totale della scena è una proprietà separata: le esposizioni possono estendersi oltre
il limite corrente della scena.

### Immagini, tile e livelli

Un livello raster è una [`TiledImage`](../src/core/document/TiledImage.h): un'immagine
sparsamente rappresentata da tile da 64×64 pixel. Le tile mancanti sono trasparenti,
perciò una linea su una tela grande non richiede memoria per l'intera tela. Le
coordinate possono essere negative; il ritaglio ai bordi del documento è compito del
rendering o dello strumento che scrive i pixel.

Le tile sono condivise con puntatori e copiate solo quando qualcuno deve modificarle
(copy-on-write). Copiare un'immagine o conservare una versione precedente costa
quindi poco finché i pixel non cambiano. La cronologia sfrutta questa proprietà per
memorizzare gli stati prima e dopo una pennellata.

Il pixel del core è ARGB premoltiplicato, compatibile con
`QImage::Format_ARGB32_Premultiplied`: il core mantiene così il formato adatto alla
composizione senza includere Qt.

Ogni disegno ha livelli ordinati dal basso verso l'alto. Un livello può essere
raster oppure una griglia prospettica; può avere visibilità, blocco, opacità, modalità
di fusione e, se raster, una maschera. La maschera è interpretata in scala di grigi:
bianco o pixel assente lascia vedere il livello, nero lo nasconde e i grigi lo
mostrano parzialmente.

### Modifiche e undo/redo

Le modifiche che devono essere annullabili sono oggetti derivati da
[`doc::Command`](../src/core/document/Command.h). Un comando implementa `apply()` e
`revert()`: oltre a eseguire o invertire la modifica, restituisce un `ChangeSet` che
descrive se sono cambiati struttura, pixel, camera, marcatori o motion. I segnali
conseguenti consentono ai consumer di aggiornare solo ciò che serve.

[`History`](../src/core/document/History.h) conserva comandi applicati e annullati,
elimina il ramo redo quando si esegue un nuovo comando dopo un undo e rimuove le
modifiche più vecchie oltre i limiti impostati. I limiti predefiniti sono 500 passi e
1 GiB stimato di dati. La cronologia tiene anche un indice di "stato salvato", usato
per determinare se il documento è modificato.

Per raggruppare più modifiche atomiche in una sola azione utente si usa un comando
composto, ad esempio durante importazioni o operazioni su più livelli. Gli strumenti
possono tenere aperte alcune modifiche interattive (trasformazione o puppet warp):
in quel caso l'undo locale dello strumento è distinto dalla cronologia generale,
finché l'utente applica o annulla l'operazione.

### Il controller: confine fra modello e UI

[`DocumentController`](../src/document/DocumentController.h) possiede il documento
attivo e la relativa cronologia. Tiene inoltre selezionati cel, fotogramma, disegno e
livello correnti. La UI legge il modello attraverso il controller e gli chiede le
modifiche tramite metodi o comandi, invece di alterare direttamente il modello.

Il controller gestisce tra le altre cose: creazione e caricamento dei documenti,
selezione temporale, livelli, esposizioni, importazioni, selezione di pixel, audio,
camera, tween, motion, collegamenti Blender, painting live e segnali di aggiornamento.
È la posizione naturale in cui aggiungere un'azione del documento che debba
rispettare contemporaneamente undo, stato modificato e aggiornamento delle viste.

## Dal pennino ai pixel sul canvas

[`CanvasView`](../src/canvas/CanvasView.h) è il widget centrale di disegno. La
conversione dell'input avviene al confine Qt: gli eventi di tavoletta e mouse diventano
[`PenSample`](../src/core/PenSample.h), una rappresentazione comune con posizione,
pressione, inclinazione, rotazione e informazioni sul dispositivo. Gli eventi mouse
sintetizzati da Qt a partire dalla tavoletta sono filtrati, per non duplicare i campioni.

Il flusso di una pennellata è:

```text
QTabletEvent / QMouseEvent
  → input::toPenSample()
  → CanvasView: navigazione, gesto strumento o disegno
  → StrokeSmoother / StrokeShaper
  → BrushInput (pressione, velocità e dinamiche)
  → DabGenerator (posizione e aspetto delle impronte)
  → DabStamper e StrokeSession
  → tile del livello o della maschera
  → PaintCommand nella cronologia al rilascio
  → FrameRenderer aggiorna la zona modificata
```

`PenSample` separa l'input dal framework. La curva di pressione
[`PressureCurve`](../src/core/PressureCurve.h) rimappa la pressione hardware con una
tabella precalcolata. Il motore in [`core/brush/`](../src/core/brush/) converte poi i
campioni in impronte ("dab"): definisce dimensione, distanza, rotazione, punte,
spray, variazioni casuali e dinamiche. Il generatore è indipendente dai pixel e un
seed per tratto permette di riprodurre le stesse scelte casuali.

[`StrokeSession`](../src/canvas/StrokeSession.h) applica le impronte all'immagine:
accumula la copertura del tratto in una maschera e aggiorna solo le tile toccate.
Gestisce inoltre selezione, gomma, miscelazione colore, modalità ribbon ed effetto
water edge previsti dallo stile del pennello. Alla fine del tratto può essere
ricalcolato il passaggio definitivo a partire dai punti finali, mantenendo lo stesso
seed del preview.

Il resto degli strumenti del canvas implementa operazioni distinte: riempimento,
selezione e maschera, spostamento, trasformazione, forme, contagocce, strumenti
effetto, griglia prospettica e puppet warp. Gli strumenti attivi condividono lo stato
tramite `ToolState`; non sono widget autonomi che modificano direttamente il
documento.

## Composizione e visualizzazione

[`FrameRenderer`](../src/canvas/FrameRenderer.h) costruisce l'immagine di un singolo
fotogramma nell'ordine seguente:

1. prepara il foglio (o trasparenza, quando richiesto);
2. per ogni cel dal basso verso l'alto, trova il disegno esposto al fotogramma;
3. compone i livelli visibili del disegno, applicando maschere, opacità e modalità
   di fusione;
4. applica opacità, fusione ed eventuale trasformazione animata del cel;
5. aggiorna la regione richiesta, che può essere solo il rettangolo toccato dal tratto.

Le coordinate del documento e quelle del widget sono separate: il canvas applica una
[`ViewTransform`](../src/canvas/ViewTransform.h) per zoom, rotazione, riflessione e
pan. Queste trasformazioni di visualizzazione non riscrivono i pixel. Le guide
prospettiche sono visualizzate nel canvas ma possono essere escluse dalla composizione
destinata alla riproduzione o all'export.

La modalità di fusione è definita nel core. Quelle supportate direttamente da
`QPainter` usano il relativo composition mode; quelle non disponibili vengono
calcolate dal codice in `core/document/Blend.cpp`. Per questo i livelli possono avere
comportamenti coerenti anche quando una modalità richiede un'implementazione software.

La UI può mostrare inoltre onion skin dei disegni vicini. La riproduzione è coordinata
dal player della timeline: il tempo di riferimento è l'orologio, non il numero di
tick del timer. Se il rendering rallenta, vengono saltati fotogrammi invece di
accumulare ritardo.

## Animazione oltre al disegno per fotogrammi

Il modello supporta più strumenti e proprietà temporali:

- **Motion dei cel**: chiavi di posizione, scala, rotazione e opacità trasformano un
  cel come unità senza modificare i disegni sorgente. Interpolazione e curve di easing
  sono nel modello del documento.
- **Camera**: chiavi temporali definiscono porzione inquadrata, zoom e rotazione.
  Camera invisibile equivale a esportare il foglio intero.
- **Tween**: `document::DrawingTween` registra due immagini di posa e genera disegni
  intermedi, abbinando i livelli per nome e facendo entrare o uscire quelli senza
  corrispondenza. Il nucleo `core/tween/` stima il flusso ottico e costruisce le
  immagini interpolate; la UI può usare anteprime ridotte.
- **Griglia prospettica**: un livello guida contiene i parametri della griglia, non
  pixel dipinti. I gizmo permettono di modificarla e gli strumenti possono usarla per
  agganciare i tratti. I dettagli geometrici sono in
  [perspective-layer.md](perspective-layer.md).
- **Puppet warp**: il livello degli strumenti del canvas raccoglie la forma e apre
  un rig di mesh, pin e deformazione ARAP nel core. Applicare la deformazione produce
  una modifica raster annullabile. Algoritmo e limiti sono descritti in
  [puppet-warp.md](puppet-warp.md).
- **Audio**: il documento conserva una traccia con i byte del file, offset in
  fotogrammi, volume e mute. `AudioPlayer` usa Qt Multimedia; la riproduzione
  dell'audio è sincronizzata con i fotogrammi dalla UI e può supportare lo scrub.
- **Compositing a nodi**: il documento conserva dati del grafo, mentre
  [`compositing::Graph`](../src/compositing/Graph.h) e `Evaluator` lo modificano e lo
  calcolano. Ogni nodo ha ingressi/uscite, parametri e una funzione di elaborazione.
  Le connessioni non possono formare cicli; i risultati restano in cache finché
  impostazioni, connessioni, input o contesto temporale non cambiano. I nodi sono
  implementati in `src/compositing/nodes/`, l'editor in
  `src/ui/compositing/`.

## Interfaccia e stato condiviso

[`MainWindow`](../src/ui/MainWindow.h) crea controller, canvas, librerie e pannelli,
costruisce menu e azioni globali, coordina apertura/salvataggio, import/export,
autosave, playback e integrazioni. I pannelli (timeline, livelli, palette strumenti,
proprietà, colore, navigatore e altri) presentano porzioni dello stato condiviso e
invocano i servizi del controller o di `ToolState`.

`ToolState` raccoglie lo strumento e le proprietà attive, inclusi valori usati da più
pannelli. I segnali Qt propagano i cambiamenti senza creare dipendenze arbitrarie fra
i pannelli. `DockLayout` gestisce disposizione e persistenza delle dock. Il navigatore
usa l'interfaccia pubblica del canvas per controllare la vista. Tema, scorciatoie,
dialoghi di importazione/esportazione e pannelli sono organizzati sotto `src/ui/`.

## File, importazione ed esportazione

### File `.moth`

[`ProjectFile`](../src/io/ProjectFile.h) memorizza il documento in un database SQLite.
È il formato nativo, con versione esplicita tramite `PRAGMA user_version` e
identificatore applicazione `MOTH`. La versione corrente è 12. Il database contiene
metadati del documento, cels, disegni, livelli, esposizioni, tile, miniature, audio,
grafo compositing, camera, maschere, marcatori, motion e riferimenti Blender.

I pixel delle tile sono salvati come blocchi ARGB premoltiplicati compressi. Il
salvataggio crea un file temporaneo vicino al file di destinazione e lo rinomina al
termine: un errore non dovrebbe distruggere il salvataggio precedente. Le versioni
precedenti vengono caricate con conversioni di compatibilità descritte nell'header
di `ProjectFile.h`; ad esempio, vecchi file senza livelli vengono ricostruiti con un
livello raster per disegno. Il file è un database SQLite, quindi può essere
esaminato con strumenti SQLite, ma la modifica manuale può violare le invarianti del
documento.

### Import e export

- **Immagini e sequenze**: trasformate in livelli/disegni attraverso il controller;
  l'importazione di una sequenza assegna disegni ai fotogrammi e ai relativi hold.
- **Pennelli Clip Studio**: `SutImporter` legge il formato `.sut` tramite SQLite e lo
  converte nelle strutture di preset e definizione del pennello.
- **Immagine singola o sequenza**: `FrameExport` renderizza il fotogramma e salva
  PNG o JPEG; per JPEG l'immagine viene resa opaca. L'export può lasciare trasparente
  il foglio quando il formato lo consente.
- **Video**: `VideoExport` consegna i fotogrammi a un processo `ffmpeg`; la
  dipendenza è runtime e non necessaria per compilare o usare il resto dell'app.
  Codec e opzioni determinano se è possibile conservare l'alpha. La guida d'uso e
  le limitazioni aggiornate sono nel README.
- **Miniature**: le miniature del progetto sono incorporate nel file e su Windows
  un provider shell separato le espone a Esplora file.

### Autosave, log e crash

`Autosave` crea copie distinte dal progetto originale e tiene metadati per proporre
il recupero alla successiva apertura. Le copie ritirate quando un progetto viene
salvato o chiuso intenzionalmente possono restare disponibili come backup temporanei.
La finestra principale decide quando salvare: evita di catturare un tratto ancora in
corso e può interrompere/chiudere le modifiche interattive prima di un salvataggio.

`CrashHandler` mantiene log di avvio e report di crash; quando possibile tenta anche
un backup del documento. Percorsi, procedura di recupero e dettagli per inviare un
report sono documentati in [Crashes e log nel README](../README.md#crashes-and-logs)
e [Autosave nel README](../README.md#autosave).

## Integrazione con Blender

L'add-on sotto `blender/mothion_link/` si collega al server
[`BlenderLinkServer`](../src/io/BlenderLinkServer.h) in esecuzione nel processo
Mothion. Il server ascolta sulla macchina locale; gli scambi sono messaggi con header
JSON e payload delle immagini. Mothion riceve informazioni sulle scene, richiede
fotogrammi e li importa come sequenza in un cel; può mantenere il riferimento alla
scena per aggiornamenti successivi e usa una cache dei frame.

Il collegamento è distinto dal compositing interno: Blender produce una sequenza
raster, mentre il grafo di compositing Mothion lavora sulle immagini del documento.
Le opzioni di installazione dell'add-on e le interazioni visibili all'utente si
trovano nei dialoghi Blender della UI e nel README.

## Compilazione e dipendenze

La compilazione usa CMake (almeno 3.21), C++20, Qt 6 (almeno 6.5) ed Eigen 3.4.
Qt è collegato dinamicamente. La configurazione cerca Eigen nel sistema e, se manca,
scarica la versione fissata dal progetto durante la configurazione.

Quick start su Linux:

```sh
./build.sh
./run.sh
```

Configurazione CMake essenziale, quando le dipendenze sono già installate:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Su Windows gli script equivalenti sono `build.bat` e `run.bat`. Per opzioni,
dipendenze specifiche della distribuzione, Qt personalizzato e distribuzione dei
runtime su Windows, fare riferimento alla sezione
[Build del README](../README.md#build). `ffmpeg` serve solo per l'export video;
Qt Multimedia serve invece per riprodurre e decodificare la traccia audio.

Le opzioni CMake principali:

| Opzione | Default | Effetto |
|---|---:|---|
| `MOTHION_BUILD_TESTS` | `ON` | Compila i test QtTest. |
| `MOTHION_BUILD_DEVTOOLS` | `OFF` | Compila `mothion_brushsheet` per generare immagini di prova dei pennelli. |
| `MOTHION_ENABLE_WINTAB` | `ON` | Su Windows abilita il backend Wintab opzionale; quello predefinito è Windows Ink tramite Qt. |

Il backend Wintab, che usa un'API privata di Qt, è confinato in
`src/input/TabletBackend_wintab.cpp`. Se la compatibilità con una versione Qt crea
problemi, si può disabilitare l'opzione senza rimuovere il backend standard.

## Test e qualità

I test sono eseguibili separati QtTest registrati in CTest. Il nome del target indica
in genere l'area coperta:

| Famiglia | Esempi | Cosa protegge |
|---|---|---|
| Core e algoritmi | `tst_core`, `tst_brush`, `tst_puppet`, `tst_tween`, `tst_blend`, `tst_camera` | Semantica dei dati e risultati degli algoritmi senza interazione completa con la finestra. |
| Documento e persistenza | `tst_document`, `tst_commands`, `tst_projectfile`, `tst_autosave` | Invarianti del documento, comandi reversibili, compatibilità del file e ciclo di recupero. |
| Canvas e compositing | `tst_canvas`, `tst_compositing` | Resa delle immagini, nodi e operazioni dell'area di disegno. |
| UI | `tst_ui_panels`, `tst_ui_tools`, `tst_ui_drawing`, `tst_ui_puppet`, `tst_ui_timeline`, `tst_ui_audio`, `tst_ui_compositing`, `tst_ui_window` | Comportamento verificabile dei pannelli e della finestra per area funzionale. |
| Integrazioni | `tst_blenderlink`, `tst_brushimport`, `tst_colorsets`, `tst_diagnostics` | Protocollo Blender, importazioni, palette e diagnostica. |

Le UI test sono eseguite sulla piattaforma Qt `offscreen`, così possono girare anche
senza display. I workflow CI documentati nel README aggiungono esecuzioni su Linux e
Windows, locale `C` e sanitizzatori. Per un cambiamento conviene partire dal test
mirato e poi usare la suite CTest se il comportamento attraversa più livelli.

## Estendere il progetto

Alcune regole pratiche per aggiungere una funzionalità mantenendo la struttura:

1. **Individuare il proprietario dei dati.** Se il dato deve essere salvato o
   annullato, modellarlo nel core/documento, non soltanto nel widget che lo modifica.
2. **Definire l'operazione nel modello.** Per una modifica annullabile introdurre un
   `Command` con `apply()` e `revert()`, e restituire un `ChangeSet` preciso.
3. **Instradare l'azione attraverso il controller.** Il controller coordina comando,
   cronologia, stato corrente e notifiche; così anche gli altri pannelli restano
   coerenti.
4. **Collegare la visualizzazione.** Se cambia l'immagine, verificare sia il canvas
   interattivo sia `FrameRenderer` e i percorsi di export che riutilizzano il
   rendering.
5. **Aggiornare la persistenza.** Nuovi dati del documento richiedono lettura e
   scrittura in `ProjectFile`, aggiornamento della versione/schema e copertura dei
   vecchi file.
6. **Aggiungere test nell'area giusta.** Coprire gli invarianti nel test core o
   documento e l'interazione nei test canvas/UI; includere round-trip di salvataggio
   se il dato è persistente.
7. **Documentare le parti specialistiche.** Le note in `docs/` sono il posto adatto
   per algoritmi o protocolli che richiedono più profondità del README.

Non tutti i moduli seguono lo stesso schema: per esempio, la pennellata viene
visualizzata mentre è ancora in corso e solo dopo diventa un comando storico; un
`Command` non deve quindi necessariamente essere l'unico meccanismo con cui i pixel
vengono aggiornati.

## Riferimento tecnico dei moduli

Questa parte approfondisce la mappa architetturale e i flussi già presentati. Le tabelle associano i principali gruppi di file alle loro responsabilità; non sono un indice di ogni singola funzione, ma aiutano a trovare il punto corretto del codice e a seguirne le collaborazioni.
Nei cataloghi, i percorsi sono relativi a `src/` salvo diversa indicazione.

### Prima di leggere il codice

#### Convenzioni e namespace

Il codice applicativo usa il namespace radice `mothion`, con namespace più specifici
come `mothion::doc`, `mothion::canvas`, `mothion::tools`, `mothion::ui`,
`mothion::io`, `mothion::brush`, `mothion::puppet` e `mothion::compositing`.
L'interfaccia del modello del documento sta in `doc`; il controller Qt sta invece in
`document`. La somiglianza dei nomi è intenzionale ma i ruoli sono diversi:

- `doc::Document` è il modello dati C++ indipendente da Qt;
- `document::DocumentController` è l'oggetto Qt che possiede il modello, coordina
  comandi e cronologia, espone lo stato corrente e invia segnali alle viste.

Gli identificatori `CelId`, `DrawingId` e `LayerId` sono distinti; non vanno
intercambiati con gli indici di posizione nella lista. Gli ID vengono allocati dal
documento e non riutilizzati durante la sua vita. Gli indici invece cambiano quando
si riordinano cels o livelli.

Quasi tutte le coordinate temporali del core sono 0-based e le durate sono conteggi
positivi. Gli intervalli di esposizione sono semiaperti: includono `start`, escludono
`start + length`. La presentazione della timeline può mostrare numeri a partire da 1.

#### Regola di dipendenza

Il core non dipende da Qt. È possibile includere header come `Document.h`,
`TiledImage.h`, `DabGenerator.h` o `PuppetRig.h` in codice C++ non Qt. La GUI può
usare il core e Qt; il core non deve importare `QWidget`, `QImage` o eventi Qt. Dove
serve una conversione (ad esempio da una `Tile` a una `QImage`), questa è collocata
nel layer GUI, come `canvas/TileQt`.

Questa regola è importante in entrambe le direzioni:

1. algoritmi puri e invarianti dei dati vanno nel core;
2. adattamento a dispositivi, file system, widget e rendering Qt vanno nella GUI;
3. l'orchestrazione di un'azione che tocca più livelli passa dal controller o da un
   servizio di alto livello già responsabile.

### Catalogo del core

#### Campioni, filtraggio e misure

| Sorgente | Ruolo |
|---|---|
| [`PenSample.h`](../src/core/PenSample.h) | Dato comune di input: coordinate subpixel, pressione, tilt, rotazione, stato del pulsante, fase e tipo di dispositivo fisico. |
| `PressureCurve.h/.cpp` | Curva Bezier precalcolata in LUT per rimappare la pressione. |
| `StrokeSmoother.h/.cpp` | Filtro esponenziale leggero per posizione e pressione; espone reset e restart, non è il correttore finale del pennello. |
| `RateMeter.h/.cpp` | Misura una frequenza nel tempo, utile per mostrare dati di input/prestazioni. |
| `Parallel.h` | Utility di lavoro parallelo per algoritmi che possono suddividere dati senza dipendere da Qt. |

`PenSample::tool` descrive il puntale fisico (penna, gomma hardware, mouse), non
l'azione scelta nella palette. L'eraser hardware può quindi essere interpretato come
gomma anche quando la palette indica un altro strumento.

#### Motore del pennello

Il motore separa **stile**, **campioni**, **impronte** e **rasterizzazione**:

| Sorgente | Ruolo |
|---|---|
| `brush/BrushStyle.h` | Parametri del tratto: dimensione, spacing, durezza, punte, spray, miscela, ribbon, water edge, in/out e dinamiche. |
| `brush/Effector.h/.cpp` | Collega un parametro a pressione, velocità, tilt, random o andamento inizio/fine; curva e valore minimo definiscono la risposta. |
| `brush/VelocityMeter.h/.cpp` | Stima la velocità usata dagli effectors, separandola dal tasso dei campioni. |
| `brush/Random.h` | Generatore pseudo-casuale deterministico usato per riprodurre lo stesso tratto con lo stesso seed. |
| `brush/DabGenerator.h/.cpp` | Interpola gli input e decide posizione, distanza, dimensione e proprietà di ogni dab. Non scrive pixel. |
| `brush/Hardness.h` | Funzioni per la copertura del bordo e la durezza dell'impronta. |
| `brush/StrokeShaper.h/.cpp` | Rileva angoli e applica correzioni che richiedono più contesto o la conoscenza della fine del tratto. |

Uno stile può avere molte dinamiche contemporaneamente; il valore finale di un
parametro deriva dal valore base moltiplicato dai fattori attivi. Per questo
l'eventuale pressione viene trasformata in `BrushInput` prima che `DabGenerator`
calcoli i dab, e l'ordine del flusso è importante. L'importatore `.sut` decodifica le
strutture binarie di curva/effector e non deve essere confuso con il renderer di
impronte.

La passata "finale" è necessaria quando la forma dipende dalla fine del tratto
(taper/zone end) o dalla semplificazione. Il seed ripristinato fa sì che le punte
random non cambino in modo arbitrario tra preview e passata definitiva.

#### Documento e comandi

| Sorgente | Ruolo |
|---|---|
| `document/Ids.h` | Tipi distinti e valore nullo degli ID di cel, disegno e livello. |
| `document/Tile.h`, `TiledImage.h/.cpp` | Pixel e immagine sparsa copy-on-write. |
| `document/Drawing.h` | Proprietà e livelli ordinati di un disegno; contiene anche i tipi di blend e layer. |
| `document/Cel.h/.cpp` | Proprietà di una riga, disegni e timing; selezione del disegno esposto. |
| `document/Timing.h/.cpp` | Sequenza normalizzata di esposizioni e operazioni temporali. |
| `document/Document.h/.cpp` | Proprietà complessive, collezione di cels e allocazione/restauro ID. |
| `document/Command.h` | Interfaccia undo/redo e descrizione di `ChangeSet`. |
| `document/History.h/.cpp` | Coda undo/redo, limite memoria/passi e stato pulito. |
| `document/commands/GroupCommand.h/.cpp` | Aggrega più comandi come un'azione unica. |
| `document/commands/PaintCommand.h/.cpp` | Cambiamenti raster catturati prima/dopo, incluse le tile. |
| `document/commands/DrawingCommands.h/.cpp` | Creazione, rimozione e modifiche ai disegni. |
| `document/commands/CelCommands.h/.cpp` | Inserimento, rimozione, spostamento e proprietà dei cels. |
| `document/commands/LayerCommands.h/.cpp` | Modifica di struttura e proprietà dei livelli. |
| `document/commands/TimingCommands.h/.cpp` | Cambiamenti di esposizione e struttura dei frame. |
| `document/commands/AudioCommands.h/.cpp` | Impostazioni/traccia audio come operazioni reversibili. |
| `document/commands/CameraCommands.h/.cpp` | Aggiunta e modifica della camera del documento. |
| `document/commands/CompositingCommands.h/.cpp` | Modifiche reversibili alla descrizione del grafo. |
| `document/commands/MarkerCommands.h/.cpp` | Modifica dei marcatori temporali. |
| `document/commands/MotionCommands.h/.cpp` | Modifica delle chiavi e del pivot del motion. |
| `document/commands/BlenderLinkCommands.h/.cpp` | Persistenza reversibile del collegamento cel/scena Blender. |
| `document/Blend.h/.cpp` | Operazioni software per le modalità di fusione non delegate a QPainter. |
| `document/Compositing.h` | Strutture dati Qt-free con cui il documento memorizza il grafo. |
| `document/Audio.h` | Dati della traccia, bytes condivisi, offset e impostazioni; non riproduce audio. |
| `document/Camera.h/.cpp` | Chiavi e interpolazione dell'inquadratura. |
| `document/CelMotion.h/.cpp` | Animazione del cel per canali indipendenti (posizione, rotazione, scala, opacità). |
| `document/BlenderLink.h` | Riferimento persistente a file e scena Blender di un cel. |

Un comando dovrebbe catturare le informazioni che servono per ripetere esattamente
l'operazione: ID assegnati, oggetti rimossi, valori precedenti e dati pixel. `apply()`
è usato sia per la prima applicazione sia per il redo; `revert()` è invocato solo
quando il modello è nello stato atteso, come garantito dalla `History`.

#### Algoritmi del core non appartenenti al pennello

| Sorgente | Ruolo |
|---|---|
| `fill/FloodFill.h/.cpp` | Ricerca di regioni contigue per il bucket, basata su immagini/mask e tolleranza. |
| `perspective/PerspectiveGrid.h/.cpp` | Geometria 3D/proiettata della griglia, segmenti, linea d'orizzonte e dati usati da snap/gizmo. |
| `puppet/Geometry.h` | Tipi e operazioni geometriche di base del rig. |
| `puppet/Contour.h/.cpp` | Maschera raster, contorni delle componenti e semplificazione dei profili. |
| `puppet/Triangulation.h/.cpp` | Mesh triangolare vincolata usata dal warp. |
| `puppet/ArapDeformer.h/.cpp` | Solver di deformazione As-Rigid-As-Possible con vincoli dei pin. |
| `puppet/PuppetRig.h/.cpp` | Mesh, pin, stato deformato e operazioni di drag/remesh. |
| `puppet/WarpRaster.h/.cpp` | Campionamento dei pixel attraverso i triangoli deformati. |
| `tween/Flow.h/.cpp` | Immagini di luminanza e campo di spostamento per registrare due pose. |
| `tween/Morph.h/.cpp` | Interpolazione raster guidata dal campo di flusso. |

I dettagli numerici del warp sono in [puppet-warp.md](puppet-warp.md); quelli della
griglia in [perspective-layer.md](perspective-layer.md). La UI e il core del tween
sono distinti: `core/tween` stima e applica il flusso; `document/Tween` associa tale
operazione ai livelli/disegni Qt e produce anteprime e risultati raster.

### Input e canvas

#### Adattatore di input

| Sorgente | Ruolo |
|---|---|
| `input/TabletInputAdapter.h/.cpp` | Conversione stateless degli eventi Qt a `PenSample` e descrizione del dispositivo. |
| `input/ProximityFilter.h/.cpp` | Gestione della prossimità, utile per distinguere hover, entrata e uscita dalla portata. |
| `input/TabletBackend.h/.cpp` | Interfaccia e selezione del backend tablet. |
| `input/TabletBackend_nowintab.cpp` | Implementazione standard senza Wintab dedicato. |
| `input/TabletBackend_wintab.cpp` | Selezione Wintab su Windows, opzionale e confinata all'uso di API private Qt. |

Il canvas non consulta direttamente `QTabletEvent` per calcolare i dab. L'adattatore
converte gli eventi e il flusso successivo usa il formato comune. Questa scelta è
utile sia per la portabilità sia per i test costruiti senza hardware.

#### Strumenti e servizi del canvas

| Sorgente | Responsabilità |
|---|---|
| `CanvasView.h/.cpp` | Widget centrale; dispatch input, lifecycle del tratto, aggiornamento immagine, overlay e strumenti attivi. |
| `CanvasTool.h` | Contratto comune per strumenti del canvas che gestiscono gesti/edit propri. |
| `FrameRenderer.h/.cpp` | Composizione del frame e aggiornamento intero o per rettangolo. |
| `TileQt.h/.cpp` | Ponte tra le tile del core e `QImage`/primitive Qt. |
| `StrokeSession.h/.cpp` | Rasterizzazione live del tratto e passata conclusiva; snapshot e maschera temporanea. |
| `DabStamper.h/.cpp` | Applica singoli dab o punte a un'immagine. |
| `BlendDraw.h/.cpp` | Disegno e blending di immagini/livelli nel contesto Qt. |
| `ViewTransform.h/.cpp` | Coordinate documento ↔ widget, zoom, rotazione, riflessioni e centro. |
| `NavigationGesture.h/.cpp` | Stato dei gesti pan, zoom e rotazione in corso. |
| `Resample.h/.cpp` | Filtri per la ricampionatura di immagini trasformate. |
| `SelectionMask.h/.cpp` | Maschera pixel derivata da una selezione vettoriale e antialiasing ai bordi. |
| `PerspectiveGizmo.h/.cpp`, `PerspectiveSnap.h/.cpp` | Manipolazione e aggancio alla griglia prospettica. |
| `CameraGizmo.h/.cpp` | Manipolazione visuale dell'inquadratura della camera. |
| `DebugOverlay.h/.cpp` | Overlay diagnostico dei campioni e del dispositivo di input. |

Gli strumenti concreti sono:

- `MoveTool`: sposta pixel o livelli/cels, secondo target e modalità keyframe;
- `EyedropperTool`: campiona il colore dal contenuto visualizzato;
- `FillTool`: avvia bucket o riempimento lasso, con tolleranza e opzioni di sorgente;
- `SelectionTool`: crea e modifica selezioni rettangolari o a mano libera;
- `TransformTool`: trasforma l'area o i pixel selezionati mantenendo la modifica
  aperta fino ad applicazione/annullamento;
- `PuppetTool`: apre il rig, gestisce pin o drag locale e applica il raster deformato;
- `ShapeTool`: linee, rettangoli, rettangoli arrotondati, ellissi, poligoni e curve;
- `EffectBrushTools`: strumenti a pennello per fusione e liquify;
- `AnimateTool`: strumenti di animazione/motion del cel e manipolazione delle pose.

Il canvas contiene anche regole che non sono operazioni sui pixel: tasti di
navigazione, gesto mano/zoom/rotazione, snap prospettico e disegno della selezione.
Una trasformazione della vista modifica come la tela viene osservata; una
trasformazione del contenuto produce invece un comando sul documento.

### Stato strumenti, librerie e preferenze

| Sorgente | Ruolo |
|---|---|
| `tools/ToolState.h/.cpp` | Stato condiviso letto dal canvas e modificato dai pannelli: strumento, colori, dimensione, opacità, dinamiche e opzioni. |
| `tools/BrushDefinition.h` | Definizione immutabile condivisa di stile e immagini di punta del pennello. |
| `tools/BrushPreset.h/.cpp` | Preset/sub-tool nominati e loro applicazione a `ToolState`. |
| `tools/BrushLibrary.h/.cpp` | Raccolta e selezione delle definizioni/preset di pennello. |
| `tools/SutImporter.h/.cpp` | Importazione di definizioni `.sut` tramite database/risorse del formato. |
| `tools/ColorSetLibrary.h/.cpp` | Palette di colori salvate e selezione delle tinte. |
| `tools/Preferences.h/.cpp` | Preferenze applicative persistenti non appartenenti a un singolo documento. |

È essenziale distinguere tre livelli di configurazione:

1. `BrushStyle`: comportamento del motore (dynamics, punte, spacing);
2. `ToolState`: valore attivo e temporaneo con cui il canvas avvierà il prossimo
   tratto;
3. `BrushPreset`: configurazione nominata selezionabile dalla UI, che può impostare
   solo parte dello stato.

Una definizione immutabile può essere condivisa da un preset e da più tratti; le
impostazioni di dimensione e colore attuali restano invece nello stato dello strumento.
Il colore trasparente selezionato equivale all'uso della gomma, ma il target della
gomma può essere una maschera: in tal caso i valori dipinti sono interpretati come
zone da nascondere/scoprire.

### Timeline e riproduzione

| Sorgente | Ruolo |
|---|---|
| `ui/panels/timeline/TimelineMetrics.h` | Conversione coerente tra frame/pixel timeline e dimensioni delle celle. |
| `ui/panels/timeline/TimelineView.h/.cpp` | Vista della griglia dei cels, esposizioni e interazioni dirette con la timeline. |
| `ui/panels/timeline/TimelinePanel.h/.cpp` | Contenitore con controlli e azioni per la timeline. |
| `ui/panels/timeline/TimelinePlayer.h/.cpp` | Riproduzione frame-time basata su clock, con gestione di stop e sincronizzazione audio. |
| `ui/AutoplayWindow.h/.cpp` | Finestra per riprodurre l'animazione e coordinare immagine/audio in playback. |

La timeline presenta almeno due assi: cels (righe) e frame (colonne). Il contenuto
della cella dipende dall'esposizione del cel in quell'intervallo; spostare un hold
modifica il timing, non necessariamente i disegni. Le operazioni che creano disegni o
spostano intervalli sono delegate al controller, così i controlli grafici non devono
replicare le regole di `Timing`.

Durante il playback la UI chiede al canvas di mostrare frame calcolati dal clock.
L'audio usa il frame corrente per calcolare la posizione attesa nella traccia; una
correzione periodica può riallineare il player se il disallineamento supera la
tolleranza. In scrub, il controller può chiedere un breve burst di audio solo se è
abilitato nelle impostazioni della traccia.

### Interfaccia: finestra, pannelli e dialoghi

#### Finestra e disposizione

| Sorgente | Ruolo |
|---|---|
| `ui/MainWindow.h/.cpp` | Workspace e coordinatore delle azioni applicative; apre/salva, crea pannelli e collega i servizi. |
| `ui/DockLayout.h/.cpp` | Posizione iniziale, ripristino e salvataggio delle dock. |
| `ui/PanelDock.h/.cpp` | Contenitore di pannello, collasso e presentazione del dock. |
| `ui/Theme.h/.cpp` | Applicazione del tema di sistema, chiaro o scuro. |
| `ui/Shortcuts.h/.cpp` | Definizione/collegamento delle scorciatoie. |
| `ui/IconFactory.h/.cpp` | Disegno o costruzione delle icone dell'interfaccia. |
| `ui/SplashScreen.h/.cpp` | Immagine e presentazione dello splash di avvio. |
| `ui/DiscordPresence.h/.cpp` | Integrazione della rich presence Discord, quando disponibile. |

`MainWindow` è il composition root Qt: crea gli oggetti condivisi e ne collega i
segnali. Un pannello non dovrebbe istanziare un secondo `DocumentController` per
ottenere una vista propria del documento; deve riferirsi al controller di workspace.
`DockLayout` è il luogo in cui si trovano disposizione iniziale e persistenza delle
dock, non ciascun pannello singolarmente.

#### Pannelli e barre

| Sorgente | Cosa presenta/controlla |
|---|---|
| `ui/panels/ToolPalettePanel.*` | Scelta degli strumenti e scorciatoie di selezione/pressione prolungata. |
| `ui/panels/SubToolPanel.*` | Preset e modalità del tool corrente. |
| `ui/panels/ToolPropertiesPanel.*` | Parametri specifici del tool attivo. |
| `ui/AdvancedToolSettings.*` | Impostazioni estese per dinamiche e funzionalità avanzate. |
| `ui/panels/BrushSlidersPanel.*`, `BrushSizePanel.*` | Controlli rapidi per dimensione, opacità e proprietà principali del pennello. |
| `ui/panels/PressureCurveEditor.*` | Visualizzazione e modifica della curva pressione-output. |
| `ui/panels/ColorWheelPanel.*`, `ColorWheel.*` | Selezione e rappresentazione del colore. |
| `ui/panels/ColorSetPanel.*` | Palette/insiemi di colori. |
| `ui/panels/LayersPanel.*` | Struttura e proprietà dei livelli e selezione del contesto di lavoro. |
| `ui/panels/NavigatorPanel.*` | Miniatura e controllo della vista del canvas. |
| `ui/panels/timeline/*` | Esposizioni, frame, cels e playback. |
| `ui/SelectionBar.*` | Azioni contestuali alla selezione. |
| `ui/TransformBar.*` | Comandi per la trasformazione aperta. |
| `ui/ShapeBar.*` | Opzioni contestuali delle forme. |
| `ui/PuppetBar.*` | Azioni e impostazioni contestuali del puppet warp. |

I controlli numerici condivisi sono implementati da `SliderField` e
`VerticalValueSlider`; la gestione delle modalità di fusione usate nell'interfaccia
sta in `panels/BlendModes.h`. Il pattern generale è: il pannello legge lo stato,
emette un'azione o modifica un oggetto condiviso, il controller/canvas applica la
semantica, i segnali aggiornano gli altri consumer.

#### Dialoghi e finestre specializzate

| Sorgente | Scopo |
|---|---|
| `ui/NewProjectDialog.*` | Dimensioni, fps, durata e proprietà iniziali del documento. |
| `ui/ImportSequenceDialog.*` | Sequenza immagini: inizio e hold per immagine. |
| `ui/ExportDialog.*`, `ui/VideoExportDialog.*` | Opzioni per immagine/serie o video. |
| `ui/SettingsDialog.*` | Preferenze applicative. |
| `ui/BackupDialog.*` | Recupero o apertura delle copie di backup. |
| `ui/TweenDialog.*` | Interpolazione dei disegni fra due pose. |
| `ui/BlenderSessionDialog.*`, `ui/BlenderLiveLink.*` | Scelta sessione e aggiornamento da Blender. |
| `ui/compositing/CompositingWindow.*` | Workspace dell'editor a nodi. |
| `ui/compositing/NodeGraphView.*` | Interazione grafica con nodi e collegamenti. |
| `ui/compositing/ImagePreview.*` | Visualizzazione del risultato o di un nodo in anteprima. |
| `ui/compositing/RangeSlider.*` | Controllo di intervallo usato dall'editor di compositing. |
| `ui/Checkerboard.h` | Presentazione del fondo trasparente. |

### Compositing

#### Strutture, tipi e valutazione

| Sorgente | Ruolo |
|---|---|
| `compositing/Graph.h/.cpp` | Modello modificabile dei nodi e collegamenti; valida socket e cicli. |
| `compositing/NodeType.h/.cpp` | Contratto dei tipi, parametri, categorie e registro dei nodi disponibili. |
| `compositing/Evaluator.h/.cpp` | Valutazione delle dipendenze, cache dei risultati e richieste di output/ingresso. |
| `compositing/ImageOps.h/.cpp` | Primitive di elaborazione immagine riutilizzate dai nodi. |
| `compositing/MaskData.h/.cpp` | Dati di maschera associati a nodi che operano su regioni. |
| `core/document/Compositing.h` | Rappresentazione persistente Qt-free del grafo nel documento. |
| `ui/compositing/*` | Editor visuale e anteprime. |

`Graph` è un grafo diretto aciclico. Un input può avere un collegamento; una
connessione non valida (socket inesistente, stesso nodo o ciclo) viene rifiutata. I
tipi dei nodi sono registrati in `NodeRegistry`: l'identificatore persistito non va
rinominato senza una migrazione perché i file di progetto lo memorizzano.

I risultati sono immagini Qt, ma i dati del grafo nel documento non includono immagini
calcolate. L'`EvalContext` dà ai nodi il frame sorgente, l'indice temporale, una scala
di preview e il colore carta. I nodi possono dichiarare dipendenze dal frame: la
cache deve invalidare in modo diverso un effetto statico e uno che genera variazioni
temporali.

#### Nodi inclusi

I tipi concreti si trovano in `src/compositing/nodes/`. Il nome indica la famiglia
funzionale; per socket, range e parametri esatti consultare la rispettiva classe:

- **Input/output e composizione**: `FrameNode`, `OutputNode`, `MergeNode`, `MaskNode`.
- **Colore e livelli**: `ColorNode`, `ColorCorrectNode`, `LevelsNode`, `OpacityNode`.
- **Sfocatura e dettaglio**: `BlurNode`, `DirectionalBlurNode`, `SharpenNode`.
- **Effetti luce/ombra**: `GlowNode`, `DropShadowNode`, `VignetteNode`.
- **Geometria ottica e trasformazioni**: `TransformNode`, `LensDistortionNode`,
  `ChromaticAberrationNode`.
- **Aspetto/grana**: `SensorNoiseNode`.

Per aggiungere un tipo, implementare `NodeType`, registrarlo in `NodeType.cpp` e
verificare insieme editor, serializzazione, valutazione, cache e test. Se il nodo
possiede dati complessi oltre ai parametri numerici, definirne la codifica e
l'invalidazione; il testo del nodo viene conservato separatamente dai normali
parametri.

### Persistenza, import/export e diagnostica

| Sorgente | Ruolo |
|---|---|
| `io/ProjectFile.h/.cpp` | Serializzazione SQLite e migrazioni del formato `.moth`. |
| `io/ProjectThumbnail.h` | Generazione/descrizione della miniatura incorporata. |
| `io/Autosave.h/.cpp` | Copie periodiche, metadati, recupero, ritiro e potatura. |
| `io/FrameExport.h/.cpp` | Composizione e salvataggio di frame/sequenze PNG o JPEG. |
| `io/VideoExport.h/.cpp` | Rendering finale e pipe di frame al processo ffmpeg. |
| `io/BlenderLinkServer.h/.cpp` | Server locale, sessioni e richieste di frame Blender. |
| `io/BlenderFrameCache.h/.cpp` | Riutilizzo dei frame ricevuti dal collegamento. |
| `io/BlenderAddOn.h/.cpp` | Distribuzione/installazione dell'add-on integrato. |
| `diagnostics/CrashHandler.h/.cpp` | Log, report crash, hang watch e salvataggio d'emergenza. |
| `shell/ThumbnailProvider.cpp` | Provider COM Win32 per miniature di file in Explorer. |

SQLite `.moth` e SQLite `.sut` sono formati diversi con schemi diversi: il fatto che
entrambi usino SQLite non implica compatibilità. Nel file progetto il numero
`user_version` governa le migrazioni. Un nuovo campo persistente richiede di
aggiornare schema, lettura, scrittura, valore predefinito per file vecchi e test
round-trip; se cambia la semantica, considerare anche dati corrotti o valori
inattesi.

`Autosave` ha un'identità di progetto, una data e uno stato ritirato per ogni copia.
Salvataggio riuscito o chiusura intenzionale impediscono che quella copia venga
proposta come recupero, ma possono lasciarla temporaneamente tra i backup. Il
controller/window stabilisce quando fermare un tratto e quando è sicuro fare la
copia.

Il watcher di blocco monitora la risposta del loop UI; non è lo stesso caso di un
crash nativo. Il report viene scritto anche se il processo non si chiude subito.
Report e log possono includere percorsi dei progetti e informazioni di sistema:
prima di condividerli, verificare che non contengano dati privati.

### Audio

`doc::AudioTrack` mantiene i byte della traccia nel progetto, così il file audio
segue il documento anche se si sposta quest'ultimo. È un oggetto dati: non dipende
dal player o dal device audio. `AudioPlayer` avvolge `QMediaPlayer` e `QAudioOutput`,
decodifica la traccia e converte tra indice frame, fps e tempo in millisecondi.
`AudioAnalyzer` è un modulo distinto che estrae informazioni audio per le feature che
lo richiedono.

L'offset è espresso in frame relativamente all'inizio della scena: può essere
negativo se l'audio comincia prima del primo fotogramma. Volume, mute e opzione
scrub appartengono alla traccia e sono persistenti; il silenziamento temporaneo della
finestra di playback è uno stato di sessione e non modifica il documento.

### Blender Link

Il server Qt ascolta su porta locale e gestisce socket client, buffer dei messaggi,
sessioni e richieste. Ogni sessione identifica un Blender e la scena aperta. Le
richieste possono chiedere più frame con dimensioni, shading, compositing e
trasparenza; i frame arrivano uno per volta e una richiesta si conclude con stato
success/cancel/error.

`BlenderLiveLink` nella UI traduce i cambiamenti di sessione e le richieste in azioni
documentali. `BlenderFrameCache` riduce i render duplicati. Il documento associa un
cel al file e alla scena Blender per poter aggiornare la sequenza successivamente.
Il protocollo applicativo dell'add-on è implementato in
[`blender/mothion_link/__init__.py`](../blender/mothion_link/__init__.py), mentre
l'archivio installabile viene incluso da CMake come risorsa.

### Test: come scegliere il bersaglio

| Se cambia... | Iniziare da... |
|---|---|
| Regole di timing, tile, curve o geometria pura | `tst_core`, `tst_document`, `tst_brush`, `tst_puppet`, `tst_tween`, `tst_camera`, a seconda del modulo. |
| Un comando o la cronologia | `tst_commands` e/o `tst_document`. |
| Salvataggio, migrazione o thumbnail | `tst_projectfile` (e il test Windows della shell, se esistente). |
| Conversione/rasterizzazione del canvas | `tst_canvas` o `tst_ui_drawing`. |
| Nodo o invalidazione del grafo | `tst_compositing` e `tst_ui_compositing`. |
| Funzione di timeline o esposizione | `tst_ui_timeline`, oltre al test core del timing. |
| Dialogo o interazione pannello | test `tst_ui_<area>` corrispondente. |
| Audio o controller dell'audio | `tst_ui_audio` e test di documento/commands se cambia lo stato persistente. |
| Protocollo/sessione Blender | `tst_blenderlink` e `tst_ui_window` per l'integrazione con la finestra. |
| Importazione `.sut` o palette | `tst_brushimport` o `tst_colorsets`. |

Un buon test verifica la semantica attraverso un risultato osservabile: per esempio,
che `revert()` ripristini pixel e struttura, che un file caricato abbia lo stesso
modello del file salvato, oppure che l'output renderizzato contenga il frame atteso.
Preferire test del core per le invarianti e test UI per il cablaggio/gesto, senza
duplicare in ogni test lo stesso algoritmo.

Per l'esecuzione dettagliata, locale `C`, offscreen e sanitizzatori, usare i comandi
del [README](../README.md#continuous-integration).

### Checklist per le modifiche trasversali

Una feature completa spesso tocca più di un solo file. Prima di considerarla finita,
rispondere a queste domande:

- **Proprietà:** chi possiede il dato? È temporaneo, globale alle preferenze o parte
  del progetto?
- **Modificabilità:** va annullato? È un preview durante il drag o una modifica già
  consolidata?
- **Notifica:** quale `ChangeSet` descrive correttamente la modifica? Quali viste
  devono aggiornarsi?
- **Target:** la feature deve rispettare livello bloccato/nascosto, cel selezionato,
  maschera, selezione o frame vuoto?
- **Rendering:** canvas, playback, export still e export video devono mostrare lo
  stesso risultato? Guide, camera, alpha e compositing sono inclusi coerentemente?
- **Persistenza:** file vecchi hanno un default definito? Il dato viene serializzato
  e deserializzato senza perdita?
- **Prestazioni:** l'aggiornamento può essere parziale? La cache ha una chiave e
  invalidazione corrette? La UI resta reattiva?
- **Test e docs:** c'è almeno un test di semantica e, se serve, uno di wiring UI?
  README, guida e nota specialistica descrivono la feature allo stesso modo?

### Cosa questa documentazione non promette

Mothion è in evoluzione. Questa documentazione descrive architettura e responsabilità
osservate nei sorgenti, ma non garantisce stabilità binaria delle API, comportamento
di ogni driver tablet o compatibilità di Blender/Qt al di là delle versioni testate.
I dettagli di ciascun parametro, ogni ramo di errore e ogni formato esterno vanno
verificati nell'header/implementazione e nei test del modulo interessato.
