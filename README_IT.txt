SXE Core - Strategy eXecution Engine
====================================

Motore C++17 che esegue strategie di trading caricate come librerie esterne, con un simulatore
realistico dei futures perpetui con margine in USDT.

La strategia e' compilata come libreria separata (.so) e non sa da dove arrivano i prezzi.
Il motore la carica all'avvio, ne controlla l'interfaccia, le passa i dati del mercato passo
dopo passo ed esegue i suoi ordini su un exchange simulato che riproduce le regole di uno
vero: commissioni, slippage, margine isolated/crossed, margine di mantenimento a scaglioni,
funding ogni 8 ore e liquidazione.

Versione 1.0.1 - modifiche in CHANGELOG.txt (in inglese)
English version: README.txt


PUNTI DI FORZA
--------------

* Strategie come plugin
  Si scrive una classe, si aggiunge una macro, si compila una .so. Il motore rifiuta le
  librerie che non sono strategie o che sono state compilate con un'altra versione
  dell'interfaccia. Le strategie si possono compilare anche fuori dal progetto con una sola
  funzione CMake.

* Motore deterministico a thread unico
  Un passo = dati -> funding -> exchange -> Run() della strategia.
  Stessi dati, stesso risultato al centesimo: verificato dai test golden.

* Simulazione realistica dei futures (sul modello di Bitget BTCUSDT perpetuo, tutto
  configurabile)
  - commissione taker e slippage su ogni esecuzione, passo della quantita', quantita' minima,
    valore minimo dell'ordine
  - margine isolated o crossed
  - margine di mantenimento a scaglioni progressivi (ogni fascia della posizione paga il tasso
    del suo scaglione) e calcolo del prezzo di liquidazione scaglione per scaglione
  - funding ogni 8 ore da un file storico, con un tasso di riserva e un moltiplicatore per le
    prove di stress
  - liquidazione: in isolated si perde il margine della posizione e si continua,
    in crossed si perde il conto e la simulazione si ferma

* Percorso del prezzo dentro la candela
  Ogni candela viene percorsa come una sequenza di tick (solo chiusura, secondo la direzione,
  prima il minimo o prima il massimo) con tick intermedi facoltativi: ordini e liquidazioni
  avvengono dentro la candela. La strategia vede solo candele chiuse: niente prezzi futuri.

* Archivio dei cicli
  Un ciclo va dal primo acquisto alla chiusura completa. Alla chiusura il motore crea
  Backup/b<N> con il riepilogo result.txt, ci sposta tutti i file di log del ciclo e aggiunge
  una riga a total_result.txt.

* Ripresa
  Si ferma una simulazione con Ctrl+C e la si riprende dopo: il risultato e' identico a una
  corsa senza interruzioni.

* Cartella dei dati e periodo configurabili
  I dati storici possono stare in qualsiasi cartella ([PATH] data). Il periodo da simulare si
  indica per mese (2023-01 -> 2025-10) o per giorno, con un avviso se i dati non lo coprono.

* Configurazione rigorosa
  Ogni chiave viene controllata all'avvio e gli errori vengono segnalati tutti insieme.

* Veloce
  Circa 700.000 passi al secondo su un Raspberry Pi 4: due anni di candele da 1 ora con i tick
  dentro la candela girano in 0,2 secondi.

* Testato
  13 test CTest su dati di mercato reali fissi (BTCUSDT 2023-2024).


COME FUNZIONA
-------------

    file candele + funding
              |
              v
       +-------------+       +---------------------------------------+
       |  Data feed  | ----> |  Motore (thread unico), a ogni passo: |
       |  tick dentro|       |    1. prossimo tick                   |
       |  la candela |       |    2. funding ogni 8 ore              |
       +-------------+       |    3. exchange: equity, liquidazione  |
                             |    4. strategia.Run(ctx)              |
                             |    5. archivio cicli chiusi, stato    |
                             +---------------------------------------+
                                  |              |              |
                                  v              v              v
                         +--------------+  +-----------+  +---------------+
                         |  Exchange    |  | Strategia |  |  Target/Log   |
                         |  simulato    |  | plugin    |  |  Target/Backup|
                         |  commissioni,|  | libX.so   |  |               |
                         |  margine,    |  |           |  |               |
                         |  scaglioni   |  |           |  |               |
                         +--------------+  +-----------+  +---------------+

La strategia implementa cinque metodi, chiamati in quest'ordine:

    GetInfo()             una volta     nome, versione, descrizione
    LoadConfig(percorso)  una volta     legge il suo .ini
    Init(InitContext)     una volta     conto e posizione iniziali, regole dell'exchange,
                                        cartelle di log e backup
    Run(StepContext&)     ogni passo    decide, piazza gli ordini con ctx.orders, ritorna
    Term()                una volta     salvataggi finali e riepilogo

Run() esegue un passo e ritorna: il ciclo e' del motore. La strategia non legge l'orologio,
non usa la rete e non parla direttamente con l'exchange. Tempo, prezzi, conto e posizione
arrivano nel contesto; gli ordini passano da ctx.orders.MarketBuy(usdt, leva) e
ctx.orders.Close(), che sono sincroni.


