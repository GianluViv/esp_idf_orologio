# Riutilizzare la base LVGL/touch in più progetti

> Questo documento **non descrive lo stato attuale del repository** (che
> resta un singolo progetto, senza `components/` separati), ma discute le
> opzioni disponibili per il giorno in cui si vorranno creare progetti
> applicativi diversi sulla stessa board — es. una bussola, una sveglia —
> mantenendo in comune il livello di driver (`display`, `lvgl_port`,
> `touch`, `diagnostics`) e propagando a tutti i progetti i miglioramenti
> fatti a quel livello. Nessuna di queste opzioni è ancora stata messa in
> pratica in questo repo.

## Il problema

Oggi `main/` contiene sia il livello di driver (display, LVGL, touch) sia
l'applicazione (i widget creati in `main.c`). Se in futuro nascono progetti
diversi (bussola, sveglia, ...) che usano la stessa board e vogliono la
stessa base, si pone una domanda: come si mantiene quella base **in comune**
tra i progetti, così che un fix o un miglioramento fatto in uno si propaghi
agli altri, senza doverlo riscrivere o copiare a mano ogni volta?

## Opzione A — un branch Git per progetto

L'idea più immediata: un branch `bussola`, un branch `sveglia`, entrambi
derivati da `main`. È fattibile, ma ha un limite strutturale: i branch sono
pensati per rappresentare *linee di sviluppo dello stesso progetto* (una
feature, una release), non *progetti applicativi indipendenti* che
condividono solo una parte del codice.

Il problema pratico: se `bussola` e `sveglia` sono due branch fratelli (non
uno figlio dell'altro), un fix a `lvgl_port.c` fatto su `bussola` non arriva
automaticamente su `sveglia`. Va portato a mano con un merge o un
`cherry-pick` per ciascun branch — fattibile con due progetti, via via più
scomodo e più a rischio di errore (dimenticare un branch, conflitti di
merge tra codice applicativo diverso) man mano che i progetti aumentano o
divergono nella parte applicativa.

**Quando ha senso**: se i progetti restano molto simili e pochi (2-3), e si
è disciplinati nel propagare ogni fix della base su tutti i branch subito
dopo averlo fatto. Non richiede di riorganizzare il codice come nelle
opzioni B/C.

## Opzione B — estrarre la base in componenti ESP-IDF condivisi (consigliata)

L'approccio più coerente con l'architettura già impostata in questo
progetto (vedi sezione *Architecture* in [CLAUDE.md](../CLAUDE.md), che già
prevede l'estrazione in `components/` quando un modulo "diventa
sostanzioso"):

1. Estrarre `display`, `lvgl_port`, `touch` (e volendo `diagnostics`) da
   `main/` a `components/`, ciascuno con la struttura standard di un
   componente ESP-IDF (`CMakeLists.txt` proprio, header pubblico in
   `include/`).
2. Mettere quei componenti in un **repository Git separato** — es.
   `esp32s3-touch-lcd-169-base` — che diventa la "base comune".
3. In ogni progetto applicativo (bussola, sveglia, questo stesso
   orologio), dichiarare quella base come dipendenza nel
   `main/idf_component.yml` del progetto, puntando al repo Git (il
   Component Manager di ESP-IDF supporta dipendenze via URL Git, non solo
   dal Component Registry ufficiale). Ogni progetto resta un repo Git a sé,
   con solo il proprio codice applicativo in `main/`.

Il vantaggio: un fix o un miglioramento alla base si fa **una sola volta**,
nel repo condiviso; ogni progetto applicativo lo recepisce con un semplice
aggiornamento della versione/tag referenziata in `idf_component.yml` (o
`idf.py update-dependencies`), senza merge incrociati tra progetti che non
sono mai stati branch dello stesso albero.

**Contro**: più struttura da mantenere (un repo in più, versionamento dei
componenti con tag/release), e la separazione va progettata bene — es.
capire cosa resta "generico" (driver display/touch/LVGL) e cosa è già
troppo specifico per un singolo progetto (i widget applicativi restano
sempre in `main/` del progetto specifico, mai nella base condivisa).

## Opzione C — variante con git submodule

Alternativa più "grezza" alla B ma senza usare il Component Manager: gli
stessi componenti estratti in un repo separato vengono inclusi nei progetti
applicativi come **git submodule** dentro `components/`. Aggiornare la base
in un progetto è un `git submodule update --remote`, poi commit del nuovo
riferimento.

Rispetto a B, evita di imparare la sintassi di `idf_component.yml` per
dipendenze Git, ma i submodule sono notoriamente scomodi da usare bene
(facile dimenticare di aggiornarli, stato del submodule disallineato tra
clone diversi). Va bene come opzione più semplice da capire in un contesto
didattico, ma B è la via più idiomatica in ESP-IDF una volta presa
confidenza col Component Manager.

## Opzione D — monorepo con `components/` condiviso

Se si preferisce restare in un unico repository invece di separare la base
in un repo a parte: un unico repo con più cartelle progetto sullo stesso
livello (`orologio/`, `bussola/`, `sveglia/`, ciascuna con il proprio
`main/` e `CMakeLists.txt` di progetto) e un `components/` in comune alla
radice, referenziato da tutti via `EXTRA_COMPONENT_DIRS` nel
`CMakeLists.txt` di ogni progetto. Nessuna duplicazione, nessun problema di
sincronizzazione: un fix ai componenti è immediatamente visibile a tutti i
progetti nello stesso repo.

**Contro**: tutti i progetti vivono nella stessa storia Git, che può
diventare rumorosa (commit di un progetto mischiati a quelli di un altro),
e non è ovviamente riutilizzabile da un altro repository esterno se in
futuro si volesse condividere solo la base con altri.

## In sintesi

| Opzione | Isolamento progetti | Sforzo per propagare un fix | Complessità aggiuntiva |
|---|---|---|---|
| A — branch Git | Basso (stesso repo) | Manuale, per ogni branch | Nessuna |
| B — componenti + repo base via Component Manager | Alto (repo separati) | Bump di versione | Media (repo + versioning) |
| C — componenti + git submodule | Alto (repo separati) | `submodule update` + commit | Media (submodule) |
| D — monorepo con `components/` condiviso | Nessuno (stesso repo) | Automatico | Bassa |

Per un contesto didattico dove si vuole comunque imparare a strutturare
correttamente un progetto ESP-IDF, l'opzione **B** è quella consigliata
quando si arriverà a creare un secondo progetto reale (bussola o sveglia):
insegna sia l'estrazione in componenti sia l'uso del Component Manager per
dipendenze Git, ed è la strada più vicina alle convenzioni ESP-IDF
"ufficiali".
