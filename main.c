/*
 * PS5-AutoupdaterPM
 *
 * Cada vez que se ejecuta (desde Payload Manager o desde el icono):
 *
 * 1. Crea /data/ps5_autoloader si no existe, descarga la última versión de
 *    PS5 Payload Manager indicada en el repositorio de payloads, verifica su
 *    SHA-256 y la deja ahí como pldmgr.elf. Crea autoload.txt si no existe o
 *    añade pldmgr.elf si falta, sin tocar el resto. En el siguiente arranque el
 *    autoloader carga la versión nueva.
 * 2. Se asegura de que /data/pldmgr/PS5-AutoupdaterPM.elf existe y es la
 *    versión del repositorio (mismo SHA-256); si falta o es distinta, la
 *    descarga y la sustituye. Es la copia que lanza el icono.
 * 3. Instala (o actualiza) la app «Actualizar Payload Manager» en la pantalla
 *    de inicio. Al abrirla, manda un pequeño lanzador a elfldr (puerto 9021)
 *    que ejecuta /data/pldmgr/PS5-AutoupdaterPM.elf.
 *
 * Antes de nada, si en /data/pldmgr hay un pepedns o un nanoDNS y no hay ningún
 * DNS respondiendo en 127.0.0.1:53, lo lanza y espera a que responda: con el
 * DNS de la consola en 127.0.0.1, sin él no hay internet.
 *
 * No arranca solo: si una versión anterior se había añadido a autoload.txt,
 * quita esa línea y borra su copia de /data/ps5_autoloader. También quita el
 * icono antiguo AUPM00001 (el que abría el navegador).
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/_iovec.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <curl/curl.h>
#include <mbedtls/sha256.h>

/* Generados por el Makefile con tools/bin2h.py */
#include "cacert_pem.h"
#include "eboot_bin.h"
#include "app_param_json.h"
#include "app_param_system_json.h"
#include "icon0_png.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define APP_NAME      "PS5-AutoupdaterPM"
#ifndef REPO_JSON_URL
#define REPO_JSON_URL "https://cacharrearconjuan.github.io/mis-payloads-ps5/payloads.json"
#endif

#ifndef AUTOLOAD_DIR
#define AUTOLOAD_DIR  "/data/ps5_autoloader"
#endif
#define AUTOLOAD_TXT  AUTOLOAD_DIR "/autoload.txt"

#define PLDMGR_ID     "pldmgr"
#define PLDMGR_FILE   "pldmgr.elf"
#define SELF_ID       "ps5-autoupdaterpm"
#define SELF_FILE     "PS5-AutoupdaterPM.elf"

/* Copia del actualizador que lanza el icono (la misma ruta va en el lanzador) */
#ifndef UPDATER_PATH
#define UPDATER_PATH  "/data/pldmgr/PS5-AutoupdaterPM.elf"
#endif
#define UPDATER_DIR   "/data/pldmgr"

/* App del icono (debe coincidir con app/sce_sys/param.json) */
#define TITLE_ID      "AUPM00002"
#define USER_APP_DIR  "/user/app/" TITLE_ID
#define SYS_APP_DIR   "/system_ex/app/" TITLE_ID
/* Icono antiguo: un enlace que abría http://127.0.0.1:8086/ en el navegador */
#define OLD_TITLE_ID  "AUPM00001"

#define MAX_JSON_SIZE (4L * 1024 * 1024)   /* 4 MiB  */
#define MAX_ELF_SIZE  (64L * 1024 * 1024)  /* 64 MiB */
#define FIELD_SIZE    1024

/* ------------------------------------------------------------------------ */
/* Notificaciones                                                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *req,
                                     size_t size, int blocking);

static void notify(const char *fmt, ...) {
    notify_request_t req;
    va_list args;

    memset(&req, 0, sizeof(req));
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);

    printf("[" APP_NAME "] %s\n", req.message);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* ------------------------------------------------------------------------ */
/* Descargas (HTTPS verificado con el bundle de CAs embebido)                */
/* ------------------------------------------------------------------------ */