AVVIO RAPIDO
------------

Requisiti: Linux, CMake 3.16 o successivo, un compilatore C++17
(provato con GCC 14 su Raspberry Pi OS 64 bit).

    ./comp_All.sh test        compila, installa in Target/ e lancia i test
    Target/Bin/SXE            esegue la strategia di esempio sui dati inclusi nel repository

Target/Bin/SXE percorso/altro.ini usa un'altra configurazione. Una riga di stato mostra data,
avanzamento, prezzo ed equity. Alla fine il motore stampa un riepilogo (rendimento, drawdown
massimo, cicli, liquidazioni, commissioni, funding, passi al secondo) e lascia una cartella
Target/Backup/b<N> per ciclo.

Opzioni di compilazione:

    ./comp_All.sh             build Release
    ./comp_All.sh debug       build Debug
    ./comp_All.sh test        build Release + test
    ./comp_All.sh clean       cancella la cartella di lavoro di CMake CMakeWork/


USARE I PROPRI DATI
-------------------

In Target/Config/EngineConfig.ini:

    [PATH]
    data = /percorso/dei/tuoi/MarketData     cartella dei dati storici

    [SIMULATION]
    data_file    = BTCUSDT_1h.csv            nella cartella dei dati (o percorso assoluto)
    funding_file = BTCUSDT_funding.csv       nella cartella dei dati, vuoto = funding_rate fisso
    start_date   = 2023-01
    end_date     = 2025-10

I percorsi relativi di EngineConfig.ini partono da Target/Bin, quindi il motore funziona da
qualunque cartella venga lanciato.

Formato dei file:

    candele   date;time(UTC);open;high;low;close;volume
              2023-01-01;00:00;16541.77;16545.7;16508.39;16529.67;4364.8357

    funding   timestamp,funding_rate
              2023-01-01 00:00,0.00010000


SCRIVERE UNA STRATEGIA
----------------------

1. Copiare file/Strategies/Template (incrocio di medie mobili) in una nuova cartella e
   rinominare la classe e NAME nel suo CMakeLists.txt. Copiare Target/Config/Template.ini
   come configurazione della nuova strategia.

2. Implementare i metodi di CStrategyBase (file/SDK/StrategyBase.h).

3. Lasciare la macro di esportazione in fondo al .cpp:

       SXE_EXPORT_STRATEGY(CMiaStrategia)

4. Ogni cartella di file/Strategies con un CMakeLists.txt viene compilata da sola.
   Oppure la si compila in un progetto separato, con SXE Core gia' installato:

       cmake_minimum_required(VERSION 3.16)
       project(MieStrategie CXX)
       include(<cartella di SXE>/cmake/SXEStrategy.cmake)
       sxe_add_strategy(NAME Mia SOURCES CMiaStrategia.cpp CONFIG Mia.ini)

   Questo installa libMia.so in Target/Bin/Strategies e Mia.ini in Target/Config.

5. Selezionarla in EngineConfig.ini:

       [STRATEGY]
       library = libMia.so
       config  = ../Config/Mia.ini

I file che la strategia scrive in InitContext::strLogDir vengono archiviati con il ciclo
automaticamente.

Una strategia che salva un suo stato (per esempio accanto al suo .ini) lo ricarica solo se
InitContext::bResume e' true, cioe' quando il motore riprende una simulazione salvata. Se e'
false la corsa parte da capo e lo stato lasciato da una corsa precedente va ignorato.


CONFIGURAZIONE
--------------

Target/Config/EngineConfig.ini, ogni chiave e' commentata nel file:

    [ENGINE]       modalita' (SIM), simbolo
    [STRATEGY]     libreria della strategia e suo file di configurazione
    [PATH]         cartelle Log, Backup e dati storici
    [EXCHANGE]     modalita' di margine, commissione taker, slippage, scaglioni del margine di
                   mantenimento, passo della quantita', valore minimo dell'ordine
    [SIMULATION]   periodo, capitale iniziale, file dei prezzi e del funding, funding di
                   riserva e moltiplicatore, percorso dentro la candela, liquidazione si'/no,
                   ripresa, file di stato e degli ordini
    [LOG]          messaggi info / warning / error / riga sul terminale


RISULTATI
---------

    Target/Log/                        file del ciclo in corso: log.txt, error.txt, registro
                                       degli ordini, file della strategia, stato della
                                       simulazione
    Target/Backup/b<N>/                una cartella per ciclo chiuso: result.txt
                                       (apertura/chiusura, prezzi, crescita, drawdown, leva
                                       massima, commissioni, funding) piu' i file spostati
                                       da Log
    Target/Backup/total_result.txt     una riga per ciclo, pronta per un foglio di calcolo


TEST
----

