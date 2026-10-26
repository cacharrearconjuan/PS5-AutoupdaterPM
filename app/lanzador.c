/*
 * Lanzador del icono «Actualizar Payload Manager».
 *
 * La app del icono (eboot.bin) no tiene permisos para tocar /data, así que lo
 * único que hace es mandar este ELF a elfldr (127.0.0.1:9021). elfldr lo ejecuta
 * con permisos completos y este, a su vez, manda a elfldr el actualizador. Es
 * el mismo método que usa Payload Manager para lanzar payloads.
 *
 * Qué copia lanza: la que gestiona Payload Manager (instalada desde el
 * repositorio, en /data/pldmgr/payloads/repo/...) o, si no hay, la de
 * /data/pldmgr/PS5-AutoupdaterPM.elf.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pm_slots.h"

#ifndef UPDATER_PATH
#define UPDATER_PATH "/data/pldmgr/PS5-AutoupdaterPM.elf"
#endif
#define ELFLDR_PORT 9021

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

    printf("[lanzador] %s\n", req.message);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static int connect_elfldr(void) {
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ELFLDR_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    /* elfldr acaba de lanzarnos: le damos unos instantes por si aún no ha
     * vuelto a aceptar conexiones */
    for (int i = 0; i < 20; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return -1;
        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0) return s;
        close(s);
        usleep(250 * 1000);
    }
    return -1;
}

int main(void) {
    static char buf[64 * 1024];
    static pm_slot_t slots[PM_MAX_SLOTS];
    const char *path = NULL;
    struct stat st;
    size_t total = 0;
    int fd, s, err = 0;

    signal(SIGPIPE, SIG_IGN);

    int n = pm_find_self_slots(slots, PM_MAX_SLOTS);
    for (int i = 0; i < n && !path; i++)
        if (stat(slots[i].payload, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
            path = slots[i].payload;
    if (!path && stat(UPDATER_PATH, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
        path = UPDATER_PATH;
    if (!path) {
        notify("No encuentro PS5-AutoupdaterPM en Payload Manager ni en " UPDATER_PATH ".\n"
               "Ejecútalo una vez desde Payload Manager.");
        return -1;
    }

    if ((fd = open(path, O_RDONLY)) < 0) {
        notify("No se puede leer %s (errno %d)", path, errno);
        return -1;
    }

    if ((s = connect_elfldr()) < 0) {
        close(fd);
        notify("elfldr no responde en el puerto %d", ELFLDR_PORT);
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
            total += (size_t)w;
        }
        if (err) break;
    }

    close(s);
    close(fd);

    if (err || total != (size_t)st.st_size) {
        notify("Error al mandar %s a elfldr", path);
        return -1;
    }
    return 0;
}
