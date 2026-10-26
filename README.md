# PS5-AutoupdaterPM

Payload para PS5 que descarga la última versión de **[PS5 Payload Manager](https://github.com/itsPLK/ps5-payload-manager)** y la deja instalada en `/data/ps5_autoloader`, para que el autoloader la cargue en el siguiente arranque.

## Qué hace

Cada vez que se ejecuta:

0. **DNS local primero.** Si no responde ningún DNS en `127.0.0.1:53` y hay un **pepedns** o un **nanoDNS** en `/data/pldmgr` (o sus subcarpetas) o en `/data/ps5_autoloader`, lo lanza por elfldr y espera hasta 20 segundos a que responda. Con el DNS de la consola en `127.0.0.1`, sin él no hay internet. Prefiere pepedns; si hay varias versiones, la más reciente.
1. Crea `/data/ps5_autoloader` si no existe.
2. Lee el repositorio de payloads [`payloads.json`](https://cacharrearconjuan.github.io/mis-payloads-ps5/payloads.json) y busca la entrada `pldmgr`.
3. Si el `pldmgr.elf` instalado ya tiene el mismo SHA-256 que el `checksum` del JSON, no descarga nada: avisa de que está al día.
4. Si hay versión nueva, la descarga a un fichero temporal, comprueba que es un ELF y que su **SHA-256 coincide**, y solo entonces la renombra a `pldmgr.elf`. Si algo falla, la versión anterior se queda intacta.
5. `autoload.txt`: si no existe, lo crea con `!3000` y `pldmgr.elf`; si existe, **respeta lo que haya** y solo añade `pldmgr.elf` al final si falta.
6. Pone al día su propia copia con la versión del repositorio (entrada `ps5-autoupdaterpm`):
   - **si lo instalaste desde Payload Manager**, actualiza esa copia (`/data/pldmgr/payloads/repo/ps5-autoupdaterpm@…/`) y su `meta.json`, para que Payload Manager enseñe la versión nueva, y borra la copia suelta `/data/pldmgr/PS5-AutoupdaterPM.elf` si la hay;
   - si no, usa `/data/pldmgr/PS5-AutoupdaterPM.elf`.

**No arranca solo.** Si una versión anterior se había añadido a `autoload.txt`, quita esa línea y borra su copia de `/data/ps5_autoloader`.

Si la descarga falla, la notificación dice por qué: falta de DNS, sin conexión, certificado (fecha y hora de la consola) o código HTTP.

> El autoloader da prioridad a la carpeta `ps5_autoloader` de un USB. Si usas una en USB, la de `/data` se ignora.

## Uso

Ejecuta `PS5-AutoupdaterPM.elf` desde Payload Manager (o mándalo a elfldr en el puerto 9021) cada vez que quieras actualizar. La versión nueva de Payload Manager se carga en el siguiente arranque.

Descarga: [Releases](https://github.com/cacharrearconjuan/PS5-AutoupdaterPM/releases/latest).

## Compilación automática (GitHub Actions)

Cada push a `main` que toque el código (`*.c`, `*.h`, `Makefile`, `scripts/`, `tools/` o el workflow) compila el ELF y publica una **release nueva** subiendo la versión menor:

`v1.0.0` → `v1.0.1` → `v1.0.2` …

También se puede lanzar a mano desde **Actions → Compilar y publicar release → Run workflow**, eligiendo qué subir:

| Opción | Ejemplo desde v1.2.0 |
|---|---|
| `minor` (por defecto) | v1.2.1 |
| `patch` | v1.3.0 |
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
- [John Törnblom](https://github.com/john-tornblom) y colaboradores del [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).
