# Futuras actualizaciones e ideas / Future updates & ideas

> **ES:** Aparcamiento de cosas que evaluamos y deliberadamente **no** enviamos
> (todavía): qué se quería, por qué se rechazó o aparcó, y qué haría falta.
> Así una futura revisión parte de evidencia y no de cero.
>
> **EN:** Parking lot for things we evaluated and deliberately did **not** ship
> (yet): what was wanted, why it was rejected or parked, and what it would
> take — so a future revisit starts from evidence instead of from zero.

---

# ESPAÑOL

## [APARCADO] Motor de búsqueda EXT.TO (`ext.to`)

### Qué queríamos
Añadir EXT Torrents como otro motor declarativo en
`SearchEngine::builtinEngines()` (`src/SearchEngine.cpp`), exactamente igual
que los existentes: un bloque `Definition` (URL de búsqueda + regex de filas),
sin código de red nuevo y sin cambios de UI. URL mapeada:
`https://ext.to/browse/?q=%s&sort=age&order=desc&age=4&user_sort=1&with_adult=1`,
filas `table.table-striped > tbody > tr`, columnas
`TORRENT NAME | SIZE | FILES | AGE | SEED | LEECH` (referencia: Prowlarr
`exttorrents.yml`, revisión ene-2026).

### Por qué NO se aplicó (spike de viabilidad, 2026-10-07 → ROJO)
1. **Cloudflare bloquea el HTTP plano.** Pedido con el stack exacto de
   FTorrent (GET HTTPS plano, UA `FTORRENT 0.5.0`, sin JS), tanto
   `ext.to/browse/…` como el espejo `extto.com` responden **HTTP 403**
   sirviendo la página `Just a moment...` de Cloudflare: **0 enlaces
   `magnet:`, 0 `data-hash`** en el cuerpo. `fetchWithFallback()` acabaría
   siempre en "no usable results".
