/*
 * Copia de PS5-AutoupdaterPM que gestiona Payload Manager.
 *
 * Cuando se instala un payload desde un repositorio, Payload Manager lo guarda
 * en su propia carpeta («slot»):
 *
 *   /data/pldmgr/payloads/repo/<id>@<hash>/meta.json   (versión, checksum...)
 *   /data/pldmgr/payloads/repo/<id>@<hash>/<filename>  (el ELF)
 *
 * y la versión que enseña en su lista sale de meta.json. Este fichero lo usan
 * el actualizador (main.c).
 */
#pragma once

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define PM_REPO_DIR     "/data/pldmgr/payloads/repo"
#define PM_SELF_ID      "ps5-autoupdaterpm"
#define PM_MAX_SLOTS    8
#define PM_META_MAX     (64 * 1024)

typedef struct {
    char dir[512];      /* carpeta del slot  */
    char meta[576];     /* .../meta.json     */
    char payload[832];  /* .../<filename>    */
} pm_slot_t;

/* Lee un fichero de texto entero (como mucho PM_META_MAX). Hay que liberarlo. */
static char *pm_read_text(const char *path, size_t *len_out) {
    FILE *fp = fopen(path, "rb");
    char *buf;
    size_t n;

    if (!fp) return NULL;
    buf = malloc(PM_META_MAX + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    n = fread(buf, 1, PM_META_MAX, fp);
    int bad = ferror(fp) || (n == PM_META_MAX && fgetc(fp) != EOF);
    fclose(fp);
    if (bad) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    if (len_out) *len_out = n;
    return buf;
}

/* p apunta a la comilla inicial; devuelve el puntero tras la comilla final. */
static const char *pm_skip_string(const char *p, const char *end) {
    for (p++; p < end && *p != '"'; p++)
        if (*p == '\\' && p + 1 < end) p++;
    return p < end ? p + 1 : NULL;
}

/* Busca la clave key en el primer nivel del objeto JSON. Si su valor es una
 * cadena, devuelve sus límites (sin comillas) en *vs y *ve. */
static int pm_json_find(const char *json, size_t len, const char *key,
                        const char **vs, const char **ve) {
    const char *p = json, *end = json + len;
    size_t klen = strlen(key);
    int depth = 0;

    while (p < end) {
        if (*p == '"') {
            const char *s = p + 1, *q = pm_skip_string(p, end);
            if (!q) return -1;
            const char *e = q - 1;
            p = q;
            if (depth != 1) continue;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (p >= end || *p != ':') continue;
            if ((size_t)(e - s) != klen || memcmp(s, key, klen) != 0) continue;
            p++;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (p >= end || *p != '"') return -1;
            q = pm_skip_string(p, end);
            if (!q) return -1;
            *vs = p + 1;
            *ve = q - 1;
            return 0;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') depth--;
        p++;
    }
    return -1;
}

/* Copia el valor (sin des-escapar; basta para id y nombres de fichero). */
static int pm_json_get(const char *json, size_t len, const char *key, char *out, size_t outsz) {
    const char *vs, *ve;

    if (pm_json_find(json, len, key, &vs, &ve) != 0 || (size_t)(ve - vs) >= outsz) return -1;
    memcpy(out, vs, (size_t)(ve - vs));
    out[ve - vs] = '\0';
    return 0;
}

static int pm_supported_ext(const char *name) {
    const char *ext = strrchr(name, '.');
    return ext && (strcasecmp(ext, ".elf") == 0 || strcasecmp(ext, ".bin") == 0);
}

/* Igual que Payload Manager: el fichero de meta.json o, si no, el primer
 * .elf/.bin de la carpeta. */
static int pm_slot_payload(pm_slot_t *slot, const char *meta, size_t meta_len) {
    char filename[256];
    struct stat st;

    if (meta && pm_json_get(meta, meta_len, "filename", filename, sizeof(filename)) == 0 &&
        filename[0] && !strchr(filename, '/')) {
        snprintf(slot->payload, sizeof(slot->payload), "%s/%s", slot->dir, filename);
        if (stat(slot->payload, &st) == 0 && S_ISREG(st.st_mode)) return 0;
    }

    DIR *d = opendir(slot->dir);
    struct dirent *ent;
    if (!d) return -1;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' || !pm_supported_ext(ent->d_name)) continue;
        snprintf(slot->payload, sizeof(slot->payload), "%s/%s", slot->dir, ent->d_name);
        if (stat(slot->payload, &st) == 0 && S_ISREG(st.st_mode)) {
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    slot->payload[0] = '\0';
    return -1;
}

/* Rellena slots con las instalaciones de PS5-AutoupdaterPM que tiene Payload
 * Manager (normalmente una). Devuelve cuántas ha encontrado. */
static int pm_find_self_slots(pm_slot_t *slots, int max) {
    DIR *d = opendir(PM_REPO_DIR);
    struct dirent *ent;
    int n = 0;

    if (!d) return 0;
    while (n < max && (ent = readdir(d)) != NULL) {
        pm_slot_t *s = &slots[n];
        struct stat st;
        char id[128] = "";
        size_t len = 0;

        if (ent->d_name[0] == '.') continue;
        snprintf(s->dir, sizeof(s->dir), "%s/%s", PM_REPO_DIR, ent->d_name);
        if (stat(s->dir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        snprintf(s->meta, sizeof(s->meta), "%s/meta.json", s->dir);

        char *meta = pm_read_text(s->meta, &len);
        if (meta) pm_json_get(meta, len, "id", id, sizeof(id));

        int ours = meta ? strcmp(id, PM_SELF_ID) == 0
                        : strncmp(ent->d_name, PM_SELF_ID "@", sizeof(PM_SELF_ID)) == 0;
        if (ours && pm_slot_payload(s, meta, len) == 0) n++;
        free(meta);
    }
    closedir(d);
    return n;
}