./comp_All.sh test lancia 13 test su dati reali fissi (file/Tests/Data: BTCUSDT 1h e funding,
2023-2024):

    loader_not_strategy     il motore rifiuta una libreria che non e' una strategia,
    loader_old_api          compilata con un'altra versione dell'interfaccia,
    loader_missing          o assente
    config_example          l'EngineConfig.ini di esempio e' valido
    golden_template         la strategia Template da' esattamente il risultato di riferimento
    golden_isolated         ordini dentro la candela, funding, liquidazione (margine isolated)
    golden_crossed          lo stesso con margine crossed
    golden_low_first        percorso estremo dentro la candela: prima il minimo
    golden_high_first       percorso estremo dentro la candela: prima il massimo
    golden_bitget           parametri reali dell'exchange: margine a scaglioni, slippage
                            basso, moltiplicatore del funding
    no_liquidation_warning  con liquidation = 0 il motore avvisa quando l'equity va sotto
                            zero: il risultato non e' realistico
    resume                  una corsa fermata a meta' candela e ripresa da' gli stessi cicli
                            di una senza interruzioni
    benchmark               passi al secondo (solo misura)

I test golden confrontano ogni ciclo con i riferimenti in file/Tests/Golden: qualsiasi
modifica al simulatore che cambia anche un solo ciclo li fa fallire. Dopo una modifica voluta
i riferimenti si rigenerano (e vanno ricontrollati) con:

    SXE_UPDATE_GOLDEN=1 ctest --test-dir CMakeWork -R golden


STRUTTURA DEL PROGETTO
----------------------

    file/                       tutti i sorgenti, una cartella per componente
                                (vedi file/README.txt)
    file/Engine                 ciclo del motore, caricamento della strategia, archivio dei
                                cicli, stato
    file/Exchange               interfaccia dell'exchange ed exchange simulato
    file/DataFeed               interfaccia dei dati e lettore CSV con i tick nella candela
    file/SDK                    gli unici header che servono a una strategia
    file/Settings               configurazione e lettore INI
    file/Common                 logger, thread, date, tipi comuni
    file/Strategies/Template    strategia di esempio
    file/Tests                  test, dati fissi, riferimenti golden
    cmake/                      sxe_add_strategy, anche per strategie fuori dal progetto
    Target/Config               configurazione di esempio: EngineConfig.ini, Template.ini
    Target/Bin, Log, Backup     create dalla compilazione e dalle esecuzioni (non sono nel
                                repository)


LIMITI DELLA VERSIONE PUBBLICA
------------------------------

* Solo simulazione: mode = REAL e' riservato al trading reale, che non fa parte di questa
  versione.
* Posizioni long con ordini a mercato (apertura/aumento, chiusura completa), un simbolo per
  avvio.
* Solo Linux (le strategie vengono caricate con dlopen).


VERSIONE PUBBLICA E VERSIONE PRIVATA
------------------------------------

Questo repository e' la versione pubblica di SXE Core, ferma alla versione descritta qui.
Le funzionalita' del motore sviluppate dopo fanno parte della versione privata dell'autore e
sono elencate qui sotto.

    Tuning dei parametri                                                   in sviluppo
        Ottimizzazione dei parametri della strategia con piu' avvii: tante simulazioni in
        parallelo sugli stessi dati, classifica per rendimento, drawdown e liquidazioni,
        verifica su un periodo non usato per il tuning. Programma separato: SXE continua a
        funzionare da solo anche senza.
        Fatto: piu' motori indipendenti nello stesso processo, in parallelo su tutti i core,
        con i dati di mercato letti una volta sola; corse leggere senza i file dei cicli,
        circa 10 volte piu' veloci di una corsa completa, con risultati identici al centesimo.
        Fatto: il programma SXE_Tune. Optimize.ini elenca i parametri della strategia da
        far variare (intervalli, elenchi o valori fissi) e SXE_Tune prova tutte le
        combinazioni (ricerca a griglia) su tutti i core; ogni corsa e' scritta in un file
        CSV appena finisce, con rendimento, drawdown, cicli, liquidazioni, commissioni e
        funding. Funziona con qualunque strategia: i parametri sono le chiavi del suo .ini.
        In corso: regole tra i parametri (per esempio percentuali che devono sommare a 100),
        prova a vuoto che controlla ogni valore con la strategia prima della prima
        simulazione, ricerca casuale per le griglie grandi, vincoli sui risultati (drawdown
        massimo, nessuna liquidazione, cicli minimi), classifica e il .ini migliore pronto
        da usare.

    Interfaccia grafica                                                       prevista
        Configurazione, simulazione con grafici (prezzo, ordini, equity, drawdown, cicli) e
        tuning dei parametri da una finestra. Programma separato: SXE continua a funzionare
        da solo anche senza.


LICENZA
-------

Copyright (c) 2026 Francesco Tinelli

SXE Core e' distribuito con la licenza PolyForm Noncommercial 1.0.0 (vedi LICENSE): si puo'
studiare, usare e modificare liberamente per qualsiasi scopo non commerciale; l'uso
commerciale richiede il permesso dell'autore.

Il parser INI e' inih di Ben Hoyt (https://github.com/benhoyt/inih), licenza BSD-3-Clause,
vedi file/Settings/LICENSE_inih.txt.


AVVERTENZA
----------

Questo software e' uno strumento di simulazione e ricerca. Non e' una consulenza finanziaria.
I risultati passati, simulati o reali, non garantiscono quelli futuri. Il trading di derivati
con leva puo' far perdere piu' del capitale iniziale.