typedef struct {
    char  *data;
    size_t len;
    size_t limit;
} mem_buf_t;

typedef struct {
    FILE  *fp;
    size_t len;
    size_t limit;
} file_sink_t;

static size_t write_mem_cb(char *ptr, size_t size, size_t nmemb, void *ud) {
    mem_buf_t *buf = ud;
    size_t n = size * nmemb;

    if (buf->len + n > buf->limit) return 0; /* aborta: demasiado grande */

    char *p = realloc(buf->data, buf->len + n + 1);
    if (!p) return 0;
    memcpy(p + buf->len, ptr, n);
    buf->len += n;
    p[buf->len] = '\0';
    buf->data = p;
    return n;
}

static size_t write_file_cb(char *ptr, size_t size, size_t nmemb, void *ud) {
    file_sink_t *sink = ud;
    size_t n = size * nmemb;

    if (sink->len + n > sink->limit) return 0;
    if (fwrite(ptr, 1, n, sink->fp) != n) return 0;
    sink->len += n;
    return n;
}

static int g_cert_error;

static int http_get(const char *url, curl_write_callback cb, void *ud, long timeout_s) {
    CURL *curl = curl_easy_init();
    CURLcode res;
    long status = 0;

    if (!curl) return -1;

    struct curl_blob ca = {
        .data  = (void *)cacert_pem,
        .len   = cacert_pem_len,
        .flags = CURL_BLOB_NOCOPY,
    };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, APP_NAME "/" APP_VERSION);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, ud);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    res = curl_easy_perform(curl);
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        printf("[" APP_NAME "] curl: %s (%s)\n", curl_easy_strerror(res), url);
        if (res == CURLE_PEER_FAILED_VERIFICATION || res == CURLE_SSL_CACERT_BADFILE)
            g_cert_error = 1;
        return -1;
    }
    if (status != 200) {
        printf("[" APP_NAME "] HTTP %ld (%s)\n", status, url);
        return -1;
    }
    return 0;
}