2. **Ni siquiera hay magnets planos tras el challenge.** Según Jackett
   (#16367, dic-2025), ni el listado ni las páginas de detalle llevan magnets;
   vivían en botones JS `a.search-magnet-btn[data-hash]`, al parecer
   eliminados desde entonces (reporte ene-2026). Nada que `parse()` pueda
   cosechar.
3. **Hueco en el parser para esa forma.** La conversión hash→magnet
   (`isInfoHash`, `src/SearchEngine.cpp`) solo existe en la rama `json`; la
   rama `html` conservaría un enlace malformado. Pequeño por sí solo, pero
   inútil sin (1) y (2).

### Qué necesitaríamos para aplicarlo
- Una vía de resolución del challenge (navegador headless o servicio tipo
  FlareSolverr) dentro de `HttpClient`, **o** que el sitio abandone Cloudflare
  / publique una API como Apibay.
- Reutilizar la conversión `isInfoHash` en la rama `html` (pocas líneas, útil
  también para futuros motores).
- Si aparece un endpoint de magnets, el flujo estilo Pirate Face
  (`detailIdSelector`/`detailUrlTemplate`) ya soporta resolución de magnets
  por fila sin cambios de UI.
- Repetir el spike: HTTP 200 + `magnet:`/`data-hash` presentes con el UA de
  FTorrent, y luego búsqueda real en la app (`Source = EXT.TO`, magnets
  válidos, file-list del swarm resolviendo).

### Qué comprometería
- **Portabilidad:** un motor JS/navegador es una dependencia pesada nueva
  frente al `HttpClient` actual (curl plano). Pone en riesgo las plataformas
  que servimos sin dependencias extra (Windows x86/ARM64, DEB/RPM 32/64-bit,
  Arch, sandbox Flatpak — sin navegador garantizado dentro).
- **Privacidad/fiabilidad/empaquetado:** un servicio resolvedor externo es
  otro salto de red con las consultas del usuario, otro modo de fallo (cfr.
  los 522 de `search.extto.com`) y otra cosa que empaquetar.
- **Mantenimiento:** el scraping tras JS se rompe con cada rediseño
  (precedente: TorrentGalaxy se dio de baja del catálogo por motivos de la
  misma clase, ver comentario en `builtinEngines()`).

### Revisitar cuando
`curl -A "FTORRENT x.y.z" "https://ext.to/browse/?q=ubuntu…"` devuelva HTTP 200
con magnets/hashes en el cuerpo, o el sitio ofrezca una API.

---

## [ACEPTADO / PENDIENTE] Motor de búsqueda Uindex.org

### Qué queremos
Añadir Uindex.org como motor declarativo en `builtinEngines()`, con el mismo
patrón modular que el resto. Anatomía verificada en 3 fuentes independientes
(plugin qBittorrent `uindex.py`, FlexGet, HTML en vivo):
`https://uindex.org/search.php?search=%s&c=%c&page=%p`, categorías
`All=0, Movies=1, TV=2, Games=3, Music=4, Apps=5, XXX=6, Anime=7, Other=8`,
fila `<tr>` con título en `<a href="details.php…">`, magnet en
`<a href="magnet:…">`, tamaño en `<td style="…white-space:nowrap">`, seeds en
`<span class="g">`, leech en `<span class="b">`. `maxPages = 3` (decidido),
sin `proxyTemplate` inicial: `uindex.net` solo entrará si se verifica que sirve
el mismo markup.

### Qué falta para aplicarlo
Escribir el bloque `Definition` (~20 líneas) tras el bloque BTDig en
`builtinEngines()`, actualizar 2 líneas cosméticas (`README.md`, `appadata`
`io.github.thedevil4k.FTorrent.appdata.xml`), compilar en todas las
plataformas, pasar `ctest`, y verificar búsqueda real (`f1 the movie`,
`ubuntu`) con `Source = Uindex`, magnets válidos y S/L numéricos. Si un
selector muere ante un rediseño, el motor cae al harvest genérico
(`parse()`, `SearchEngine.cpp`), igual que Nyaa.si/BTDig.

### Qué comprometería
Nada estructural: sin includes nuevos, sin hilos, sin cambios de firmas; solo
`std::regex` ECMAScript ya usado. Riesgo residual conocido: posible 403 a
clientes no-navegador (un fetch de prueba recibió 403) y lentitud intermitente
del sitio — mitigado con `searchStreaming` cancelable y reintento por espejo.

---

## Plantilla para la próxima idea

### [ESTADO] Título
### Qué queremos
### Por qué aún no / evidencia
### Qué necesitaríamos
### Qué comprometería
### Revisitar cuando

---

# ENGLISH

## [PARKED] EXT.TO (`ext.to`) search engine

### What we wanted
Add EXT Torrents as another declarative engine in
`SearchEngine::builtinEngines()` (`src/SearchEngine.cpp`), exactly like the
existing ones: one `Definition` block (search URL + row regexes), no new
network code, no UI changes. Mapped URL:
`https://ext.to/browse/?q=%s&sort=age&order=desc&age=4&user_sort=1&with_adult=1`,
rows `table.table-striped > tbody > tr`, columns
`TORRENT NAME | SIZE | FILES | AGE | SEED | LEECH` (reference: Prowlarr
`exttorrents.yml`, Jan-2026 revision).

### Why it was NOT applied (viability spike, 2026-10-07 → RED)
1. **Cloudflare blocks plain HTTP.** Fetched with FTorrent's exact stack
   (plain HTTPS GET, UA `FTORRENT 0.5.0`, no JS), both `ext.to/browse/…` and
   the `extto.com` mirror answer **HTTP 403** serving Cloudflare's
   `Just a moment...` page: **0 `magnet:` links, 0 `data-hash`** in the body.
   `fetchWithFallback()` would always end in "no usable results".
2. **No plain magnets even past the challenge.** Per Jackett (#16367,
   Dec-2025) neither the listing nor the detail pages carry magnet links;
   they lived in JS `a.search-magnet-btn[data-hash]` buttons, reportedly
   removed since (Jan-2026 report). Nothing for `parse()` to harvest.
3. **Parser gap for that shape.** The bare-hash → magnet conversion
   (`isInfoHash`, `src/SearchEngine.cpp`) only exists in the `json` branch;
   the `html` branch would keep a malformed link. Small on its own, but
   pointless without (1) and (2).

### What we would need to apply it
- A challenge-solving fetch path (headless browser or a FlareSolverr-like
  sidecar service) inside `HttpClient`, **or** the site dropping Cloudflare /
  publishing an Apibay-like API.
- Reuse the `isInfoHash` conversion in the `html` branch (a few lines, helps
  future engines too).
- If a magnet endpoint surfaces, the Pirate-Face-style
  `detailIdSelector`/`detailUrlTemplate` flow already supports per-row magnet
  resolution with zero UI changes.
- Re-run the spike: HTTP 200 + `magnet:`/`data-hash` present with the FTorrent
  UA, then a real in-app search (`Source = EXT.TO`, valid magnets, swarm
  file-list resolving).

### What it would compromise
- **Portability:** a browser/JS engine is a heavy new dependency vs today's
  plain-curl `HttpClient`. It endangers the platforms we ship with zero extra
  deps (Windows x86/ARM64, 32/64-bit DEB/RPM, Arch, Flatpak sandbox — no
  browser guaranteed inside).
- **Privacy/reliability/packaging:** an external solver service is another
  network hop holding user queries, another failure mode (cf. the 522s on
  `search.extto.com`), and another thing packagers must ship.
- **Maintenance:** JS-gated scraping breaks on every site redesign
  (precedent: TorrentGalaxy was dropped from the catalogue for the same class
  of reason, see comment in `builtinEngines()`).

### Revisit when
`curl -A "FTORRENT x.y.z" "https://ext.to/browse/?q=ubuntu…"` returns HTTP 200
with magnets/hashes in the body, or the site offers an API.

---

## [ACCEPTED / PENDING] Uindex.org search engine

### What we want
Add Uindex.org as a declarative engine in `builtinEngines()`, following the
same modular pattern as the rest. Anatomy verified against 3 independent
sources (qBittorrent `uindex.py` plugin, FlexGet, live HTML):
`https://uindex.org/search.php?search=%s&c=%c&page=%p`, categories
`All=0, Movies=1, TV=2, Games=3, Music=4, Apps=5, XXX=6, Anime=7, Other=8`,
`<tr>` rows with the title in `<a href="details.php…">`, the magnet in
`<a href="magnet:…">`, size in `<td style="…white-space:nowrap">`, seeds in
`<span class="g">`, leech in `<span class="b">`. `maxPages = 3` (decided), no
initial `proxyTemplate`: `uindex.net` only joins if verified to serve the same
markup.

### What is missing to apply it
Write the ~20-line `Definition` block after the BTDig block in
`builtinEngines()`, update 2 cosmetic lines (`README.md`, the
`io.github.thedevil4k.FTorrent.appdata.xml` metainfo), build on every
platform, run `ctest`, and verify a real search (`f1 the movie`, `ubuntu`)
with `Source = Uindex`, valid magnets and numeric S/L. If a selector dies on a
redesign, the engine falls back to the generic harvest (`parse()`,
`SearchEngine.cpp`), like Nyaa.si/BTDig.

### What it would compromise
Nothing structural: no new includes, no threads, no signature changes; only
the already-used ECMAScript `std::regex`. Known residual risks: possible 403
to non-browser clients (one probe fetch got 403) and intermittent site
slowness — mitigated by cancellable `searchStreaming` and mirror retry.

---

## Template for the next idea

### [STATUS] Title
### What we want
### Why not yet / evidence
### What we'd need
### What it would compromise
### Revisit when
