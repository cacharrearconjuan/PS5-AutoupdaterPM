#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>

typedef struct {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *req, size_t size, int blocking);

static void pldmgr_notify(const char *fmt, ...) {
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);

    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static size_t write_file_cb(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

// Firma ajustada a void *userdata para evitar avisos del compilador
static size_t write_buffer_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    char **buffer = (char **)userdata;
    size_t new_len = size * nmemb;
    size_t old_len = *buffer ? strlen(*buffer) : 0;
    
    char *new_buf = realloc(*buffer, old_len + new_len + 1);
    if (!new_buf) return 0;

    memcpy(new_buf + old_len, ptr, new_len);
    new_buf[old_len + new_len] = '\0';
    *buffer = new_buf;

    return new_len;
}

int download_to_file(const char *url, const char *out_path) {
    CURL *curl;
    FILE *fp;
    CURLcode res = CURLE_FAILED_INIT;

    if (!url || !out_path) return -1;

    fp = fopen(out_path, "wb");
    if (!fp) return -1;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "pldmgr/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // Bypass SSL para evitar fallos de certificados en la PS5
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK || http_code != 200) {
            fclose(fp);
            remove(out_path);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

char* download_to_string(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    char *json_str = strdup("");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_buffer_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &json_str);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pldmgr/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) {
        if (json_str) free(json_str);
        return NULL;
    }

    return json_str;
}

int main(int argc, char *argv[]) {
    pldmgr_notify("Iniciando PS5 Autoloader via cURL...");

    curl_global_init(CURL_GLOBAL_ALL);

    mkdir("/data/ps5_autoloader", 0777);

    FILE *f_auto = fopen("/data/ps5_autoloader/autoload.txt", "wb");
    if (f_auto) {
        const char *contenido = "!3000\npldmgr.elf\n";
        fwrite(contenido, 1, strlen(contenido), f_auto);
        fclose(f_auto);
    } else {
        pldmgr_notify("Error: No se pudo crear autoload.txt");
        curl_global_cleanup();
        return -1;
    }

    const char *json_url = "https://cacharrearconjuan.github.io/mis-payloads-ps5/payloads.json";
    char *json_buf = download_to_string(json_url);

    if (!json_buf) {
        pldmgr_notify("Error al descargar payloads.json");
        curl_global_cleanup();
        return -1;
    }

    char *download_url = NULL;
    char *src = strstr(json_buf, "source_direct");
    if (!src) src = strstr(json_buf, "browser_download_url");
    if (!src) src = strstr(json_buf, "url");

    if (src) {
        char *q1 = strchr(src, ':');
        if (q1) {
            q1 = strchr(q1, '"');
            if (q1) {
                q1++;
                char *q2 = strchr(q1, '"');
                if (q2) {
                    *q2 = '\0';
                    download_url = q1;
                }
            }
        }
    }

    if (!download_url) {
        pldmgr_notify("Error: URL no encontrada en JSON");
        free(json_buf);
        curl_global_cleanup();
        return -1;
    }

    const char *elf_destination = "/data/ps5_autoloader/pldmgr.elf";
    if (download_to_file(download_url, elf_destination) == 0) {
        pldmgr_notify("¡Exito! pldmgr.elf descargado correctamente.");
    } else {
        pldmgr_notify("Error al descargar el ejecutable ELF");
    }

    free(json_buf);
    curl_global_cleanup();
    return 0;
}