static char *download_to_string(const char *url) {
    mem_buf_t buf = { NULL, 0, MAX_JSON_SIZE };

    if (http_get(url, write_mem_cb, &buf, 30L) != 0 || !buf.data) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

static int download_to_file(const char *url, const char *path) {
    file_sink_t sink = { NULL, 0, MAX_ELF_SIZE };
    int rc;

    sink.fp = fopen(path, "wb");
    if (!sink.fp) return -1;

    rc = http_get(url, write_file_cb, &sink, 300L);
    if (fclose(sink.fp) != 0) rc = -1;
    if (rc != 0 || sink.len == 0) {
        unlink(path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Mini-lector JSON: localiza el objeto con "id": "<id>" y lee sus campos    */
/* ------------------------------------------------------------------------ */

static const char *skip_ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* p apunta a la comilla inicial. Copia (truncando) en out y devuelve el
 * puntero tras la comilla final, o NULL si la cadena está mal formada. */
static const char *read_string(const char *p, const char *end, char *out, size_t outsz) {
    size_t n = 0;

    if (p >= end || *p != '"') return NULL;
    p++;
    while (p < end && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            if (p >= end) return NULL;
            c = *p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': /* no lo necesitamos para URLs ni hashes */
                if (end - p < 4) return NULL;
                p += 4;
                c = '?';
                break;
            default: break; /* \" \\ \/ */
            }
        }
        if (out && n + 1 < outsz) out[n++] = c;
    }
    if (p >= end) return NULL;
    if (out && outsz) out[n] = '\0';
    return p + 1;
}

/* Devuelve el final ('}' incluido) del objeto que empieza en obj, o NULL. */
static const char *object_end(const char *obj, const char *end) {
    int depth = 0;
    const char *p = obj;

    while (p < end) {
        if (*p == '"') {
            p = read_string(p, end, NULL, 0);
            if (!p) return NULL;
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') {
            if (--depth == 0) return p + 1;
        }
        p++;
    }
    return NULL;
}

static int find_object_by_id(const char *json, size_t len, const char *id,
                             const char **obj_start, const char **obj_end) {
    const char *end = json + len;
    const char *stack[64];
    int depth = 0;
    const char *p = json;
    char key[64], val[256];

    while (p < end) {
        if (*p == '{') {
            if (depth < 64) stack[depth] = p;
            depth++;
            p++;
        } else if (*p == '}') {
            if (depth > 0) depth--;
            p++;
        } else if (*p == '"') {
            const char *q = read_string(p, end, key, sizeof(key));
            if (!q) return -1;
            q = skip_ws(q, end);
            if (q < end && *q == ':' && strcmp(key, "id") == 0 && depth > 0 && depth <= 64) {
                const char *v = skip_ws(q + 1, end);
                if (v < end && *v == '"') {
                    const char *after = read_string(v, end, val, sizeof(val));
                    if (!after) return -1;
                    if (strcmp(val, id) == 0) {
                        *obj_start = stack[depth - 1];
                        *obj_end = object_end(*obj_start, end);
                        return *obj_end ? 0 : -1;
                    }
                    p = after;
                    continue;
                }
            }
            p = q;
        } else {
            p++;
        }
    }
    return -1;
}

/* Lee un campo de texto del primer nivel del objeto [obj, end). */
static int get_string_field(const char *obj, const char *end, const char *field,
                            char *out, size_t outsz) {
    const char *p = obj;
    int depth = 0;
    char key[64];

    while (p < end) {
        if (*p == '"') {
            const char *q = read_string(p, end, key, sizeof(key));
            if (!q) return -1;
            q = skip_ws(q, end);
            if (depth == 1 && q < end && *q == ':' && strcmp(key, field) == 0) {
                const char *v = skip_ws(q + 1, end);
                if (v < end && *v == '"')
                    return read_string(v, end, out, outsz) ? 0 : -1;
                return -1;
            }
            p = q;
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') depth--;
        p++;
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/* Ficheros: SHA-256 y comprobaciones                                        */
/* ------------------------------------------------------------------------ */

static int sha256_file_hex(const char *path, char hex[65]) {
    static unsigned char buf[64 * 1024];
    unsigned char digest[32];
    mbedtls_sha256_context ctx;
    size_t n;
    FILE *fp = fopen(path, "rb");

    if (!fp) return -1;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        mbedtls_sha256_update(&ctx, buf, n);
    int err = ferror(fp);
    fclose(fp);
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    if (err) return -1;

    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return 0;
}

static int is_sha256_hex(const char *s) {
    if (strlen(s) != 64) return 0;
    for (const char *p = s; *p; p++) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')))
            return 0;
    }
    return 1;
}

static int is_elf(const char *path) {
    unsigned char magic[4] = {0};
    FILE *fp = fopen(path, "rb");

    if (!fp) return 0;
    size_t n = fread(magic, 1, sizeof(magic), fp);
    fclose(fp);
    return n == 4 && magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    return (mkdir(tmp, 0777) != 0 && errno != EEXIST) ? -1 : 0;
}

/* Escribe data en path solo si el contenido es distinto.
 * Devuelve 1 si lo ha escrito, 0 si ya era igual, -1 si hay error. */
static int write_if_different(const char *path, const unsigned char *data, size_t size) {
    struct stat st;

    if (stat(path, &st) == 0 && (size_t)st.st_size == size) {
        FILE *fp = fopen(path, "rb");
        if (fp) {
            unsigned char *cur = malloc(size);
            int same = cur && fread(cur, 1, size, fp) == size && memcmp(cur, data, size) == 0;
            free(cur);
            fclose(fp);
            if (same) return 0;
        }
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    int ok = fwrite(data, 1, size, fp) == size;
    if (fclose(fp) != 0) ok = 0;
    return ok ? 1 : -1;
}

/* ------------------------------------------------------------------------ */
/* Instalar un payload del repositorio en una ruta concreta                  */
/* ------------------------------------------------------------------------ */

enum { UPD_ERROR = -1, UPD_CURRENT = 0, UPD_INSTALLED = 1 };

/* Deja en dest el fichero de la entrada "id" de payloads.json.
 * Si dest ya tiene el mismo SHA-256 que el repositorio, no descarga nada.
 * Si no, descarga a dest.tmp, comprueba que es un ELF y su SHA-256, y solo
 * entonces sustituye dest. Si algo falla, el fichero anterior sigue intacto. */
static int install_from_repo(const char *json, const char *id, const char *dest,
                             const char *label, char *version_out, size_t version_outsz) {
    char url[FIELD_SIZE] = "", version[64] = "", checksum[128] = "", have[65] = "";
    char tmp[512];
    const char *obj = NULL, *obj_end = NULL;

    snprintf(tmp, sizeof(tmp), "%s.tmp", dest);
    if (version_out && version_outsz) version_out[0] = '\0';

    if (find_object_by_id(json, strlen(json), id, &obj, &obj_end) != 0) {
        notify("Error: \"%s\" no aparece en payloads.json", id);
        return UPD_ERROR;
    }

    if (get_string_field(obj, obj_end, "url", url, sizeof(url)) != 0 || strncmp(url, "https://", 8) != 0) {
        if (get_string_field(obj, obj_end, "source_direct", url, sizeof(url)) != 0 ||
            strncmp(url, "https://", 8) != 0) {
            notify("Error: URL de %s no válida en payloads.json", id);
            return UPD_ERROR;
        }
    }
    get_string_field(obj, obj_end, "version", version, sizeof(version));
    if (!version[0]) strcpy(version, "?");
    if (version_out && version_outsz) snprintf(version_out, version_outsz, "%s", version);
    get_string_field(obj, obj_end, "checksum", checksum, sizeof(checksum));
    int has_checksum = is_sha256_hex(checksum);

    /* ¿Ya está esa misma versión? */
    if (has_checksum && file_exists(dest) && sha256_file_hex(dest, have) == 0 &&
        strcasecmp(have, checksum) == 0)
        return UPD_CURRENT;

    notify("Descargando %s %s...", label, version);
    if (download_to_file(url, tmp) != 0) {
        notify(g_cert_error ? "Error al descargar %s %s: certificado HTTPS. ¿Está bien la fecha y hora de la consola?"
                            : "Error al descargar %s %s", label, version);
        return UPD_ERROR;
    }

    if (!is_elf(tmp)) {
        unlink(tmp);
        notify("Error: lo descargado de %s no es un ELF", label);
        return UPD_ERROR;
    }

    if (has_checksum) {
        if (sha256_file_hex(tmp, have) != 0 || strcasecmp(have, checksum) != 0) {
            unlink(tmp);
            notify("Error: el SHA-256 de %s no coincide. Se mantiene la versión anterior.", label);
            return UPD_ERROR;
        }
    } else {
        printf("[" APP_NAME "] Aviso: %s sin checksum en payloads.json; solo se comprueba que sea ELF\n", id);
    }

    /* Sustitución atómica: si algo falla antes, el fichero anterior sigue intacto */
    if (rename(tmp, dest) != 0) {
        unlink(tmp);
        notify("Error al guardar %s", dest);
        return UPD_ERROR;
    }
    return UPD_INSTALLED;
}

/* ------------------------------------------------------------------------ */
/* autoload.txt: añade entradas sin tocar el resto                           */
/* ------------------------------------------------------------------------ */

static int autoload_has_entry(const char *content, const char *entry) {
    const char *p = content;
    size_t elen = strlen(entry);

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        const char *s = p;
        while (len && (*s == ' ' || *s == '\t')) { s++; len--; }
        while (len && (s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
        if (len == elen && memcmp(s, entry, elen) == 0) return 1;
        if (!eol) break;
        p = eol + 1;
    }
    return 0;
}

/* entries: lista terminada en NULL. Devuelve 1 si ha cambiado algo, 0 si no, -1 error */
static int ensure_autoload_entries(const char *const *entries) {
    char content[64 * 1024];
    size_t n = 0;
    int changed = 0;
    FILE *fp = fopen(AUTOLOAD_TXT, "rb");

    if (fp) {
        n = fread(content, 1, sizeof(content) - 1, fp);
        int bad = ferror(fp);
        fclose(fp);
        if (bad) return -1; /* nunca truncar autoload.txt por un error de lectura */
    } else if (errno != ENOENT) {
        return -1;
    }
    content[n] = '\0';

    fp = fopen(AUTOLOAD_TXT, n ? "ab" : "wb");
    if (!fp) return -1;
    if (n == 0) {
        fputs("!3000\n", fp);
        changed = 1;
    } else if (content[n - 1] != '\n') {
        fputc('\n', fp);
    }
    for (int i = 0; entries[i]; i++) {
        if (!autoload_has_entry(content, entries[i])) {
            fprintf(fp, "%s\n", entries[i]);
            changed = 1;
        }
    }
    if (fclose(fp) != 0) return -1;
    return changed;
}

/* Quita de autoload.txt las líneas que sean exactamente entry, sin tocar el
 * resto. Devuelve 1 si ha quitado algo, 0 si no estaba, -1 si hay error. */
static int remove_autoload_entry(const char *entry) {
    static char content[64 * 1024], out[64 * 1024];
    size_t n, o = 0, elen = strlen(entry);
    int removed = 0;
    FILE *fp = fopen(AUTOLOAD_TXT, "rb");

    if (!fp) return errno == ENOENT ? 0 : -1;
    n = fread(content, 1, sizeof(content), fp);
    int bad = ferror(fp) || (n == sizeof(content) && fgetc(fp) != EOF);
    fclose(fp);
    if (bad) return -1; /* no se reescribe si no se ha leído entero */

    const char *end = content + n;
    for (const char *p = content; p < end;) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t full = eol ? (size_t)(eol - p) + 1 : (size_t)(end - p);
        size_t len = eol ? (size_t)(eol - p) : full;
        const char *s = p;
        while (len && (*s == ' ' || *s == '\t')) { s++; len--; }
        while (len && (s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
        if (len == elen && memcmp(s, entry, elen) == 0) {
            removed = 1;
        } else {
            memcpy(out + o, p, full);
            o += full;
        }
        p += full;
    }
    if (!removed) return 0;

    fp = fopen(AUTOLOAD_TXT ".tmp", "wb");
    if (!fp) return -1;
    int ok = fwrite(out, 1, o, fp) == o;
    if (fclose(fp) != 0) ok = 0;
    if (!ok || rename(AUTOLOAD_TXT ".tmp", AUTOLOAD_TXT) != 0) {
        unlink(AUTOLOAD_TXT ".tmp");
        return -1;
    }
    return 1;
}

/* Versiones anteriores se copiaban en /data/ps5_autoloader y se añadían a
 * autoload.txt para arrancar solas. Ya no: se lanza desde el icono. */
static void remove_legacy_autostart(void) {
    int r = remove_autoload_entry(SELF_FILE);
    int had_copy = r >= 0 && file_exists(AUTOLOAD_DIR "/" SELF_FILE) &&
                   unlink(AUTOLOAD_DIR "/" SELF_FILE) == 0;

    if (r < 0)
        notify("Aviso: no se pudo quitar " SELF_FILE " de " AUTOLOAD_TXT);
    else if (r > 0 || had_copy)
        notify(APP_NAME " ya no arranca solo: quitado de " AUTOLOAD_DIR);
}

/* ------------------------------------------------------------------------ */
/* App «Actualizar Payload Manager» en la pantalla de inicio                 */
/* Mismo método que samples/install_app del PS5 Payload SDK: el eboot.bin va */
/* en /system_ex/app/<ID>, param.json e icono en /user/app/<ID>/sce_sys.     */
/* ------------------------------------------------------------------------ */

int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallTitleDir(const char *title_id, const char *dir, void *unused);
int sceAppInstUtilAppUnInstall(const char *title_id);

#define IOVEC_SIZE(x)  (sizeof(x) / sizeof(struct iovec))
#define IOVEC_ENTRY(x) { (void *)(x), (x) ? strlen(x) + 1 : 0 }

/* /system_ex está montado en solo lectura (igual que el comando MTRW de ftpsrv) */
static int remount_system_ex(void) {
    struct iovec iov[] = {
        IOVEC_ENTRY("from"),      IOVEC_ENTRY("/dev/ssd0.system_ex"),
        IOVEC_ENTRY("fspath"),    IOVEC_ENTRY("/system_ex"),
        IOVEC_ENTRY("fstype"),    IOVEC_ENTRY("exfatfs"),
        IOVEC_ENTRY("large"),     IOVEC_ENTRY("yes"),
        IOVEC_ENTRY("timezone"),  IOVEC_ENTRY("static"),
        IOVEC_ENTRY("async"),     IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("ignoreacl"), IOVEC_ENTRY(NULL),
    };
    return nmount(iov, IOVEC_SIZE(iov), MNT_UPDATE);
}

#define APP_MARKER SYS_APP_DIR "/.instalado"

static int install_app(void) {
    struct stat st;
    int registered = stat(USER_APP_DIR, &st) == 0 && file_exists(APP_MARKER);
    int e, p, i, s, err;

    if (remount_system_ex() != 0) {
        notify("Error: no se pudo abrir /system_ex para escritura (errno %d). "
               "No se puede instalar el icono.", errno);
        return -1;
    }
    if (mkdir_p(SYS_APP_DIR "/sce_sys") != 0 || mkdir_p(USER_APP_DIR "/sce_sys") != 0) {
        notify("Error: no se pudieron crear las carpetas de la app " TITLE_ID " (errno %d)", errno);
        return -1;
    }

    e = write_if_different(SYS_APP_DIR "/eboot.bin", eboot_bin, eboot_bin_len);
    p = write_if_different(USER_APP_DIR "/sce_sys/param.json", app_param_json, app_param_json_len);
    i = write_if_different(USER_APP_DIR "/sce_sys/icon0.png", icon0_png, icon0_png_len);
    if (e < 0 || p < 0 || i < 0) {
        notify("Error al copiar los ficheros de la app " TITLE_ID);
        return -1;
    }

    if (!registered || p || i) {
        unlink(APP_MARKER);
        /* Como en el ejemplo del SDK: al instalar no debe estar el de sistema */
        unlink(SYS_APP_DIR "/sce_sys/param.json");
        if ((err = sceAppInstUtilInitialize()) != 0) {
            notify("Error al instalar el icono: sceAppInstUtilInitialize 0x%08X", err);
            return -1;
        }
        err = sceAppInstUtilAppInstallTitleDir(TITLE_ID, "/user/app/", 0);
        sceAppInstUtilTerminate();
        if (err != 0) {
            notify("Error al instalar el icono: sceAppInstUtilAppInstallTitleDir 0x%08X", err);
            return -1;
        }
    }

    /* Como en el ejemplo del SDK: el param.json de sistema va después de instalar */
    s = write_if_different(SYS_APP_DIR "/sce_sys/param.json",
                           app_param_system_json, app_param_system_json_len);
    if (s < 0) {
        notify("Error al copiar el param.json de sistema de " TITLE_ID);
        return -1;
    }
    if (!file_exists(APP_MARKER)) {
        FILE *fp = fopen(APP_MARKER, "wb");
        if (fp) {
            fputs(APP_VERSION "\n", fp);
            fclose(fp);
        }
    }

    if (!registered)
        notify("Icono «Actualizar Payload Manager» instalado en la pantalla de inicio");
    else if (e || p || i || s)
        notify("App «Actualizar Payload Manager» actualizada");
    return 0;
}

/* Quita el acceso directo antiguo (AUPM00001), que abría el navegador */
static void remove_old_shortcut(void) {
    struct stat st;
    int err;

    if (stat("/user/app/" OLD_TITLE_ID, &st) != 0) return;
    if (sceAppInstUtilInitialize() != 0) return;
    err = sceAppInstUtilAppUnInstall(OLD_TITLE_ID);
    sceAppInstUtilTerminate();
    if (err == 0 && stat("/user/app/" OLD_TITLE_ID, &st) != 0)
        notify("Quitado el icono antiguo (" OLD_TITLE_ID ")");
    else
        printf("[" APP_NAME "] sceAppInstUtilAppUnInstall(" OLD_TITLE_ID "): 0x%08X\n", err);
}

/* ------------------------------------------------------------------------ */
/* DNS local: pepedns o nanoDNS                                              */
/* Con el DNS de la consola en 127.0.0.1, sin ellos no hay internet.         */
/* ------------------------------------------------------------------------ */

#define ELFLDR_PORT 9021

/* ¿Hay algo respondiendo a consultas DNS en 127.0.0.1:53? */
static int local_dns_answers(int timeout_ms) {
    static const unsigned char query[] = {
        0xA5, 0x5A, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0,
        0x00, 0x01, 0x00, 0x01, /* A, IN */
    };
    unsigned char resp[512];
    struct sockaddr_in addr;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int s = socket(AF_INET, SOCK_DGRAM, 0), ok = 0;

    if (s < 0) return 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (sendto(s, query, sizeof(query), 0, (struct sockaddr *)&addr, sizeof(addr)) == (ssize_t)sizeof(query)) {
        ssize_t n = recv(s, resp, sizeof(resp), 0);
        /* Vale cualquier respuesta a nuestra consulta (aunque sea un bloqueo) */
        ok = n >= 12 && resp[0] == query[0] && resp[1] == query[1] && (resp[2] & 0x80);
    }
    close(s);
    return ok;
}

static int name_has(const char *name, const char *needle) {
    size_t nl = strlen(needle);
    for (; *name; name++)
        if (strncasecmp(name, needle, nl) == 0) return 1;
    return 0;
}

/* Busca en dir (y subcarpetas, como payloads/repo/... de Payload Manager) el
 * ELF de pepedns (preferido) o nanoDNS; si hay varios, el más reciente. */
static void find_dns_payload(const char *dir, int depth, char *best, size_t bestsz,
                             int *best_rank, time_t *best_mtime) {
    DIR *d = opendir(dir);
    struct dirent *ent;

    if (!d) return;
    while ((ent = readdir(d)) != NULL) {
        char path[768];
        struct stat st;

        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth > 0) find_dns_payload(path, depth - 1, best, bestsz, best_rank, best_mtime);
            continue;
        }
        if (!S_ISREG(st.st_mode) || st.st_size == 0) continue;

        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || (strcasecmp(ext, ".elf") != 0 && strcasecmp(ext, ".bin") != 0)) continue;

        int rank = name_has(ent->d_name, "pepedns") ? 2 : name_has(ent->d_name, "nanodns") ? 1 : 0;
        if (!rank || !is_elf(path)) continue;
        if (rank > *best_rank || (rank == *best_rank && st.st_mtime > *best_mtime)) {
            snprintf(best, bestsz, "%s", path);
            *best_rank = rank;
            *best_mtime = st.st_mtime;
        }
    }
    closedir(d);
}

/* Manda un ELF a elfldr, igual que hace Payload Manager */
static int send_to_elfldr(const char *path) {
    static char buf[64 * 1024];
    struct sockaddr_in addr;
    int fd, s = -1, err = 0;

    if ((fd = open(path, O_RDONLY)) < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ELFLDR_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (int i = 0; i < 10 && s < 0; i++) {
        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s >= 0 && connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            close(s);
            s = -1;
            usleep(300 * 1000);
        }
    }
    if (s < 0) {
        close(fd);
        return -1;
    }

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            err = 1;
            break;
        }
        for (ssize_t off = 0; off < n;) {
            ssize_t w = send(s, buf + off, (size_t)(n - off), MSG_NOSIGNAL);
            if (w < 0) {
                if (errno == EINTR) continue;
                err = 1;
                break;
            }
            off += w;
        }
        if (err) break;
    }
    close(s);
    close(fd);
    return err ? -1 : 0;
}

static void ensure_local_dns(void) {
    char path[768] = "";
    int rank = 0;
    time_t mtime = 0;

    find_dns_payload(UPDATER_DIR, 4, path, sizeof(path), &rank, &mtime);
    if (!rank) return;                    /* no hay pepedns ni nanoDNS */
    if (local_dns_answers(1500)) return;  /* ya está en marcha */

    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    notify("Arrancando %s para tener internet...", name);
    if (send_to_elfldr(path) != 0) {
        notify("No se pudo arrancar %s: elfldr no responde en el puerto %d", name, ELFLDR_PORT);
        return;
    }
    for (int i = 0; i < 20; i++) {
        if (local_dns_answers(500)) {
            notify("%s en marcha", name);
            return;
        }
        usleep(500 * 1000);
    }
    notify("Aviso: %s no responde todavía; sigo igualmente", name);
}

/* ------------------------------------------------------------------------ */
/* La actualización completa                                                 */
/* ------------------------------------------------------------------------ */

static int run_update(void) {
    static const char *const entries[] = { PLDMGR_FILE, NULL };
    char version[64];
    char *json;
    int r, rs, self_existed;

    g_cert_error = 0;

    /* 1. Payload Manager en /data/ps5_autoloader + autoload.txt */
    if (mkdir(AUTOLOAD_DIR, 0777) != 0 && errno != EEXIST) {
        notify("Error: no se pudo crear " AUTOLOAD_DIR);
        return -1;
    }

    remove_legacy_autostart(); /* no necesita red */
    ensure_local_dns();        /* pepedns/nanoDNS primero, para tener internet */

    json = download_to_string(REPO_JSON_URL);
    if (!json) {
        notify(g_cert_error ? "Error de certificado HTTPS. ¿Está bien la fecha y hora de la consola?"
                            : "Error al descargar payloads.json");
        return -1;
    }

    r = install_from_repo(json, PLDMGR_ID, AUTOLOAD_DIR "/" PLDMGR_FILE, "Payload Manager",
                          version, sizeof(version));
    if (r == UPD_CURRENT)
        notify("Payload Manager %s ya está al día", version);
    else if (r == UPD_INSTALLED)
        notify("Payload Manager %s instalado. Se cargará en el próximo arranque.", version);

    if (file_exists(AUTOLOAD_DIR "/" PLDMGR_FILE) && ensure_autoload_entries(entries) < 0)
        notify("Aviso: no se pudo actualizar " AUTOLOAD_TXT);

    /* 2. Copia del actualizador en /data/pldmgr (la que lanza el icono) */
    self_existed = file_exists(UPDATER_PATH);
    if (mkdir_p(UPDATER_DIR) != 0) {
        notify("Error: no se pudo crear " UPDATER_DIR);
    } else {
        rs = install_from_repo(json, SELF_ID, UPDATER_PATH, APP_NAME, version, sizeof(version));
        if (rs == UPD_INSTALLED)
            notify(self_existed ? APP_NAME " %s actualizado en " UPDATER_DIR
                                : APP_NAME " %s copiado en " UPDATER_DIR, version);
    }
    if (!file_exists(UPDATER_PATH))
        notify("Aviso: falta " UPDATER_PATH ". El icono no funcionará hasta que se pueda descargar.");

    free(json);
    return r == UPD_ERROR ? -1 : 0;
}

/* ------------------------------------------------------------------------ */

int main(void) {
    int rc;

    notify(APP_NAME " v" APP_VERSION ": buscando actualización de Payload Manager...");

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        notify("Error al iniciar cURL");
        rc = -1;
    } else {
        rc = run_update();
        curl_global_cleanup();
    }

    /* 3. El icono no necesita red: se instala o actualiza siempre */
    install_app();
    remove_old_shortcut(); /* el enlace antiguo ya no funciona: fuera siempre */

    return rc;
}
