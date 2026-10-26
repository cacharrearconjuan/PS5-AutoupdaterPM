/*
 * eboot.bin de la app «Actualizar Payload Manager».
 *
 * Basado en samples/install_app del PS5 Payload SDK (John Törnblom, GPLv3).
 *
 * Es un ejecutable autónomo (sin libc ni libkernel): las llamadas al sistema
 * se hacen a mano. Una app no tiene permisos para escribir en /data, así que
 * no actualiza nada por sí misma: se conecta a elfldr (127.0.0.1:9021), le
 * manda lanzador.elf (incrustado aquí) y termina. elfldr ejecuta el lanzador
 * con permisos completos y este lanza /data/pldmgr/PS5-AutoupdaterPM.elf.
 */

#include "lanzador_elf.h" /* generado por el Makefile con tools/bin2h.py */

#define SYS_exit      1
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_socket    97
#define SYS_connect   98
#define SYS_sendto    133
#define SYS_nanosleep 240
#define SYS_klog      0x259

#define AF_INET      2
#define SOCK_STREAM  1
#define O_WRONLY     0x0001
#define O_NONBLOCK   0x0004
#define EINTR        4
#define MSG_NOSIGNAL 0x20000 /* que un corte de elfldr no mate la app con SIGPIPE */

#define ELFLDR_PORT  9021

/* Variable escribible inicializada: así el eboot tiene segmento de datos (RW),
 * igual que el del ejemplo del SDK */
static volatile int g_rw_segment = 1;

long __syscall(long n, ...) {
    long a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0;
    __builtin_va_list ap;
    unsigned long ret;
    char iserror;

    __builtin_va_start(ap, n);
    a1 = __builtin_va_arg(ap, long);
    a2 = __builtin_va_arg(ap, long);
    a3 = __builtin_va_arg(ap, long);
    a4 = __builtin_va_arg(ap, long);
    a5 = __builtin_va_arg(ap, long);
    a6 = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);

    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;

    __asm__ __volatile__("syscall"
                         : "=a"(ret), "=@ccc"(iserror), "+r"(r10), "+r"(r8), "+r"(r9)
                         : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                         : "rcx", "r11", "memory");

    return iserror ? -(long)ret : (long)ret;
}

/* El compilador puede generar llamadas a estas dos aunque no las usemos */
void *memset(void *dst, int c, unsigned long n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

static unsigned long str_len(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

static void klog(const char *s) {
    __syscall(SYS_klog, 7, s, 0);
}

static void sleep_ms(long ms) {
    struct { long sec; long nsec; } ts = { ms / 1000, (ms % 1000) * 1000000L };
    __syscall(SYS_nanosleep, &ts, 0);
}

/* Aviso en pantalla, por si elfldr no responde. Es lo mismo que hace
 * sceKernelSendNotificationRequest; si el sistema no lo permite, no pasa nada. */
typedef struct {
    char useless1[45];
    char message[3075];
} notify_request_t;

static void notify(const char *msg) {
    notify_request_t g_notify; /* en la pila: el eboot no tiene .bss */
    unsigned long n = str_len(msg);
    long fd;

    klog(msg);
    klog("\n");
    if (n >= sizeof(g_notify.message)) n = sizeof(g_notify.message) - 1;
    memset(&g_notify, 0, sizeof(g_notify));
    memcpy(g_notify.message, msg, n);

    fd = __syscall(SYS_open, "/dev/notification0", O_WRONLY | O_NONBLOCK, 0);
    if (fd >= 0) {
        __syscall(SYS_write, fd, &g_notify, sizeof(g_notify));
        __syscall(SYS_close, fd);
    }
}

/* sockaddr_in de FreeBSD */
struct sockaddr_in_bsd {
    unsigned char  sin_len;
    unsigned char  sin_family;
    unsigned short sin_port;   /* orden de red */
    unsigned int   sin_addr;   /* orden de red */
    char           sin_zero[8];
};

static long connect_elfldr(void) {
    struct sockaddr_in_bsd addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = (unsigned short)(((ELFLDR_PORT & 0xff) << 8) | (ELFLDR_PORT >> 8));
    addr.sin_addr = 0x0100007f; /* 127.0.0.1 */

    for (int i = 0; i < 10; i++) {
        long s = __syscall(SYS_socket, AF_INET, SOCK_STREAM, 0);
        if (s < 0) return s;
        if (__syscall(SYS_connect, s, &addr, sizeof(addr)) == 0) return s;
        __syscall(SYS_close, s);
        sleep_ms(300);
    }
    return -1;
}

static int send_all(long s, const unsigned char *p, unsigned long len) {
    while (len) {
        long w = __syscall(SYS_sendto, s, p, len, MSG_NOSIGNAL, 0, 0);
        if (w == -EINTR) continue;
        if (w <= 0) return -1;
        p += w;
        len -= (unsigned long)w;
    }
    return 0;
}

int main(void) {
    long s;

    klog("[AUPM] Actualizar Payload Manager: enviando lanzador a elfldr\n");
    g_rw_segment = 0;

    s = connect_elfldr();
    if (s < 0) {
        notify("Actualizar Payload Manager:\nelfldr no responde en el puerto 9021");
        return -1;
    }
    if (send_all(s, lanzador_elf, lanzador_elf_len) != 0) {
        __syscall(SYS_close, s);
        notify("Actualizar Payload Manager:\nerror al enviar el lanzador a elfldr");
        return -1;
    }
    __syscall(SYS_close, s);
    return 0;
}

__attribute__((force_align_arg_pointer))
void _start(void) {
    __syscall(SYS_exit, main());
    __builtin_trap();
}
