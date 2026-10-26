# PS5-AutoupdaterPM

Payload para PS5 que descarga la última versión de **[PS5 Payload Manager](https://github.com/itsPLK/ps5-payload-manager)** y la deja instalada en `/data/ps5_autoloader`, para que el autoloader la cargue en el siguiente arranque. Además instala en la pantalla de inicio la app **«Actualizar Payload Manager»**, que repite la actualización con un botón.

## Qué hace

Cada vez que se ejecuta, ya sea desde Payload Manager o desde el icono:

0. **DNS local primero.** Si en `/data/pldmgr` (o sus subcarpetas, donde Payload Manager guarda lo que instala del repositorio) hay un **pepedns** o un **nanoDNS** y no hay ningún DNS respondiendo en `127.0.0.1:53`, lo lanza por elfldr y espera hasta 20 segundos a que responda. Con el DNS de la consola en `127.0.0.1`, sin él no hay internet. Si hay los dos, prefiere pepedns; si hay varias versiones, la más reciente.
1. Crea `/data/ps5_autoloader` si no existe.
2. Lee el repositorio de payloads [`payloads.json`](https://cacharrearconjuan.github.io/mis-payloads-ps5/payloads.json) y busca la entrada con `"id": "pldmgr"`.
3. Si el `pldmgr.elf` instalado ya tiene el mismo SHA-256 que el `checksum` del JSON, no descarga nada: avisa de que está al día.
4. Si hay versión nueva, la descarga a un fichero temporal, comprueba que es un ELF y que su **SHA-256 coincide** con el `checksum`, y solo entonces la renombra a `pldmgr.elf`. Si algo falla, la versión anterior se queda intacta.
5. Se asegura de que `pldmgr.elf` está en `autoload.txt`:
   - si no existe, lo crea con `!3000` y `pldmgr.elf`;
   - si existe, **respeta lo que haya** (elfldr, kstuff…) y solo añade `pldmgr.elf` al final si falta.
6. Pone al día su propia copia, la que lanza el icono, con la versión del repositorio (entrada `ps5-autoupdaterpm`). Si falta o su SHA-256 es distinto, la descarga, la verifica y la sustituye:
   - **Si lo instalaste desde Payload Manager**, actualiza esa copia (`/data/pldmgr/payloads/repo/ps5-autoupdaterpm@…/`) y su `meta.json` (versión, checksum, URLs y fecha), para que Payload Manager enseñe la versión nueva. Si queda una copia suelta en `/data/pldmgr/PS5-AutoupdaterPM.elf` (la creaba la v2.0.0), la borra para que no haya duplicados.
   - Si no, usa `/data/pldmgr/PS5-AutoupdaterPM.elf`.
7. Instala la app del icono, o la actualiza si ha cambiado.

**No arranca solo.** Si una versión anterior se había añadido a `autoload.txt`, quita esa línea y borra su copia de `/data/ps5_autoloader`. También quita el icono antiguo `AUPM00001`, el que abría el navegador.

## El icono «Actualizar Payload Manager»

Es una app de verdad (`AUPM00002`), instalada con el mismo método que el ejemplo [`install_app`](https://github.com/ps5-payload-dev/sdk/tree/master/samples/install_app) del PS5 Payload SDK: el `eboot.bin` va en `/system_ex/app/AUPM00002` y el `param.json` y el icono en `/user/app/AUPM00002/sce_sys`. No hace falta instalar ninguna PKG: lo hace el payload la primera vez.

Una app no tiene permisos para escribir en `/data`, así que el icono no actualiza nada por sí mismo:

1. Al abrirlo, el `eboot.bin` manda a elfldr (`127.0.0.1:9021`) un pequeño lanzador que lleva dentro y se cierra.
2. elfldr ejecuta el lanzador con permisos completos.
3. El lanzador manda a elfldr la copia de Payload Manager (o, si no hay, `/data/pldmgr/PS5-AutoupdaterPM.elf`), que hace todos los pasos de arriba y muestra el resultado en notificaciones.

Requisitos para que el icono funcione:

- **elfldr escuchando en el puerto 9021.** Si usas Payload Manager, ya lo tienes.
- **Soporte de fself cargado (kstuff o etaHEN)** en ese arranque: el `eboot.bin` está firmado en falso, como el de cualquier homebrew.
- Si no encuentra ninguna copia, el lanzador lo avisa: ejecuta el payload una vez desde Payload Manager.

> Si borras el icono desde la pantalla de inicio, vuelve a instalarse la próxima vez que ejecutes el payload desde Payload Manager.

Las descargas van por HTTPS con verificación de certificado (se embebe el bundle de CAs de curl en el ELF). Si la consola tiene la fecha muy desajustada, la verificación falla y el payload lo avisa.

> El autoloader da prioridad a la carpeta `ps5_autoloader` de un USB. Si usas una en USB, la de `/data` se ignora.

## Uso

Ejecuta `PS5-AutoupdaterPM.elf` **una vez** desde Payload Manager (o mándalo a elfldr en el puerto 9021). Verás notificaciones con el resultado y aparecerá el icono «Actualizar Payload Manager» en la pantalla de inicio. A partir de ahí, cuando quieras actualizar, pulsa el icono. La versión nueva de Payload Manager se carga en el siguiente arranque.

Descarga: [Releases](https://github.com/cacharrearconjuan/PS5-AutoupdaterPM/releases/latest).

## Estructura

| Ruta | Qué es |
|---|---|
| `main.c` | El actualizador (`PS5-AutoupdaterPM.elf`) |
| `app/eboot.c` | `eboot.bin` de la app del icono: manda el lanzador a elfldr |
| `app/lanzador.c` | Lanzador: manda a elfldr la copia de PS5-AutoupdaterPM |
| `app/pm_slots.h` | Localiza la copia que gestiona Payload Manager (lo usan el actualizador y el lanzador) |
| `app/sce_sys/` | `param.json`, `param.json.system` e `icon0.png` de la app |

El `Makefile` compila el lanzador, lo mete dentro del `eboot`, firma el `eboot` con `make_fself.py` del SDK y mete todo dentro del actualizador. Solo se publica un fichero: `PS5-AutoupdaterPM.elf`.

## Compilación automática (GitHub Actions)

Cada push a `main` que toque el código (`*.c`, `*.h`, `Makefile`, `app/`, `scripts/`, `tools/` o el workflow) compila el ELF y publica una **release nueva** subiendo la versión menor:

`v1.0.0` → `v1.1.0` → `v1.2.0` …

También se puede lanzar a mano desde **Actions → Compilar y publicar release → Run workflow**, eligiendo qué subir:

| Opción | Ejemplo desde v1.2.0 |
|---|---|
| `minor` (por defecto) | v1.3.0 |
| `patch` | v1.2.1 |
| `major` | v2.0.0 |

La versión se graba dentro del ELF (sale en la notificación) y el SHA-256 va en las notas de la release. El SDK, mbedTLS y curl se guardan en caché, así que solo la primera ejecución tarda unos minutos.

## Compilación local

Requisitos: `clang-18`, `lld-18`, `make`, `curl`, `python3`, `unzip`.

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5-payload-sdk
./scripts/build_deps.sh          # SDK + mbedTLS + curl para PS5 (solo la primera vez)
make VERSION=1.0.0
```

## Créditos

- [itsPLK](https://github.com/itsPLK) por PS5 Payload Manager.
- [John Törnblom](https://github.com/john-tornblom) y colaboradores del [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk). El `eboot` y la instalación de la app se basan en su ejemplo `install_app` (GPLv3).
