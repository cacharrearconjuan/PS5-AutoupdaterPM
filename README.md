# PS5-AutoupdaterPM

Payload para PS5 que descarga la última versión de **[PS5 Payload Manager](https://github.com/itsPLK/ps5-payload-manager)** y la deja instalada en `/data/ps5_autoloader` para que el autoloader la cargue en cada arranque.

## Qué hace

1. Lee el repositorio de payloads [`payloads.json`](https://cacharrearconjuan.github.io/mis-payloads-ps5/payloads.json) y busca la entrada con `"id": "pldmgr"`.
2. Si el `pldmgr.elf` instalado ya tiene el mismo SHA-256 que el `checksum` del JSON, no descarga nada: avisa de que está al día.
3. Si hay versión nueva, la descarga a un fichero temporal, comprueba que es un ELF y que su **SHA-256 coincide** con el `checksum`, y solo entonces sustituye el `pldmgr.elf` anterior. Si algo falla, la versión anterior se queda intacta.
4. Se asegura de que `pldmgr.elf` está en `autoload.txt`:
   - si no existe, lo crea con `!3000` + `pldmgr.elf`;
   - si existe, **respeta lo que haya** (elfldr, etaHEN…) y solo añade `pldmgr.elf` al final si falta.

Las descargas van por HTTPS con verificación de certificado (se embebe el bundle de CAs de curl en el ELF). Si la consola tiene la fecha muy desajustada, la verificación falla y el payload lo avisa.

> El autoloader da prioridad a la carpeta `ps5_autoloader` de un USB. Si usas una en USB, la de `/data` se ignora.

## Uso

Envía `PS5-AutoupdaterPM.elf` a la PS5 con tu método habitual (elfldr en el puerto 9021, Payload Manager, etc.). Verás notificaciones en pantalla con el resultado.

Descarga: [Releases](https://github.com/cacharrearconjuan/PS5-AutoupdaterPM/releases/latest).

## Compilación automática (GitHub Actions)

Cada push a `main` que toque el código (`*.c`, `*.h`, `Makefile`, `scripts/`, `tools/` o el workflow) compila el ELF y publica una **release nueva** subiendo la versión menor:

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
- [John Törnblom](https://github.com/john-tornblom) y colaboradores del [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).
