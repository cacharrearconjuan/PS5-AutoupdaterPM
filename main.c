/*
 * PS5-AutoupdaterPM
 *
 * Descarga la última versión de PS5 Payload Manager (pldmgr.elf) indicada en
 * el repositorio de payloads, verifica su SHA-256 y la instala en
 * /data/ps5_autoloader, añadiéndola a autoload.txt sin borrar lo que ya hubiera.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>
#include <mbedtls/sha256.h>

#include "cacert_pem.h" /* generado por el Makefile a partir de cacert.pem */

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define APP_NAME      "PS5-AutoupdaterPM"
#ifndef REPO_JSON_URL
#define REPO_JSON_URL "https://cacharrearconjuan.github.io/mis-payloads-ps5/payloads.json"
#endif
#define PAYLOAD_ID    "pldmgr"

#ifndef AUTOLOAD_DIR
#define AUTOLOAD_DIR  "/data/ps5_autoloader"
#endif
#define AUTOLOAD_TXT  AUTOLOAD_DIR "/autoload.txt"
#define ELF_NAME      "pldmgr.elf"
#define ELF_PATH      AUTOLOAD_DIR "/" ELF_NAME
#define ELF_TMP_PATH  ELF_PATH ".tmp"

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
            notify("Error de certificado HTTPS. ¿Está bien la fecha y hora de la consola?");
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
/* SHA-256 y comprobaciones                                                  */
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

/* ------------------------------------------------------------------------ */
/* autoload.txt: añade pldmgr.elf sin tocar el resto de entradas             */
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

static int ensure_autoload_entry(void) {
    FILE *fp = fopen(AUTOLOAD_TXT, "rb");

    if (!fp) {
        if (errno != ENOENT) return -1;
        fp = fopen(AUTOLOAD_TXT, "wb");
        if (!fp) return -1;
        fputs("!3000\n" ELF_NAME "\n", fp);
        return fclose(fp) == 0 ? 1 : -1;
    }

    char content[64 * 1024];
    size_t n = fread(content, 1, sizeof(content) - 1, fp);
    fclose(fp);
    content[n] = '\0';

    if (autoload_has_entry(content, ELF_NAME)) return 0;

    fp = fopen(AUTOLOAD_TXT, "ab");
    if (!fp) return -1;
    if (n > 0 && content[n - 1] != '\n') fputc('\n', fp);
    fputs(ELF_NAME "\n", fp);
    return fclose(fp) == 0 ? 1 : -1;
}

/* ------------------------------------------------------------------------ */

static void finish_autoload(void) {
    int r = ensure_autoload_entry();
    if (r < 0) notify("Aviso: no se pudo actualizar " AUTOLOAD_TXT);
    else if (r > 0) printf("[" APP_NAME "] " ELF_NAME " añadido a autoload.txt\n");
}

int main(void) {
    char url[FIELD_SIZE] = "", version[64] = "", checksum[128] = "", have[65] = "";
    const char *obj = NULL, *obj_end = NULL;
    char *json = NULL;
    int rc = -1;

    notify(APP_NAME " v" APP_VERSION ": buscando actualización de Payload Manager...");

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        notify("Error al iniciar cURL");
        return -1;
    }

    if (mkdir(AUTOLOAD_DIR, 0777) != 0 && errno != EEXIST) {
        notify("Error: no se pudo crear " AUTOLOAD_DIR);
        goto out;
    }

    json = download_to_string(REPO_JSON_URL);
    if (!json) {
        notify("Error al descargar payloads.json");
        goto out;
    }

    if (find_object_by_id(json, strlen(json), PAYLOAD_ID, &obj, &obj_end) != 0) {
        notify("Error: \"" PAYLOAD_ID "\" no aparece en payloads.json");
        goto out;
    }

    if (get_string_field(obj, obj_end, "url", url, sizeof(url)) != 0 || strncmp(url, "https://", 8) != 0) {
        if (get_string_field(obj, obj_end, "source_direct", url, sizeof(url)) != 0 ||
            strncmp(url, "https://", 8) != 0) {
            notify("Error: URL de " PAYLOAD_ID " no válida en payloads.json");
            goto out;
        }
    }
    get_string_field(obj, obj_end, "version", version, sizeof(version));
    if (!version[0]) strcpy(version, "?");
    get_string_field(obj, obj_end, "checksum", checksum, sizeof(checksum));
    int has_checksum = is_sha256_hex(checksum);

    /* ¿Ya está instalada esa misma versión? */
    if (has_checksum && file_exists(ELF_PATH) && sha256_file_hex(ELF_PATH, have) == 0 &&
        strcasecmp(have, checksum) == 0) {
        finish_autoload();
        notify("Payload Manager ya está al día (%s)", version);
        rc = 0;
        goto out;
    }

    notify("Descargando Payload Manager %s...", version);
    if (download_to_file(url, ELF_TMP_PATH) != 0) {
        notify("Error al descargar Payload Manager %s", version);
        goto out;
    }

    if (!is_elf(ELF_TMP_PATH)) {
        unlink(ELF_TMP_PATH);
        notify("Error: el fichero descargado no es un ELF");
        goto out;
    }

    if (has_checksum) {
        if (sha256_file_hex(ELF_TMP_PATH, have) != 0 || strcasecmp(have, checksum) != 0) {
            unlink(ELF_TMP_PATH);
            notify("Error: el SHA-256 no coincide. Se mantiene la versión anterior.");
            goto out;
        }
    } else {
        printf("[" APP_NAME "] Aviso: payloads.json no trae checksum; solo se comprueba que sea ELF\n");
    }

    /* Sustitución atómica: si algo falla antes, el pldmgr.elf anterior sigue intacto */
    if (rename(ELF_TMP_PATH, ELF_PATH) != 0) {
        unlink(ELF_TMP_PATH);
        notify("Error al instalar " ELF_PATH);
        goto out;
    }

    finish_autoload();
    notify("Payload Manager %s instalado. Se cargará en el próximo arranque.", version);
    rc = 0;

out:
    free(json);
    curl_global_cleanup();
    return rc;
}
