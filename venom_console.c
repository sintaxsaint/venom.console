/*
 * venom_console.c — Cross-platform power console in C
 * Version: 3.0
 * OS Support: Windows, Linux, macOS
 *
 * Build:
 *   Windows (MinGW):  gcc venom_console.c -o venom_console.exe -lws2_32
 *   Windows (MSVC):   cl venom_console.c /Fe:venom_console.exe /link ws2_32.lib
 *   Linux/macOS:      gcc venom_console.c -o venom_console -lm
 *
 * Elevation:
 *   Windows: embed venom_console.manifest at link time
 *     mt.exe -manifest venom_console.manifest -outputresource:venom_console.exe;1
 *   Linux/macOS: use the venom launcher wrapper script
 *
 * Features:
 *   System, Network, Files, Hashing, Encoding, Text Utils,
 *   AI (Ollama local + Remote + HuggingFace), Setup Wizard,
 *   Local HTTP Server, Port Tools, IP Tools, Fun, Kill Switch, Config
 */

/* ================================================================
   PLATFORM DETECTION & HEADERS
   ================================================================ */
#if defined(_WIN32) || defined(_WIN64)
    #define VENOM_WINDOWS 1
    #define _WIN32_WINNT 0x0601
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <tlhelp32.h>
    #include <shlobj.h>
    #include <lmcons.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    typedef SOCKET venom_sock_t;
    #define INVALID_VENOM_SOCK INVALID_SOCKET
    #define venom_closesocket  closesocket
    #define PATH_SEP           '\\'
    #define PATH_SEP_STR       "\\"
    #define CLEAR_CMD          "cls"
    #define HOSTS_PATH         "C:\\Windows\\System32\\drivers\\etc\\hosts"
#else
    #define VENOM_POSIX 1
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/stat.h>
    #include <sys/utsname.h>
    #include <sys/statvfs.h>
    #include <sys/wait.h>
    #include <sys/ioctl.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <dirent.h>
    #include <unistd.h>
    #include <pwd.h>
    #include <ifaddrs.h>
    #include <signal.h>
    typedef int venom_sock_t;
    #define INVALID_VENOM_SOCK (-1)
    #define venom_closesocket  close
    #define PATH_SEP           '/'
    #define PATH_SEP_STR       "/"
    #define CLEAR_CMD          "clear"
    #define HOSTS_PATH         "/etc/hosts"
    #define SOCKET_ERROR       (-1)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* ================================================================
   VERSION & CONSTANTS
   ================================================================ */
#define VENOM_VERSION    "venom.console v3.0"
#define VENOM_MAX_INPUT  4096
#define MAX_PATH_LEN     2048
#define CFG_HF_KEY_MAX   256
#define CFG_PIN_MAX      64
#define CFG_VAL_MAX      512
#define KILLSWITCH_FILE  ".venom_dead"
#define SERVE_STATE_FILE ".venom_serve"

/* ================================================================
   GLOBALS
   ================================================================ */
static char g_cwd[MAX_PATH_LEN];
static char g_hist_path[MAX_PATH_LEN];
static char g_config_path[MAX_PATH_LEN];
static volatile int g_interrupted = 0;
static volatile int g_server_running = 0;
static int  g_server_port = 0;

/* ================================================================
   CONFIG STRUCT
   ================================================================ */
typedef struct {
    /* AI */
    char hf_key[CFG_HF_KEY_MAX];
    char ai_provider[32];       /* ollama | huggingface | remote */
    char ollama_host[128];      /* default: localhost:11434 */
    char ollama_model[64];
    char remote_endpoint[CFG_VAL_MAX];
    char remote_key[CFG_HF_KEY_MAX];
    char remote_model[64];
    int  ai_console_access;     /* 0=none 1=read 2=full */
    /* Matrix */
    char matrix_color[32];      /* rainbow|green|red|cyan|white */
    int  matrix_speed;          /* 1-10 */
    char matrix_chars[32];      /* binary|ascii|hex */
    /* Display */
    int  ls_colors;
    int  ping_graph;
    char tree_style[16];        /* box|plain */
    /* Utils */
    int  util_sidebyside;
    int  guid_count;
    int  calc_precision;
    int  timer_style;           /* 0=number 1=bar */
    int  typewriter_speed;      /* ms per char */
    /* Server */
    int  serve_port;
    char serve_dir[MAX_PATH_LEN];
    int  serve_autostart;
    /* Kill switch */
    char pin[CFG_PIN_MAX];
    int  killswitch_enabled;
} VenomConfig;

static VenomConfig g_cfg = {
    "",                  /* hf_key */
    "ollama",            /* ai_provider */
    "localhost:11434",   /* ollama_host */
    "llama3.2",          /* ollama_model */
    "",                  /* remote_endpoint */
    "",                  /* remote_key */
    "",                  /* remote_model */
    0,                   /* ai_console_access */
    "rainbow",           /* matrix_color */
    5,                   /* matrix_speed */
    "binary",            /* matrix_chars */
    1,                   /* ls_colors */
    1,                   /* ping_graph */
    "box",               /* tree_style */
    1,                   /* util_sidebyside */
    1,                   /* guid_count */
    4,                   /* calc_precision */
    1,                   /* timer_style: bar */
    50,                  /* typewriter_speed */
    8080,                /* serve_port */
    ".",                 /* serve_dir */
    0,                   /* serve_autostart */
    "",                  /* pin */
    0                    /* killswitch_enabled */
};

/* ================================================================
   COLOR SYSTEM
   ================================================================ */
typedef enum {
    COL_DEFAULT = 0,
    COL_BLUE, COL_CYAN, COL_GREEN, COL_RED,
    COL_YELLOW, COL_MAGENTA, COL_WHITE, COL_BOLD,
    COL_DIM
} VenomColor;

/* Rainbow cycle: returns a color cycling through the spectrum */
static VenomColor rainbow_color(int idx) {
    static VenomColor cycle[] = {
        COL_RED, COL_YELLOW, COL_GREEN,
        COL_CYAN, COL_BLUE, COL_MAGENTA
    };
    return cycle[idx % 6];
}

static void venom_set_color(VenomColor c) {
#ifdef VENOM_WINDOWS
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD attr;
    switch (c) {
        case COL_BLUE:    attr = FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
        case COL_CYAN:    attr = FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY; break;
        case COL_GREEN:   attr = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
        case COL_RED:     attr = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
        case COL_YELLOW:  attr = FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY; break;
        case COL_MAGENTA: attr = FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY; break;
        case COL_WHITE:
        case COL_BOLD:    attr = FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY; break;
        case COL_DIM:     attr = FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE; break;
        default:          attr = FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE; break;
    }
    SetConsoleTextAttribute(h, attr);
#else
    switch (c) {
        case COL_BLUE:    printf("\033[1;34m"); break;
        case COL_CYAN:    printf("\033[1;36m"); break;
        case COL_GREEN:   printf("\033[1;32m"); break;
        case COL_RED:     printf("\033[1;31m"); break;
        case COL_YELLOW:  printf("\033[1;33m"); break;
        case COL_MAGENTA: printf("\033[1;35m"); break;
        case COL_WHITE:
        case COL_BOLD:    printf("\033[1;37m"); break;
        case COL_DIM:     printf("\033[2m");    break;
        default:          printf("\033[0m");    break;
    }
    fflush(stdout);
#endif
}

static void venom_reset_color(void) {
#ifdef VENOM_WINDOWS
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(h, FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE);
#else
    printf("\033[0m"); fflush(stdout);
#endif
}

static void vprint(VenomColor c, const char *fmt, ...) {
    va_list ap;
    venom_set_color(c);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    venom_reset_color();
}

/* Draw a horizontal rule */
static void venom_rule(VenomColor c, int width) {
    venom_set_color(c);
    for (int i = 0; i < width; i++) printf("─");
    printf("\n");
    venom_reset_color();
}

/* Boxed label */
static void venom_box_label(const char *label, VenomColor c) {
    int len = (int)strlen(label);
    venom_set_color(c);
    printf("┌");
    for (int i = 0; i < len+2; i++) printf("─");
    printf("┐\n│ %s │\n└", label);
    for (int i = 0; i < len+2; i++) printf("─");
    printf("┘\n");
    venom_reset_color();
}

/* ================================================================
   UTILITIES
   ================================================================ */
static void str_trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

static void str_lower(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void human_size(long long bytes, char *buf, size_t buflen) {
    const char *units[] = {"B","KB","MB","GB","TB"};
    double sz = (double)bytes;
    int i = 0;
    while (sz >= 1024.0 && i < 4) { sz /= 1024.0; i++; }
    snprintf(buf, buflen, "%.2f %s", sz, units[i]);
}

static void venom_sleep_ms(int ms) {
#ifdef VENOM_WINDOWS
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* Progress bar: fill 0-100 */
static void draw_progress_bar(int pct, int width, VenomColor c) {
    int filled = (pct * width) / 100;
    venom_set_color(c);
    printf("[");
    for (int i = 0; i < width; i++)
        printf(i < filled ? "█" : "░");
    printf("] %3d%%", pct);
    venom_reset_color();
}

/* Side-by-side display */
static void print_sidebyside(const char *label_a, const char *val_a,
                              const char *label_b, const char *val_b) {
    vprint(COL_DIM,  "  %-10s", label_a);
    vprint(COL_WHITE, "%-30s", val_a);
    vprint(COL_DIM,  "  %-10s", label_b);
    vprint(COL_CYAN,  "%s\n", val_b);
}

/* ================================================================
   HISTORY
   ================================================================ */
static void history_save(const char *entry) {
    FILE *f = fopen(g_hist_path, "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", t);
    fprintf(f, "%s %s\n", ts, entry);
    fclose(f);
}

/* ================================================================
   CONFIG — SAVE / LOAD
   ================================================================ */
static void config_save(void) {
    FILE *f = fopen(g_config_path, "w");
    if (!f) { printf("config: cannot write '%s'\n", g_config_path); return; }
    fprintf(f, "# venom.console config\n");
    fprintf(f, "hf_key=%s\n",             g_cfg.hf_key);
    fprintf(f, "ai_provider=%s\n",        g_cfg.ai_provider);
    fprintf(f, "ollama_host=%s\n",        g_cfg.ollama_host);
    fprintf(f, "ollama_model=%s\n",       g_cfg.ollama_model);
    fprintf(f, "remote_endpoint=%s\n",    g_cfg.remote_endpoint);
    fprintf(f, "remote_key=%s\n",         g_cfg.remote_key);
    fprintf(f, "remote_model=%s\n",       g_cfg.remote_model);
    fprintf(f, "ai_console_access=%d\n",  g_cfg.ai_console_access);
    fprintf(f, "matrix_color=%s\n",       g_cfg.matrix_color);
    fprintf(f, "matrix_speed=%d\n",       g_cfg.matrix_speed);
    fprintf(f, "matrix_chars=%s\n",       g_cfg.matrix_chars);
    fprintf(f, "ls_colors=%d\n",          g_cfg.ls_colors);
    fprintf(f, "ping_graph=%d\n",         g_cfg.ping_graph);
    fprintf(f, "tree_style=%s\n",         g_cfg.tree_style);
    fprintf(f, "util_sidebyside=%d\n",    g_cfg.util_sidebyside);
    fprintf(f, "guid_count=%d\n",         g_cfg.guid_count);
    fprintf(f, "calc_precision=%d\n",     g_cfg.calc_precision);
    fprintf(f, "timer_style=%d\n",        g_cfg.timer_style);
    fprintf(f, "typewriter_speed=%d\n",   g_cfg.typewriter_speed);
    fprintf(f, "serve_port=%d\n",         g_cfg.serve_port);
    fprintf(f, "serve_dir=%s\n",          g_cfg.serve_dir);
    fprintf(f, "serve_autostart=%d\n",    g_cfg.serve_autostart);
    fprintf(f, "pin=%s\n",               g_cfg.pin);
    fprintf(f, "killswitch_enabled=%d\n", g_cfg.killswitch_enabled);
    fclose(f);
}

static void config_load(void) {
    FILE *f = fopen(g_config_path, "r");
    if (!f) return;
    char line[CFG_VAL_MAX + 64];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char *eq = strchr(line, '=');  if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq+1;
        if (!strcmp(key,"hf_key"))            strncpy(g_cfg.hf_key,            val, CFG_HF_KEY_MAX-1);
        else if (!strcmp(key,"ai_provider"))  strncpy(g_cfg.ai_provider,       val, 31);
        else if (!strcmp(key,"ollama_host"))  strncpy(g_cfg.ollama_host,        val, 127);
        else if (!strcmp(key,"ollama_model")) strncpy(g_cfg.ollama_model,       val, 63);
        else if (!strcmp(key,"remote_endpoint")) strncpy(g_cfg.remote_endpoint, val, CFG_VAL_MAX-1);
        else if (!strcmp(key,"remote_key"))   strncpy(g_cfg.remote_key,         val, CFG_HF_KEY_MAX-1);
        else if (!strcmp(key,"remote_model")) strncpy(g_cfg.remote_model,       val, 63);
        else if (!strcmp(key,"ai_console_access")) g_cfg.ai_console_access = atoi(val);
        else if (!strcmp(key,"matrix_color")) strncpy(g_cfg.matrix_color,       val, 31);
        else if (!strcmp(key,"matrix_speed")) g_cfg.matrix_speed = atoi(val);
        else if (!strcmp(key,"matrix_chars")) strncpy(g_cfg.matrix_chars,       val, 31);
        else if (!strcmp(key,"ls_colors"))    g_cfg.ls_colors       = atoi(val);
        else if (!strcmp(key,"ping_graph"))   g_cfg.ping_graph      = atoi(val);
        else if (!strcmp(key,"tree_style"))   strncpy(g_cfg.tree_style, val, 15);
        else if (!strcmp(key,"util_sidebyside")) g_cfg.util_sidebyside = atoi(val);
        else if (!strcmp(key,"guid_count"))   g_cfg.guid_count      = atoi(val);
        else if (!strcmp(key,"calc_precision")) g_cfg.calc_precision = atoi(val);
        else if (!strcmp(key,"timer_style"))  g_cfg.timer_style     = atoi(val);
        else if (!strcmp(key,"typewriter_speed")) g_cfg.typewriter_speed = atoi(val);
        else if (!strcmp(key,"serve_port"))   g_cfg.serve_port      = atoi(val);
        else if (!strcmp(key,"serve_dir"))    strncpy(g_cfg.serve_dir, val, MAX_PATH_LEN-1);
        else if (!strcmp(key,"serve_autostart")) g_cfg.serve_autostart = atoi(val);
        else if (!strcmp(key,"pin"))          strncpy(g_cfg.pin, val, CFG_PIN_MAX-1);
        else if (!strcmp(key,"killswitch_enabled")) g_cfg.killswitch_enabled = atoi(val);
    }
    fclose(f);
}

/* ================================================================
   KILL SWITCH
   ================================================================ */
static void ks_get_paths(char *cwd_path, char *cfg_path) {
    snprintf(cwd_path, MAX_PATH_LEN, "%s%c%s", g_cwd, PATH_SEP, KILLSWITCH_FILE);
    char *last = strrchr(g_config_path, PATH_SEP);
    if (last) {
        size_t dlen = (size_t)(last - g_config_path);
        char cfg_dir[MAX_PATH_LEN];
        strncpy(cfg_dir, g_config_path, dlen); cfg_dir[dlen] = '\0';
        snprintf(cfg_path, MAX_PATH_LEN, "%s%c%s", cfg_dir, PATH_SEP, KILLSWITCH_FILE);
    } else {
        strncpy(cfg_path, cwd_path, MAX_PATH_LEN-1);
    }
}

static int killswitch_triggered(void) {
    char p1[MAX_PATH_LEN], p2[MAX_PATH_LEN];
    ks_get_paths(p1, p2);
    FILE *f = fopen(p1, "r"); if (f) { fclose(f); return 1; }
    f = fopen(p2, "r"); if (f) { fclose(f); return 1; }
    return 0;
}

static void killswitch_arm_files(void) {
    char p1[MAX_PATH_LEN], p2[MAX_PATH_LEN];
    ks_get_paths(p1, p2);
    FILE *f = fopen(p1, "w"); if (f) { fprintf(f, "VENOM_DEAD\n"); fclose(f); }
    f = fopen(p2, "w"); if (f) { fprintf(f, "VENOM_DEAD\n"); fclose(f); }
}

static void killswitch_disarm_files(void) {
    char p1[MAX_PATH_LEN], p2[MAX_PATH_LEN];
    ks_get_paths(p1, p2);
    remove(p1); remove(p2);
}

/* ================================================================
   SIGNAL HANDLER
   ================================================================ */
#ifdef VENOM_POSIX
static void sig_handler(int sig) {
    if (sig == SIGINT) { g_interrupted = 1; printf("\n[Interrupted]\n"); }
}
#endif

/* ================================================================
   NETWORK INIT
   ================================================================ */
static void net_init(void) {
#ifdef VENOM_WINDOWS
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
}

/* Simple HTTP GET helper — returns body in caller-provided buffer */
static int http_get(const char *host, int port, const char *path,
                    char *out, int outlen) {
    venom_sock_t s;
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((unsigned short)port);
    struct hostent *he = gethostbyname(host);
    if (!he) return -1;
    memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_VENOM_SOCK) return -1;
#ifdef VENOM_WINDOWS
    DWORD tv = 8000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
#else
    struct timeval tv = {8,0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        venom_closesocket(s); return -1;
    }
    char req[1024];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: venom/3.0\r\n\r\n",
        path, host);
    send(s, req, (int)strlen(req), 0);
    int total = 0, n;
    char buf[4096];
    while ((n = recv(s, buf, sizeof(buf)-1, 0)) > 0) {
        buf[n] = '\0';
        if (total + n < outlen-1) {
            memcpy(out+total, buf, n); total += n;
        }
    }
    out[total] = '\0';
    venom_closesocket(s);
    /* Strip HTTP headers */
    char *body = strstr(out, "\r\n\r\n");
    if (body) { body += 4; memmove(out, body, strlen(body)+1); }
    return total;
}

/* ================================================================
   SYSTEM COMMANDS
   ================================================================ */
static void cmd_cls(const char *a)     { (void)a; system(CLEAR_CMD); }
static void cmd_echo(const char *a)    { printf("%s\n", a ? a : ""); }

static void cmd_version(const char *a) {
    (void)a;
    venom_box_label(VENOM_VERSION, COL_GREEN);
    printf("Platform : ");
#ifdef VENOM_WINDOWS
    printf("Windows\n");
#else
    struct utsname u; uname(&u);
    printf("%s %s %s\n", u.sysname, u.release, u.machine);
#endif
}

static void cmd_time_cmd(const char *a) {
    (void)a;
    time_t now = time(NULL); struct tm *t = localtime(&now);
    char buf[32]; strftime(buf, sizeof(buf), "%H:%M:%S", t);
    vprint(COL_CYAN, "  ⏰ %s\n", buf);
}

static void cmd_date_cmd(const char *a) {
    (void)a;
    time_t now = time(NULL); struct tm *t = localtime(&now);
    char buf[32]; strftime(buf, sizeof(buf), "%A, %d %B %Y", t);
    vprint(COL_CYAN, "  📅 %s\n", buf);
}

static void cmd_whoami(const char *a) {
    (void)a;
    venom_box_label("Identity", COL_CYAN);
#ifdef VENOM_WINDOWS
    char user[256], host[256];
    DWORD ul=sizeof(user), hl=sizeof(host);
    GetUserNameA(user, &ul); GetComputerNameA(host, &hl);
    printf("  User     : %s\n", user);
    printf("  Host     : %s\n", host);
    /* Admin check */
    BOOL admin = FALSE; HANDLE tok = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION e; DWORD sz = sizeof(e);
        if (GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &sz))
            admin = (BOOL)e.TokenIsElevated;
        CloseHandle(tok);
    }
    printf("  Admin    : "); vprint(admin?COL_GREEN:COL_YELLOW, "%s\n", admin?"YES":"NO");
#else
    char host[256]; gethostname(host, sizeof(host));
    struct passwd *pw = getpwuid(getuid());
    printf("  User     : %s\n", pw ? pw->pw_name : "unknown");
    printf("  Host     : %s\n", host);
    printf("  Root     : "); vprint(getuid()==0?COL_GREEN:COL_YELLOW,
                                    "%s\n", getuid()==0?"YES":"NO");
    printf("  UID/EUID : %d / %d\n", (int)getuid(), (int)geteuid());
#endif
    cmd_time_cmd(NULL);
}

static void cmd_whereami(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    char buf[MAX_PATH_LEN]; GetCurrentDirectoryA(sizeof(buf), buf);
    vprint(COL_CYAN, "  📁 %s\n", buf);
#else
    char buf[MAX_PATH_LEN];
    if (getcwd(buf, sizeof(buf))) vprint(COL_CYAN, "  📁 %s\n", buf);
#endif
}

static void cmd_uptime(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    ULONGLONG ms = GetTickCount64();
    unsigned long long s=ms/1000, m=s/60, h=m/60, d=h/24;
    printf("  Uptime: %llud %lluh %llum %llus\n", d, h%24, m%60, s%60);
#else
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        double up; fscanf(f, "%lf", &up); fclose(f);
        unsigned long long s=(unsigned long long)up,m=s/60,h=m/60,d=h/24;
        printf("  Uptime: %llud %lluh %llum %llus\n", d, h%24, m%60, s%60);
    }
#endif
}

static void cmd_sysinfo(const char *a) {
    (void)a;
    venom_box_label("System Info", COL_CYAN);
#ifdef VENOM_WINDOWS
    SYSTEM_INFO si; GetSystemInfo(&si);
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms); GlobalMemoryStatusEx(&ms);
    char host[256], user[256];
    DWORD hl=sizeof(host), ul=sizeof(user);
    GetComputerNameA(host,&hl); GetUserNameA(user,&ul);
    char tot[32], avail[32];
    human_size((long long)ms.ullTotalPhys, tot, sizeof(tot));
    human_size((long long)ms.ullAvailPhys, avail, sizeof(avail));
    printf("  Host     : %s\n", host);
    printf("  User     : %s\n", user);
    printf("  CPUs     : %lu\n", si.dwNumberOfProcessors);
    printf("  RAM      : %s total  /  %s free\n", tot, avail);
    printf("  RAM Used : "); draw_progress_bar((int)ms.dwMemoryLoad, 30, COL_CYAN); printf("\n");
#else
    struct utsname u; uname(&u);
    char host[256]; gethostname(host, sizeof(host));
    struct passwd *pw = getpwuid(getuid());
    printf("  OS       : %s %s\n", u.sysname, u.release);
    printf("  Arch     : %s\n", u.machine);
    printf("  Host     : %s\n", host);
    printf("  User     : %s\n", pw ? pw->pw_name : "?");
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    printf("  CPUs     : %ld\n", cpus);
    FILE *f = fopen("/proc/meminfo","r");
    if (f) {
        char line[256]; long total=0, avail=0;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line,"MemTotal:",9)==0)     sscanf(line+9, "%ld", &total);
            if (strncmp(line,"MemAvailable:",13)==0) sscanf(line+13, "%ld", &avail);
        }
        fclose(f);
        if (total>0) {
            char t[32], av[32];
            human_size((long long)total*1024, t, sizeof(t));
            human_size((long long)avail*1024, av, sizeof(av));
            printf("  RAM      : %s total  /  %s free\n", t, av);
            int pct = (int)(100.0*(total-avail)/total);
            printf("  RAM Used : "); draw_progress_bar(pct, 30, COL_CYAN); printf("\n");
        }
    }
#endif
    cmd_uptime(NULL);
}

static void cmd_env(const char *a)    { (void)a; system(
#ifdef VENOM_WINDOWS
"set"
#else
"env | sort"
#endif
); }

static void cmd_storage(const char *a) {
    (void)a;
    venom_box_label("Storage", COL_CYAN);
#ifdef VENOM_WINDOWS
    DWORD drives = GetLogicalDrives();
    char letter[4] = "A:\\";
    for (int i = 0; i < 26; i++) {
        if (!(drives & (1<<i))) continue;
        letter[0] = 'A'+i;
        ULARGE_INTEGER fr, tot, tfr;
        if (!GetDiskFreeSpaceExA(letter, &fr, &tot, &tfr)) continue;
        char ts[32], fs[32];
        human_size((long long)tot.QuadPart, ts, sizeof(ts));
        human_size((long long)fr.QuadPart,  fs, sizeof(fs));
        int pct = (tot.QuadPart>0)
            ? (int)(100.0*(tot.QuadPart-fr.QuadPart)/tot.QuadPart) : 0;
        printf("  %s  ", letter);
        draw_progress_bar(pct, 20, pct>85?COL_RED:COL_GREEN);
        printf("  %s / %s\n", fs, ts);
    }
#else
    system("df -h --output=target,size,used,avail,pcent 2>/dev/null || df -h");
#endif
}

static void cmd_processes(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    system("tasklist /fo table");
#else
    system("ps aux --sort=-%cpu | head -30");
#endif
}

static void cmd_kill_proc(const char *a) {
    if (!a||!*a) { printf("Usage: kill <pid|name>\n"); return; }
    char t[256]; strncpy(t,a,255); str_trim(t);
    int is_num=1;
    for (int i=0;t[i];i++) if(!isdigit((unsigned char)t[i])){is_num=0;break;}
    char cmd[512];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd), is_num?"taskkill /PID %s /F":"taskkill /IM \"%s\" /F", t);
#else
    snprintf(cmd,sizeof(cmd), is_num?"kill -9 %s":"pkill -9 \"%s\"", t);
#endif
    system(cmd);
}

static void cmd_isadmin(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    BOOL adm=FALSE; HANDLE tok=NULL;
    if (OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&tok)){
        TOKEN_ELEVATION e; DWORD sz=sizeof(e);
        if(GetTokenInformation(tok,TokenElevation,&e,sizeof(e),&sz)) adm=(BOOL)e.TokenIsElevated;
        CloseHandle(tok);
    }
    vprint(adm?COL_GREEN:COL_YELLOW, "  Admin: %s\n", adm?"YES":"NO");
#else
    vprint(getuid()==0?COL_GREEN:COL_YELLOW,
           "  Root: %s  (UID=%d)\n", getuid()==0?"YES":"NO", (int)getuid());
#endif
}

static void cmd_battery(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    SYSTEM_POWER_STATUS ps;
    if (GetSystemPowerStatus(&ps)) {
        printf("  Battery : ");
        if (ps.BatteryLifePercent==255) printf("N/A (no battery)\n");
        else {
            draw_progress_bar(ps.BatteryLifePercent, 30,
                ps.BatteryLifePercent<20?COL_RED:COL_GREEN);
            printf("  %s\n", ps.ACLineStatus==1?"[Charging]":"[On battery]");
        }
    }
#elif defined(__APPLE__)
    system("pmset -g batt");
#else
    int r=system("cat /sys/class/power_supply/BAT*/capacity 2>/dev/null"
                 "| awk '{print \"  Battery: \" $1 \"%\"}'"); (void)r;
#endif
}

static void cmd_shutdown(const char *a) {
    (void)a; char c[8]; printf("Shutdown? (type YES): "); fgets(c,sizeof(c),stdin); str_trim(c);
    if(strcmp(c,"YES")!=0){printf("Aborted.\n");return;}
#ifdef VENOM_WINDOWS
    system("shutdown /s /t 0");
#else
    system("shutdown -h now");
#endif
}

static void cmd_restart(const char *a) {
    (void)a; char c[8]; printf("Restart? (type YES): "); fgets(c,sizeof(c),stdin); str_trim(c);
    if(strcmp(c,"YES")!=0){printf("Aborted.\n");return;}
#ifdef VENOM_WINDOWS
    system("shutdown /r /t 0");
#else
    system("shutdown -r now");
#endif
}

#ifdef VENOM_WINDOWS
static void cmd_lock(const char *a)     { (void)a; LockWorkStation(); }
static void cmd_logoff(const char *a)   { (void)a; ExitWindowsEx(EWX_LOGOFF,0); }
static void cmd_hibernate(const char *a){ (void)a; system("shutdown /h"); }
static void cmd_suspend(const char *a)  { (void)a; system("rundll32.exe powrprof.dll,SetSuspendState 0,1,0"); }
#else
static void cmd_lock(const char *a)     { (void)a; int r=system("loginctl lock-session 2>/dev/null||xdg-screensaver lock 2>/dev/null"); (void)r; }
static void cmd_logoff(const char *a)   { (void)a; int r=system("pkill -KILL -u $USER 2>/dev/null"); (void)r; }
static void cmd_hibernate(const char *a){ (void)a; int r=system("systemctl hibernate"); (void)r; }
static void cmd_suspend(const char *a)  { (void)a; int r=system("systemctl suspend"); (void)r; }
#endif

/* ================================================================
   FILE COMMANDS — UPGRADED
   ================================================================ */
static void cmd_ls(const char *a) {
    const char *path = (a&&*a)?a:".";
#ifdef VENOM_WINDOWS
    if (g_cfg.ls_colors) {
        /* Colored ls via PowerShell */
        char cmd[MAX_PATH_LEN+256];
        snprintf(cmd,sizeof(cmd),
            "powershell -command \"Get-ChildItem '%s' | "
            "ForEach-Object { if($_.PSIsContainer){'[DIR]  '+$_.Name} else{$_.Name} }\"",
            path);
        system(cmd);
    } else {
        char cmd[MAX_PATH_LEN+32];
        snprintf(cmd,sizeof(cmd),"dir \"%s\"",path);
        system(cmd);
    }
#else
    char cmd[MAX_PATH_LEN+32];
    snprintf(cmd,sizeof(cmd),
             g_cfg.ls_colors?"ls -la --color=auto \"%s\"":"ls -la \"%s\"", path);
    system(cmd);
#endif
}

static void cmd_cd(const char *a) {
    if(!a||!*a){printf("Usage: cd <path>\n");return;}
    char p[MAX_PATH_LEN]; strncpy(p,a,sizeof(p)-1); str_trim(p);
#ifdef VENOM_WINDOWS
    if(!SetCurrentDirectoryA(p)) printf("cd: cannot change to '%s'\n",p);
    else { GetCurrentDirectoryA(sizeof(g_cwd),g_cwd); vprint(COL_CYAN,"  📁 %s\n",g_cwd); }
#else
    if(chdir(p)!=0) printf("cd: %s: %s\n",p,strerror(errno));
    else { getcwd(g_cwd,sizeof(g_cwd)); vprint(COL_CYAN,"  📁 %s\n",g_cwd); }
#endif
}

static void cmd_cat(const char *a) {
    if(!a||!*a){printf("Usage: cat <file>\n");return;}
    FILE *f=fopen(a,"r");
    if(!f){printf("cat: cannot open '%s'\n",a);return;}
    char buf[4096]; int ln=1;
    vprint(COL_DIM,"  ── %s ──\n",a);
    while(fgets(buf,sizeof(buf),f)){
        vprint(COL_DIM,"%4d │ ",ln++);
        printf("%s",buf);
    }
    fclose(f);
}

static void cmd_mkdir_cmd(const char *a) {
    if(!a||!*a){printf("Usage: mkdir <dir>\n");return;}
#ifdef VENOM_WINDOWS
    if(!CreateDirectoryA(a,NULL)) printf("mkdir: failed\n");
    else vprint(COL_GREEN,"  Created: %s\n",a);
#else
    if(mkdir(a,0755)!=0) printf("mkdir: %s\n",strerror(errno));
    else vprint(COL_GREEN,"  Created: %s\n",a);
#endif
}

static void cmd_rmdir_cmd(const char *a) {
    if(!a||!*a){printf("Usage: rmdir <dir>\n");return;}
    char c[8]; printf("Remove '%s'? (type YES): ",a);
    fgets(c,sizeof(c),stdin); str_trim(c);
    if(strcmp(c,"YES")!=0){printf("Aborted.\n");return;}
#ifdef VENOM_WINDOWS
    char cmd[MAX_PATH_LEN+32]; snprintf(cmd,sizeof(cmd),"rd /s /q \"%s\"",a); system(cmd);
#else
    char cmd[MAX_PATH_LEN+32]; snprintf(cmd,sizeof(cmd),"rm -rf \"%s\"",a); system(cmd);
#endif
}

static void cmd_copy(const char *a) {
    char s[MAX_PATH_LEN],d[MAX_PATH_LEN];
    if(!a||sscanf(a,"%2047s %2047s",s,d)<2){
        printf("Source: ");fgets(s,sizeof(s),stdin);str_trim(s);
        printf("Dest  : ");fgets(d,sizeof(d),stdin);str_trim(d);
    }
    char cmd[MAX_PATH_LEN*2+32];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd),"copy /Y \"%s\" \"%s\"",s,d);
#else
    snprintf(cmd,sizeof(cmd),"cp -r \"%s\" \"%s\"",s,d);
#endif
    system(cmd);
}

static void cmd_move(const char *a) {
    char s[MAX_PATH_LEN],d[MAX_PATH_LEN];
    if(!a||sscanf(a,"%2047s %2047s",s,d)<2){
        printf("Source: ");fgets(s,sizeof(s),stdin);str_trim(s);
        printf("Dest  : ");fgets(d,sizeof(d),stdin);str_trim(d);
    }
    char cmd[MAX_PATH_LEN*2+32];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd),"move /Y \"%s\" \"%s\"",s,d);
#else
    snprintf(cmd,sizeof(cmd),"mv \"%s\" \"%s\"",s,d);
#endif
    system(cmd);
}

static void cmd_delete(const char *a) {
    if(!a||!*a){printf("Usage: del <file>\n");return;}
    char c[8]; printf("Delete '%s'? (type YES): ",a);
    fgets(c,sizeof(c),stdin); str_trim(c);
    if(strcmp(c,"YES")!=0){printf("Aborted.\n");return;}
    if(remove(a)!=0) printf("del: %s\n",strerror(errno));
    else vprint(COL_GREEN,"  Deleted: %s\n",a);
}

static void cmd_touch(const char *a) {
    if(!a||!*a){printf("Usage: touch <file>\n");return;}
    FILE *f=fopen(a,"ab");
    if(f){fclose(f);vprint(COL_GREEN,"  Touched: %s\n",a);}
    else printf("touch: %s\n",strerror(errno));
}

static void cmd_filesize(const char *a) {
    if(!a||!*a){printf("Usage: filesize <file>\n");return;}
    FILE *f=fopen(a,"rb");
    if(!f){printf("filesize: cannot open '%s'\n",a);return;}
    fseek(f,0,SEEK_END); long long sz=ftell(f); fclose(f);
    char buf[32]; human_size(sz,buf,sizeof(buf));
    printf("  %s  →  %s\n",a,buf);
}

static void cmd_head(const char *a) {
    char p[MAX_PATH_LEN]; int n=10;
    if(!a||!*a){printf("Usage: head <file> [N]\n");return;}
    sscanf(a,"%2047s %d",p,&n);
    FILE *f=fopen(p,"r");
    if(!f){printf("head: cannot open '%s'\n",p);return;}
    char line[4096]; int i=0;
    vprint(COL_DIM,"  ── first %d lines of %s ──\n",n,p);
    while(i<n&&fgets(line,sizeof(line),f)){
        vprint(COL_DIM,"%4d │ ",i+1); printf("%s",line); i++;
    }
    fclose(f);
}

static void cmd_tail(const char *a) {
    char p[MAX_PATH_LEN]; int n=10;
    if(!a||!*a){printf("Usage: tail <file> [N]\n");return;}
    sscanf(a,"%2047s %d",p,&n);
    FILE *f=fopen(p,"r");
    if(!f){printf("tail: cannot open '%s'\n",p);return;}
    char **lines=(char**)malloc(n*sizeof(char*));
    for(int i=0;i<n;i++) lines[i]=NULL;
    char buf[4096]; int idx=0,count=0;
    while(fgets(buf,sizeof(buf),f)){
        if(lines[idx]) free(lines[idx]);
        lines[idx]=strdup(buf);
        idx=(idx+1)%n; count++;
    }
    fclose(f);
    int start=(count<n)?0:idx;
    vprint(COL_DIM,"  ── last %d lines of %s ──\n",n,p);
    for(int i=0;i<(count<n?count:n);i++){
        int j=(start+i)%n;
        if(lines[j]){vprint(COL_DIM,"%4d │ ",count-(count<n?count:n)+i+1);printf("%s",lines[j]);}
    }
    for(int i=0;i<n;i++) if(lines[i]) free(lines[i]);
    free(lines);
}

static void cmd_find(const char *a) {
    if(!a||!*a){printf("Usage: find <pattern> [path]\n");return;}
    char pat[256],sp[MAX_PATH_LEN]; strcpy(sp,".");
    sscanf(a,"%255s %2047s",pat,sp);
#ifdef VENOM_WINDOWS
    char cmd[MAX_PATH_LEN+256]; snprintf(cmd,sizeof(cmd),"dir /s /b \"%s\\%s\" 2>nul",sp,pat); system(cmd);
#else
    char cmd[MAX_PATH_LEN+256]; snprintf(cmd,sizeof(cmd),"find \"%s\" -name \"%s\" 2>/dev/null",sp,pat); system(cmd);
#endif
}

/* Box-drawing tree */
static void print_tree_node(const char *path, const char *prefix, int depth) {
    if(depth>6) return;
#ifdef VENOM_POSIX
    DIR *d=opendir(path);
    if(!d) return;
    struct dirent *e;
    /* collect entries */
    char **entries=NULL; int cnt=0;
    while((e=readdir(d))!=NULL){
        if(e->d_name[0]=='.') continue;
        entries=(char**)realloc(entries,(cnt+1)*sizeof(char*));
        entries[cnt++]=strdup(e->d_name);
    }
    closedir(d);
    for(int i=0;i<cnt;i++){
        int last=(i==cnt-1);
        char full[MAX_PATH_LEN];
        snprintf(full,sizeof(full),"%s/%s",path,entries[i]);
        struct stat st; stat(full,&st);
        int is_dir=S_ISDIR(st.st_mode);
        venom_set_color(is_dir?COL_BLUE:COL_DEFAULT);
        printf("%s%s%s%s\n",prefix,last?"└── ":"├── ",
               entries[i], is_dir?"/":"");
        venom_reset_color();
        if(is_dir){
            char np[MAX_PATH_LEN];
            snprintf(np,sizeof(np),"%s%s",prefix,last?"    ":"│   ");
            print_tree_node(full,np,depth+1);
        }
        free(entries[i]);
    }
    free(entries);
#else
    char cmd[MAX_PATH_LEN+32];
    snprintf(cmd,sizeof(cmd),"tree \"%s\"",path); system(cmd);
    (void)prefix; (void)depth;
#endif
}

static void cmd_tree(const char *a) {
    const char *path=(a&&*a)?a:".";
    vprint(COL_CYAN,"  %s\n",path);
    if(strcmp(g_cfg.tree_style,"box")==0)
        print_tree_node(path,"",0);
    else {
        char cmd[MAX_PATH_LEN+32];
#ifdef VENOM_WINDOWS
        snprintf(cmd,sizeof(cmd),"tree \"%s\"",path);
#else
        snprintf(cmd,sizeof(cmd),"find \"%s\" | sort | sed 's|[^/]*/|  |g'",path);
#endif
        system(cmd);
    }
}

static void cmd_wc(const char *a) {
    if(!a||!*a){printf("Usage: wc <file>\n");return;}
    FILE *f=fopen(a,"r");
    if(!f){
        /* inline text */
        size_t ch=strlen(a); int w=0,iw=0;
        for(size_t i=0;i<ch;i++){
            if(isspace((unsigned char)a[i])) iw=0;
            else if(!iw){iw=1;w++;}
        }
        venom_box_label("Word Count",COL_CYAN);
        printf("  %-10s %zu\n","Chars:",ch);
        printf("  %-10s %d\n","Words:",w);
        return;
    }
    long lines=0,words=0,bytes=0; int iw=0,c;
    while((c=fgetc(f))!=EOF){
        bytes++;
        if(c=='\n') lines++;
        if(isspace((unsigned char)c)) iw=0;
        else if(!iw){iw=1;words++;}
    }
    fclose(f);
    venom_box_label("Word Count",COL_CYAN);
    printf("  %-10s %ld\n","Lines:",lines);
    printf("  %-10s %ld\n","Words:",words);
    printf("  %-10s %ld\n","Bytes:",bytes);
    printf("  File     : %s\n",a);
}

static void cmd_compress(const char *a) {
    if(!a||!*a){printf("Usage: compress <path>\n");return;}
    char out[MAX_PATH_LEN]; snprintf(out,sizeof(out),"%s.zip",a);
#ifdef VENOM_WINDOWS
    char cmd[MAX_PATH_LEN*2+128];
    snprintf(cmd,sizeof(cmd),"powershell -command \"Compress-Archive -Path '%s' -DestinationPath '%s' -Force\"",a,out);
    system(cmd);
#else
    char cmd[MAX_PATH_LEN*2+32];
    snprintf(cmd,sizeof(cmd),"zip -r \"%s\" \"%s\"",out,a); system(cmd);
#endif
    vprint(COL_GREEN,"  Created: %s\n",out);
}

static void cmd_extract(const char *a) {
    if(!a||!*a){printf("Usage: extract <file.zip> [dest]\n");return;}
    char zf[MAX_PATH_LEN],dest[MAX_PATH_LEN]; strcpy(dest,".");
    sscanf(a,"%2047s %2047s",zf,dest);
#ifdef VENOM_WINDOWS
    char cmd[MAX_PATH_LEN*2+128];
    snprintf(cmd,sizeof(cmd),"powershell -command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\"",zf,dest);
    system(cmd);
#else
    char cmd[MAX_PATH_LEN*2+32];
    snprintf(cmd,sizeof(cmd),"unzip \"%s\" -d \"%s\"",zf,dest); system(cmd);
#endif
}

/* ================================================================
   HASHING
   ================================================================ */
static void cmd_md5(const char *a) {
    if(!a||!*a){printf("Usage: md5 <file>\n");return;}
    char cmd[MAX_PATH_LEN+64];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd),"certutil -hashfile \"%s\" MD5",a);
#elif defined(__APPLE__)
    snprintf(cmd,sizeof(cmd),"md5 \"%s\"",a);
#else
    snprintf(cmd,sizeof(cmd),"md5sum \"%s\"",a);
#endif
    system(cmd);
}

static void cmd_sha256(const char *a) {
    if(!a||!*a){printf("Usage: sha256 <file>\n");return;}
    char cmd[MAX_PATH_LEN+64];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd),"certutil -hashfile \"%s\" SHA256",a);
#elif defined(__APPLE__)
    snprintf(cmd,sizeof(cmd),"shasum -a 256 \"%s\"",a);
#else
    snprintf(cmd,sizeof(cmd),"sha256sum \"%s\"",a);
#endif
    system(cmd);
}

static void cmd_sha512(const char *a) {
    if(!a||!*a){printf("Usage: sha512 <file>\n");return;}
    char cmd[MAX_PATH_LEN+64];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd),"certutil -hashfile \"%s\" SHA512",a);
#elif defined(__APPLE__)
    snprintf(cmd,sizeof(cmd),"shasum -a 512 \"%s\"",a);
#else
    snprintf(cmd,sizeof(cmd),"sha512sum \"%s\"",a);
#endif
    system(cmd);
}

/* ================================================================
   NETWORK — CORE
   ================================================================ */
static void cmd_ping(const char *a) {
    if(!a||!*a){printf("Usage: ping <host> [count]\n");return;}
    char cmd[512];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd),"ping %s",a);
#else
    snprintf(cmd,sizeof(cmd),"ping -c 4 %s",a);
#endif
    system(cmd);
}

static void cmd_tracert(const char *a) {
    if(!a||!*a){printf("Usage: tracert <host>\n");return;}
    char cmd[512];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd),"tracert %s",a);
#else
    snprintf(cmd,sizeof(cmd),"traceroute %s 2>/dev/null||tracepath %s",a,a);
#endif
    system(cmd);
}

static void cmd_ns(const char *a) {
    if(!a||!*a){printf("Usage: ns <host>\n");return;}
    char cmd[512]; snprintf(cmd,sizeof(cmd),"nslookup %s",a); system(cmd);
}

static void cmd_netstat(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    system("netstat -ano");
#else
    int r=system("ss -tulnp 2>/dev/null||netstat -tulnp"); (void)r;
#endif
}

static void cmd_arp(const char *a) { (void)a; system("arp -a"); }

static void cmd_hostname(const char *a) {
    (void)a; char h[256]; gethostname(h,sizeof(h)); printf("  %s\n",h);
}

static void cmd_dnsflush(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    system("ipconfig /flushdns");
#elif defined(__APPLE__)
    system("dscacheutil -flushcache && killall -HUP mDNSResponder");
#else
    int r=system("systemd-resolve --flush-caches 2>/dev/null||service nscd restart 2>/dev/null"); (void)r;
#endif
}

static void cmd_myip(const char *a) {
    (void)a;
    venom_box_label("My IP", COL_CYAN);
    char host[256]; gethostname(host,sizeof(host));
    struct hostent *h=gethostbyname(host);
    if(h){
        for(int i=0;h->h_addr_list[i];i++){
            struct in_addr addr; memcpy(&addr,h->h_addr_list[i],sizeof(addr));
            printf("  Local  : %s\n",inet_ntoa(addr));
        }
    }
    /* Public IP */
    char out[256]="";
    if(http_get("api.ipify.org",80,"/?format=text",out,sizeof(out))>0)
        printf("  Public : %s\n",out);
}

static void cmd_get_ip(const char *a) {
    if(!a||!*a){printf("Usage: get-ip <host>\n");return;}
    struct addrinfo hints={0},*res;
    hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(a,NULL,&hints,&res)!=0){printf("Resolve failed\n");return;}
    for(struct addrinfo *p=res;p;p=p->ai_next){
        char ip[INET6_ADDRSTRLEN]; void *addr;
        if(p->ai_family==AF_INET) addr=&((struct sockaddr_in*)p->ai_addr)->sin_addr;
        else addr=&((struct sockaddr_in6*)p->ai_addr)->sin6_addr;
        inet_ntop(p->ai_family,addr,ip,sizeof(ip));
        printf("  %s  →  %s\n",a,ip);
    }
    freeaddrinfo(res);
}

static void cmd_wifi(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    system("netsh wlan show interfaces");
#elif defined(__APPLE__)
    system("/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -I");
#else
    int r=system("nmcli device wifi 2>/dev/null||iwconfig 2>/dev/null||iw dev"); (void)r;
#endif
}

static void cmd_netinfo(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    system("ipconfig /all"); system("arp -a");
#else
    int r; r=system("ip addr 2>/dev/null||ifconfig");
    r=system("ip route 2>/dev/null||route -n 2>/dev/null");
    r=system("arp -a 2>/dev/null"); (void)r;
#endif
}

/* ================================================================
   NETWORK — ADVANCED PORT & IP TOOLS
   ================================================================ */
static void cmd_portscan(const char *a) {
    if(!a||!*a){printf("Usage: portscan <host> [port,port,...]\n");return;}
    char host[256]; sscanf(a,"%255s",host);
    int ports[]={21,22,23,25,53,80,110,135,139,143,443,445,
                 587,993,995,1433,3306,3389,5432,5900,6379,8080,8443,8888};
    int np=(int)(sizeof(ports)/sizeof(ports[0]));
    venom_box_label("Port Scan",COL_CYAN);
    printf("  Target: %s\n\n",host);
    int open=0;
    for(int i=0;i<np;i++){
        venom_sock_t s; struct sockaddr_in sa={0};
        sa.sin_family=AF_INET; sa.sin_port=htons((unsigned short)ports[i]);
        struct hostent *he=gethostbyname(host);
        if(!he) continue;
        memcpy(&sa.sin_addr,he->h_addr,he->h_length);
        s=socket(AF_INET,SOCK_STREAM,0);
#ifdef VENOM_WINDOWS
        DWORD tv=1500; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
        setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(char*)&tv,sizeof(tv));
#else
        struct timeval tv={1,500000};
        setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
        setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
#endif
        int ok=(connect(s,(struct sockaddr*)&sa,sizeof(sa))==0);
        venom_closesocket(s);
        if(ok){ vprint(COL_GREEN,"  [OPEN]   port %-6d\n",ports[i]); open++; }
        else    vprint(COL_DIM,  "  [closed] port %d\n",ports[i]);
    }
    printf("\n  Open ports found: %d\n",open);
}

static void cmd_tcping(const char *a) {
    if(!a||!*a){printf("Usage: tcping <host> <port> [count]\n");return;}
    char host[256]; int port=80,count=4;
    sscanf(a,"%255s %d %d",host,&port,&count);
    venom_box_label("TCP Ping",COL_CYAN);
    printf("  %s:%d  x%d\n\n",host,port,count);
    for(int i=0;i<count;i++){
        venom_sock_t s; struct sockaddr_in sa={0};
        sa.sin_family=AF_INET; sa.sin_port=htons((unsigned short)port);
        struct hostent *he=gethostbyname(host);
        if(!he){printf("  Resolve failed\n");return;}
        memcpy(&sa.sin_addr,he->h_addr,he->h_length);
        s=socket(AF_INET,SOCK_STREAM,0);
        clock_t t0=clock();
        int ok=(connect(s,(struct sockaddr*)&sa,sizeof(sa))==0);
        double ms=(double)(clock()-t0)/CLOCKS_PER_SEC*1000.0;
        venom_closesocket(s);
        if(ok) vprint(COL_GREEN,"  [%d] Connected  %.1fms\n",i+1,ms);
        else   vprint(COL_RED,  "  [%d] Failed\n",i+1);
        venom_sleep_ms(500);
    }
}

static void cmd_whois(const char *a) {
    if(!a||!*a){printf("Usage: whois <domain>\n");return;}
    venom_sock_t s; struct sockaddr_in sa={0};
    sa.sin_family=AF_INET; sa.sin_port=htons(43);
    struct hostent *he=gethostbyname("whois.iana.org");
    if(!he){printf("Cannot reach WHOIS server\n");return;}
    memcpy(&sa.sin_addr,he->h_addr,he->h_length);
    s=socket(AF_INET,SOCK_STREAM,0);
    if(connect(s,(struct sockaddr*)&sa,sizeof(sa))!=0){
        printf("WHOIS connect failed\n");venom_closesocket(s);return;
    }
    char req[280]; snprintf(req,sizeof(req),"%s\r\n",a);
    send(s,req,(int)strlen(req),0);
    char buf[4096]; int n;
    while((n=recv(s,buf,sizeof(buf)-1,0))>0){buf[n]='\0';printf("%s",buf);}
    venom_closesocket(s);
}

static void cmd_geoip(const char *a) {
    char path[256];
    if(a&&*a) snprintf(path,sizeof(path),"/%s/json",a);
    else strcpy(path,"/json");
    char out[4096]="";
    if(http_get("ipapi.co",80,path,out,sizeof(out))>0){
        venom_box_label("GeoIP",COL_CYAN);
        printf("%s\n",out);
    } else printf("geoip: request failed\n");
}

/* --- New IP tools --- */
static void cmd_ipcalc(const char *a) {
    if(!a||!*a){printf("Usage: ipcalc <ip> <mask>  e.g. ipcalc 192.168.1.0 24\n");return;}
    char ip[64]; int prefix=24;
    sscanf(a,"%63s %d",ip,&prefix);
    struct in_addr addr; inet_aton(ip,&addr);
    unsigned long host=ntohl(addr.s_addr);
    unsigned long mask=(prefix==0)?0:( ~0UL<<(32-prefix));
    unsigned long net=host&mask;
    unsigned long bc=net|(~mask);
    unsigned long first=net+1, last=bc-1;
    unsigned long count=(prefix>=31)?(unsigned long)(1<<(32-prefix)):last-first+1;
    struct in_addr tmp;
    venom_box_label("IP Calculator",COL_CYAN);
    tmp.s_addr=htonl(net);  printf("  Network   : %s/%d\n",inet_ntoa(tmp),prefix);
    tmp.s_addr=htonl(mask); printf("  Mask      : %s\n",inet_ntoa(tmp));
    tmp.s_addr=htonl(bc);   printf("  Broadcast : %s\n",inet_ntoa(tmp));
    tmp.s_addr=htonl(first);printf("  First Host: %s\n",inet_ntoa(tmp));
    tmp.s_addr=htonl(last); printf("  Last Host : %s\n",inet_ntoa(tmp));
    printf("  Hosts     : %lu\n",count);
}

static void cmd_rdns(const char *a) {
    if(!a||!*a){printf("Usage: rdns <ip>\n");return;}
    struct in_addr addr;
    if(inet_aton(a,&addr)==0){printf("Invalid IP\n");return;}
    struct hostent *h=gethostbyaddr(&addr,sizeof(addr),AF_INET);
    if(h) printf("  %s  →  %s\n",a,h->h_name);
    else  printf("  No reverse DNS for %s\n",a);
}

static void cmd_portinfo(const char *a) {
    if(!a||!*a){printf("Usage: portinfo <port>\n");return;}
    int port=atoi(a);
    struct servent *se=getservbyport(htons((unsigned short)port),NULL);
    venom_box_label("Port Info",COL_CYAN);
    printf("  Port   : %d\n",port);
    printf("  Service: %s\n",se?se->s_name:"unknown");
    /* Well-known descriptions */
    typedef struct{int p;const char*d;}PortDesc;
    PortDesc pd[]={
        {21,"FTP"},{22,"SSH"},{23,"Telnet"},{25,"SMTP"},{53,"DNS"},
        {80,"HTTP"},{110,"POP3"},{143,"IMAP"},{443,"HTTPS"},
        {445,"SMB"},{3306,"MySQL"},{3389,"RDP"},{5432,"PostgreSQL"},
        {6379,"Redis"},{8080,"HTTP-Alt"},{8443,"HTTPS-Alt"},{0,NULL}
    };
    for(int i=0;pd[i].d;i++)
        if(pd[i].p==port){ printf("  Known  : %s\n",pd[i].d); break; }
}

static void cmd_localports(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    system("netstat -ano | findstr LISTENING");
#else
    int r=system("ss -tlnp 2>/dev/null||netstat -tlnp 2>/dev/null"); (void)r;
#endif
}

static void cmd_dnscheck(const char *a) {
    if(!a||!*a){printf("Usage: dnscheck <domain>\n");return;}
    venom_box_label("DNS Records",COL_CYAN);
    printf("  Domain: %s\n\n",a);
    char cmd[512];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd),"nslookup -type=ANY %s",a); system(cmd);
#else
    snprintf(cmd,sizeof(cmd),"dig ANY %s +noall +answer 2>/dev/null||nslookup -type=ANY %s",a,a);
    system(cmd);
#endif
}

static void cmd_interfaces(const char *a) {
    (void)a;
    venom_box_label("Network Interfaces",COL_CYAN);
#ifdef VENOM_WINDOWS
    system("ipconfig /all");
#else
    int r=system("ip -brief addr 2>/dev/null||ifconfig"); (void)r;
#endif
}

static void cmd_routetable(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    system("route print");
#else
    int r=system("ip route 2>/dev/null||route -n"); (void)r;
#endif
}

static void cmd_banner(const char *a) {
    if(!a||!*a){printf("Usage: banner <host> <port>\n");return;}
    char host[256]; int port=80;
    sscanf(a,"%255s %d",host,&port);
    venom_sock_t s; struct sockaddr_in sa={0};
    sa.sin_family=AF_INET; sa.sin_port=htons((unsigned short)port);
    struct hostent *he=gethostbyname(host);
    if(!he){printf("Resolve failed\n");return;}
    memcpy(&sa.sin_addr,he->h_addr,he->h_length);
    s=socket(AF_INET,SOCK_STREAM,0);
#ifdef VENOM_WINDOWS
    DWORD tv=5000; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
#else
    struct timeval tv={5,0}; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
#endif
    if(connect(s,(struct sockaddr*)&sa,sizeof(sa))!=0){
        printf("  Connect failed\n"); venom_closesocket(s); return;
    }
    /* Send minimal HTTP request to get banner */
    const char *req="HEAD / HTTP/1.0\r\n\r\n";
    send(s,req,(int)strlen(req),0);
    char buf[1024]; int n=recv(s,buf,sizeof(buf)-1,0);
    venom_closesocket(s);
    if(n>0){buf[n]='\0';venom_box_label("Service Banner",COL_CYAN);printf("%s\n",buf);}
    else printf("  No banner received\n");
}

static void cmd_reachable(const char *a) {
    if(!a||!*a){printf("Usage: reachable <host>\n");return;}
    char cmd[256];
#ifdef VENOM_WINDOWS
    snprintf(cmd,sizeof(cmd),"ping -n 1 -w 2000 %s >nul 2>&1",a);
#else
    snprintf(cmd,sizeof(cmd),"ping -c 1 -W 2 %s >/dev/null 2>&1",a);
#endif
    int r=system(cmd);
    if(r==0) vprint(COL_GREEN,"  ✓ %s is reachable\n",a);
    else     vprint(COL_RED,  "  ✗ %s is NOT reachable\n",a);
}

static void cmd_netmon(const char *a) {
    (void)a;
    vprint(COL_CYAN,"  Network monitor (5s sample, Ctrl+C to stop)\n\n");
#ifdef VENOM_WINDOWS
    system("netstat -e 1");
#else
    int r=system("ifstat 1 5 2>/dev/null||sar -n DEV 1 5 2>/dev/null||"
                 "cat /proc/net/dev"); (void)r;
#endif
}

static void cmd_blocksite(const char *a) {
    if(!a||!*a){printf("Usage: blocksite <domain>\n");return;}
    char d[256]; strncpy(d,a,255);
    char *p=strstr(d,"://"); if(p) memmove(d,p+3,strlen(p+3)+1);
    p=strchr(d,'/'); if(p) *p='\0';
    char c[8]; printf("Block '%s'? (type YES): ",d);
    fgets(c,sizeof(c),stdin); str_trim(c);
    if(strcmp(c,"YES")!=0){printf("Aborted.\n");return;}
    FILE *f=fopen(HOSTS_PATH,"a");
    if(!f){printf("blocksite: need admin/root\n");return;}
    fprintf(f,"0.0.0.0\t%s\n",d);
    if(strncmp(d,"www.",4)!=0) fprintf(f,"0.0.0.0\twww.%s\n",d);
    fclose(f);
    vprint(COL_GREEN,"  Blocked: %s\n",d);
    cmd_dnsflush(NULL);
}

static void cmd_unblocksite(const char *a) {
    if(!a||!*a){printf("Usage: unblocksite <domain>\n");return;}
    char d[256]; strncpy(d,a,255);
    char *p=strstr(d,"://"); if(p) memmove(d,p+3,strlen(p+3)+1);
    p=strchr(d,'/'); if(p)*p='\0';
    FILE *f=fopen(HOSTS_PATH,"r");
    if(!f){printf("unblocksite: cannot read hosts\n");return;}
    char tmp[MAX_PATH_LEN]; snprintf(tmp,sizeof(tmp),"%s.vbak",HOSTS_PATH);
    FILE *fo=fopen(tmp,"w"); if(!fo){fclose(f);return;}
    char line[1024]; int rm=0;
    while(fgets(line,sizeof(line),f)){
        if(strstr(line,d)){rm++;continue;}
        fputs(line,fo);
    }
    fclose(f);fclose(fo);
#ifdef VENOM_WINDOWS
    CopyFileA(tmp,HOSTS_PATH,FALSE); DeleteFileA(tmp);
#else
    rename(tmp,HOSTS_PATH);
#endif
    vprint(COL_GREEN,"  Removed %d entries for '%s'\n",rm,d);
    cmd_dnsflush(NULL);
}

/* ================================================================
   LOCAL HTTP SERVER
   ================================================================ */
static const char *mime_type(const char *path) {
    const char *ext = strrchr(path,'.');
    if(!ext) return "application/octet-stream";
    if(!strcmp(ext,".html")||!strcmp(ext,".htm")) return "text/html";
    if(!strcmp(ext,".css"))  return "text/css";
    if(!strcmp(ext,".js"))   return "application/javascript";
    if(!strcmp(ext,".json")) return "application/json";
    if(!strcmp(ext,".png"))  return "image/png";
    if(!strcmp(ext,".jpg")||!strcmp(ext,".jpeg")) return "image/jpeg";
    if(!strcmp(ext,".gif"))  return "image/gif";
    if(!strcmp(ext,".svg"))  return "image/svg+xml";
    if(!strcmp(ext,".ico"))  return "image/x-icon";
    if(!strcmp(ext,".txt"))  return "text/plain";
    if(!strcmp(ext,".pdf"))  return "application/pdf";
    return "application/octet-stream";
}

static void serve_client(venom_sock_t client, const char *root) {
    char req[4096]={0}; int n;
    n=recv(client,req,sizeof(req)-1,0);
    if(n<=0){venom_closesocket(client);return;}
    req[n]='\0';
    /* Parse method and path */
    char method[8],path[1024];
    sscanf(req,"%7s %1023s",method,path);
    /* Decode URL (basic) */
    char decoded[1024]={0}; int di=0;
    for(int i=0;path[i]&&di<1023;i++){
        if(path[i]=='%'&&path[i+1]&&path[i+2]){
            char h[3]={path[i+1],path[i+2],0};
            decoded[di++]=(char)strtol(h,NULL,16); i+=2;
        } else if(path[i]=='?'){ break; }
        else decoded[di++]=path[i];
    }
    decoded[di]='\0';
    /* Build file path */
    char fpath[MAX_PATH_LEN];
    snprintf(fpath,sizeof(fpath),"%s%s",root,decoded);
    /* Default to index.html */
    if(fpath[strlen(fpath)-1]==PATH_SEP||!strcmp(decoded,"/")){
        strncat(fpath,"index.html",sizeof(fpath)-strlen(fpath)-1);
    }
    time_t now=time(NULL);
    struct tm *t=localtime(&now);
    char ts[32]; strftime(ts,sizeof(ts),"%H:%M:%S",t);
    FILE *f=fopen(fpath,"rb");
    if(!f){
        /* Directory listing or 404 */
        const char *body="<html><body><h1>404 Not Found</h1></body></html>";
        char hdr[512];
        snprintf(hdr,sizeof(hdr),
            "HTTP/1.0 404 Not Found\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %zu\r\n\r\n",(size_t)strlen(body));
        send(client,hdr,(int)strlen(hdr),0);
        send(client,body,(int)strlen(body),0);
        vprint(COL_RED,"  [%s] 404  %s\n",ts,decoded);
    } else {
        fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
        char hdr[512];
        snprintf(hdr,sizeof(hdr),
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n\r\n",
            mime_type(fpath),sz);
        send(client,hdr,(int)strlen(hdr),0);
        char buf[4096]; size_t nr;
        while((nr=fread(buf,1,sizeof(buf),f))>0)
            send(client,buf,(int)nr,0);
        fclose(f);
        char szs[32]; human_size(sz,szs,sizeof(szs));
        vprint(COL_GREEN,"  [%s] 200  %-40s %s\n",ts,decoded,szs);
    }
    venom_closesocket(client);
}

static void cmd_serve(const char *a) {
    int port=g_cfg.serve_port;
    char root[MAX_PATH_LEN]; strncpy(root,g_cfg.serve_dir,sizeof(root)-1);

    /* Parse args: serve [port] [dir] */
    if(a&&*a){
        char p1[MAX_PATH_LEN],p2[MAX_PATH_LEN];
        int cnt=sscanf(a,"%2047s %2047s",p1,p2);
        if(cnt>=1){
            /* First arg: port or dir */
            int maybe_port=atoi(p1);
            if(maybe_port>0) port=maybe_port;
            else strncpy(root,p1,sizeof(root)-1);
        }
        if(cnt>=2) strncpy(root,p2,sizeof(root)-1);
    }

    venom_box_label("Local Server",COL_GREEN);
    printf("  Root    : %s\n",root);
    printf("  Address : http://localhost:%d\n",port);
    printf("  Ctrl+C to stop\n\n");

    venom_sock_t srv=socket(AF_INET,SOCK_STREAM,0);
    if(srv==INVALID_VENOM_SOCK){printf("serve: socket error\n");return;}
    int opt=1;
    setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
    struct sockaddr_in sa={0};
    sa.sin_family=AF_INET; sa.sin_port=htons((unsigned short)port);
    sa.sin_addr.s_addr=INADDR_ANY;
    if(bind(srv,(struct sockaddr*)&sa,sizeof(sa))!=0){
        printf("serve: port %d in use\n",port); venom_closesocket(srv); return;
    }
    listen(srv,10);
    g_server_running=1; g_server_port=port;

    /* Save state */
    FILE *sf=fopen(SERVE_STATE_FILE,"w");
    if(sf){fprintf(sf,"%d\n%s\n",port,root);fclose(sf);}

    /* Accept loop */
    while(g_server_running){
#ifdef VENOM_WINDOWS
        DWORD tv=500; setsockopt(srv,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
#else
        struct timeval tv={0,500000};
        setsockopt(srv,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
#endif
        venom_sock_t cl=accept(srv,NULL,NULL);
        if(cl==INVALID_VENOM_SOCK) continue;
        serve_client(cl,root);
    }
    venom_closesocket(srv);
    remove(SERVE_STATE_FILE);
    g_server_running=0;
    printf("Server stopped.\n");
}

static void cmd_serve_restore(const char *a) {
    (void)a;
    FILE *f=fopen(SERVE_STATE_FILE,"r");
    if(!f){printf("No saved server state.\n");return;}
    char line[MAX_PATH_LEN]; int port=8080; char root[MAX_PATH_LEN]=".";
    if(fgets(line,sizeof(line),f)) port=atoi(line);
    if(fgets(line,sizeof(line),f)){str_trim(line);strncpy(root,line,sizeof(root)-1);}
    fclose(f);
    char args[MAX_PATH_LEN+32];
    snprintf(args,sizeof(args),"%d %s",port,root);
    cmd_serve(args);
}

/* ================================================================
   ENCODING UTILITIES — UPGRADED
   ================================================================ */
static void cmd_b64enc(const char *a) {
    if(!a||!*a){printf("Usage: b64enc <text>\n");return;}
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *in=(const unsigned char*)a;
    size_t len=strlen(a);
    char *out=(char*)malloc(((len+2)/3)*4+2);
    int oi=0;
    for(size_t i=0;i<len;i+=3){
        unsigned int b=in[i]<<16;
        if(i+1<len) b|=in[i+1]<<8;
        if(i+2<len) b|=in[i+2];
        out[oi++]=t[(b>>18)&0x3F];
        out[oi++]=t[(b>>12)&0x3F];
        out[oi++]=(i+1<len)?t[(b>>6)&0x3F]:'=';
        out[oi++]=(i+2<len)?t[b&0x3F]:'=';
    }
    out[oi]='\0';
    if(g_cfg.util_sidebyside)
        print_sidebyside("Input:",a,"Base64:",out);
    else {
        printf("  Input  : %s\n",a);
        vprint(COL_CYAN,"  Base64 : %s\n",out);
    }
    printf("  Bytes  : %zu → %d\n",len,oi);
    free(out);
}

static void cmd_b64dec(const char *a) {
    if(!a||!*a){printf("Usage: b64dec <base64>\n");return;}
    static const int tb[256]={
        ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
        ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
        ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
        ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
        ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
        ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
        ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
        ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63
    };
    size_t len=strlen(a);
    char *out=(char*)malloc(len+2); int oi=0;
    for(size_t i=0;i+3<len+1;i+=4){
        if(i+3>=len) break;
        int x=tb[(unsigned char)a[i]],y=tb[(unsigned char)a[i+1]];
        int z=(a[i+2]!='=')?tb[(unsigned char)a[i+2]]:0;
        int w=(a[i+3]!='=')?tb[(unsigned char)a[i+3]]:0;
        out[oi++]=(x<<2)|(y>>4);
        if(a[i+2]!='=') out[oi++]=((y&0xF)<<4)|(z>>2);
        if(a[i+3]!='=') out[oi++]=((z&0x3)<<6)|w;
    }
    out[oi]='\0';
    if(g_cfg.util_sidebyside)
        print_sidebyside("Base64:",a,"Decoded:",out);
    else {
        printf("  Base64 : %s\n",a);
        vprint(COL_CYAN,"  Decoded: %s\n",out);
    }
    free(out);
}

static void cmd_hexenc(const char *a) {
    if(!a||!*a){printf("Usage: hexenc <text>\n");return;}
    printf("  Input : %s\n  Hex   : ",a);
    int i=0;
    for(const unsigned char *p=(const unsigned char*)a;*p;p++,i++){
        if(i&&i%8==0) printf(" ");
        vprint(COL_CYAN,"%02x",*p);
    }
    printf("\n  Bytes : %zu\n",strlen(a));
}

static void cmd_hexdec(const char *a) {
    if(!a||!*a){printf("Usage: hexdec <hexstring>\n");return;}
    /* strip spaces */
    char clean[VENOM_MAX_INPUT]; int ci=0;
    for(const char *p=a;*p;p++) if(!isspace((unsigned char)*p)) clean[ci++]=*p;
    clean[ci]='\0';
    char *out=(char*)malloc(ci/2+2); int oi=0;
    for(int i=0;i+1<ci;i+=2){
        char b[3]={clean[i],clean[i+1],0};
        out[oi++]=(char)strtol(b,NULL,16);
    }
    out[oi]='\0';
    if(g_cfg.util_sidebyside)
        print_sidebyside("Hex:",a,"Decoded:",out);
    else {
        printf("  Hex    : %s\n",a);
        vprint(COL_CYAN,"  Decoded: %s\n",out);
    }
    free(out);
}

static void cmd_rot13(const char *a) {
    if(!a||!*a){printf("Usage: rot13 <text>\n");return;}
    char *out=strdup(a);
    for(int i=0;out[i];i++){
        char c=out[i];
        if(c>='a'&&c<='z') out[i]='a'+(c-'a'+13)%26;
        else if(c>='A'&&c<='Z') out[i]='A'+(c-'A'+13)%26;
    }
    if(g_cfg.util_sidebyside)
        print_sidebyside("Input:",a,"ROT13:",out);
    else {
        printf("  Input : %s\n",a);
        vprint(COL_CYAN,"  ROT13 : %s\n",out);
    }
    free(out);
}

static void cmd_urlenc(const char *a) {
    if(!a||!*a){printf("Usage: urlenc <text>\n");return;}
    char out[VENOM_MAX_INPUT*3]; int oi=0;
    for(const unsigned char *p=(const unsigned char*)a;*p;p++){
        if(isalnum(*p)||*p=='-'||*p=='_'||*p=='.'||*p=='~')
            out[oi++]=*p;
        else { sprintf(out+oi,"%%%02X",*p); oi+=3; }
    }
    out[oi]='\0';
    if(g_cfg.util_sidebyside)
        print_sidebyside("Input:",a,"Encoded:",out);
    else {
        printf("  Input  : %s\n",a);
        vprint(COL_CYAN,"  Encoded: %s\n",out);
    }
}

static void cmd_urldec(const char *a) {
    if(!a||!*a){printf("Usage: urldec <text>\n");return;}
    char *out=(char*)malloc(strlen(a)+2); int oi=0;
    for(const char *p=a;*p;){
        if(*p=='%'&&p[1]&&p[2]){
            char h[3]={p[1],p[2],0}; out[oi++]=(char)strtol(h,NULL,16); p+=3;
        } else if(*p=='+'){out[oi++]=' ';p++;}
        else out[oi++]=*p++;
    }
    out[oi]='\0';
    if(g_cfg.util_sidebyside)
        print_sidebyside("Encoded:",a,"Decoded:",out);
    else {
        printf("  Encoded: %s\n",a);
        vprint(COL_CYAN,"  Decoded: %s\n",out);
    }
    free(out);
}

/* ================================================================
   TEXT UTILITIES — UPGRADED
   ================================================================ */
static void cmd_upper(const char *a) {
    if(!a||!*a){printf("Usage: upper <text>\n");return;}
    char *out=strdup(a);
    for(int i=0;out[i];i++) out[i]=(char)toupper((unsigned char)out[i]);
    if(g_cfg.util_sidebyside)
        print_sidebyside("Input:",a,"Upper:",out);
    else { printf("  Input : %s\n",a); vprint(COL_CYAN,"  Upper : %s\n",out); }
    free(out);
}

static void cmd_lower(const char *a) {
    if(!a||!*a){printf("Usage: lower <text>\n");return;}
    char *out=strdup(a);
    for(int i=0;out[i];i++) out[i]=(char)tolower((unsigned char)out[i]);
    if(g_cfg.util_sidebyside)
        print_sidebyside("Input:",a,"Lower:",out);
    else { printf("  Input : %s\n",a); vprint(COL_CYAN,"  Lower : %s\n",out); }
    free(out);
}

static void cmd_rev(const char *a) {
    if(!a||!*a){printf("Usage: rev <text>\n");return;}
    char *out=strdup(a); int n=(int)strlen(out);
    for(int i=0;i<n/2;i++){char t=out[i];out[i]=out[n-1-i];out[n-1-i]=t;}
    if(g_cfg.util_sidebyside)
        print_sidebyside("Input:",a,"Reversed:",out);
    else { printf("  Input   : %s\n",a); vprint(COL_CYAN,"  Reversed: %s\n",out); }
    free(out);
}

/* GUID — supports count */
static void cmd_guid(const char *a) {
    int count=g_cfg.guid_count;
    if(a&&*a) count=atoi(a);
    if(count<1) count=1;
    if(count>100) count=100;
    srand((unsigned int)time(NULL)+(unsigned int)(size_t)a);
    for(int i=0;i<count;i++){
        unsigned int r1=rand(),r2=rand(),r3=rand(),r4=rand();
        vprint(COL_CYAN,"  %08x-%04x-4%03x-%04x-%08x%04x\n",
               r1,r2&0xFFFF,r3&0x0FFF,(r4&0x3FFF)|0x8000,rand(),rand()&0xFFFF);
    }
}

/* Proper expression evaluator: supports +,-,*,/,% and functions */
static double eval_expr(const char *s, int *err);

/* Forward declarations */
static double eval_expr_pos(const char *s, int *pos, int *err);
static double eval_power(const char *s, int *pos, int *err);

static double eval_primary(const char *s, int *pos, int *err) {
    while(s[*pos]==' ') (*pos)++;
    /* Functions */
    if(strncmp(s+*pos,"sqrt(",5)==0){
        *pos+=5;
        double v=eval_expr_pos(s,pos,err);
        if(s[*pos]==')') (*pos)++;
        return sqrt(v);
    }
    if(strncmp(s+*pos,"abs(",4)==0){
        *pos+=4;
        double v=eval_expr_pos(s,pos,err);
        if(s[*pos]==')') (*pos)++;
        return fabs(v);
    }
    /* Parentheses */
    if(s[*pos]=='('){
        (*pos)++;
        double v=eval_expr_pos(s,pos,err);
        if(s[*pos]==')') (*pos)++;
        return v;
    }
    /* Unary minus */
    if(s[*pos]=='-'){(*pos)++; return -eval_primary(s,pos,err);}
    /* Number */
    char *end; double v=strtod(s+*pos,&end);
    if(end==s+*pos){*err=1;return 0;}
    *pos=(int)(end-s);
    return v;
}

/* Forward declaration for mutual recursion */
static double eval_expr_pos(const char *s, int *pos, int *err);

/* Right-associative power: 2^3^2 = 2^(3^2) = 512 */
static double eval_power(const char *s, int *pos, int *err) {
    double v=eval_primary(s,pos,err);
    while(s[*pos]==' ') (*pos)++;
    while(s[*pos]=='^'){
        (*pos)++;
        while(s[*pos]==' ') (*pos)++;
        double r=eval_power(s,pos,err);
        v=pow(v,r);
    }
    return v;
}

static double eval_term(const char *s, int *pos, int *err) {
    double v=eval_power(s,pos,err);
    while(s[*pos]==' ') (*pos)++;
    while(s[*pos]=='*'||s[*pos]=='/'||s[*pos]=='%'){
        char op=s[(*pos)++];
        while(s[*pos]==' ') (*pos)++;
        double r=eval_power(s,pos,err);
        if(op=='*') v*=r;
        else if(op=='/') v=(r!=0)?v/r:0;
        else v=(long long)v%(long long)r;
        while(s[*pos]==' ') (*pos)++;
    }
    return v;
}

static double eval_expr_pos(const char *s, int *pos, int *err) {
    double v=eval_term(s,pos,err);
    while(s[*pos]==' ') (*pos)++;
    while(s[*pos]=='+'||s[*pos]=='-'){
        char op=s[(*pos)++];
        while(s[*pos]==' ') (*pos)++;
        double r=eval_term(s,pos,err);
        if(op=='+') v+=r; else v-=r;
        while(s[*pos]==' ') (*pos)++;
    }
    return v;
}

static double eval_expr(const char *s, int *err) {
    int pos=0;
    return eval_expr_pos(s,&pos,err);
}

static void cmd_calcpy(const char *a) {
    if(!a||!*a){printf("Usage: calc <expression>  e.g. calc (10+5)*2\n");return;}
    int err=0;
    double result=eval_expr(a,&err);
    if(err){printf("  Invalid expression\n");return;}
    char fmt[32]; snprintf(fmt,sizeof(fmt),"  %%.%df\n",g_cfg.calc_precision);
    printf("  %s = ",a);
    vprint(COL_CYAN,fmt,result);
}

static void cmd_timer(const char *a) {
    int secs=a&&*a?atoi(a):0;
    if(secs<=0){printf("Usage: timer <seconds>\n");return;}
    printf("  Timer: %d seconds\n\n",secs);
    for(int i=secs;i>=0;i--){
        printf("\r  ");
        if(g_cfg.timer_style==1){
            draw_progress_bar((secs-i)*100/secs,30,
                              i<10?COL_RED:COL_GREEN);
            printf("  %ds remaining  ",i);
        } else {
            vprint(COL_CYAN,"%d",i);
            printf("s remaining    ");
        }
        fflush(stdout);
        if(i>0) venom_sleep_ms(1000);
    }
    printf("\n");
    vprint(COL_GREEN,"  ✓ Done!\n");
}

static void cmd_typewriter(const char *a) {
    if(!a||!*a){printf("Usage: typewriter <text>\n");return;}
    int speed=g_cfg.typewriter_speed;
    /* Check for speed modifier */
    if(strncmp(a,"fast ",5)==0){speed=20;a+=5;}
    else if(strncmp(a,"slow ",5)==0){speed=120;a+=5;}
    for(const char *p=a;*p;p++){
        putchar(*p); fflush(stdout);
        venom_sleep_ms(speed);
    }
    putchar('\n');
}

static void cmd_lipsum(const char *a) {
    int count=a&&*a?atoi(a):1;
    if(count<1) count=1; if(count>10) count=10;
    const char *para=
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
        "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris "
        "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
        "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
        "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum.";
    for(int i=0;i<count;i++) printf("%s\n\n",para);
}

static void cmd_clock_anim(const char *a) {
    int dur=a&&*a?atoi(a):60;
    if(dur<=0) dur=60;
    printf("  Live clock — %ds (Ctrl+C to stop)\n\n",dur);
    time_t start=time(NULL);
    while(time(NULL)-start<dur){
        time_t now=time(NULL); struct tm *t=localtime(&now);
        char buf[32]; strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",t);
        printf("\r  "); vprint(COL_CYAN,"%s",buf);
        fflush(stdout); venom_sleep_ms(1000);
    }
    printf("\n");
}

static void cmd_hexplain(const char *a) {
    if(!a||!*a){printf("Usage: hexplain <file|text>\n");return;}
    FILE *f=fopen(a,"rb");
    const unsigned char *data=NULL; size_t dlen=0;
    unsigned char *fbuf=NULL;
    if(f){
        fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
        fbuf=(unsigned char*)malloc(sz);
        if(fbuf){dlen=fread(fbuf,1,sz,f);data=fbuf;}
        fclose(f);
    } else { data=(const unsigned char*)a; dlen=strlen(a); }
    venom_box_label("Hex Dump",COL_CYAN);
    for(size_t i=0;i<dlen;i+=16){
        vprint(COL_DIM,"%08zx  ",i);
        for(size_t j=0;j<16;j++){
            if(i+j<dlen){ vprint(COL_CYAN,"%02X ",data[i+j]); }
            else printf("   ");
            if(j==7) printf(" ");
        }
        printf(" │");
        for(size_t j=0;j<16&&i+j<dlen;j++)
            putchar((data[i+j]>=32&&data[i+j]<127)?data[i+j]:'.');
        printf("│\n");
    }
    if(fbuf) free(fbuf);
}

/* ================================================================
   CLIPBOARD
   ================================================================ */
static void cmd_clipcopy(const char *a) {
    if(!a||!*a){printf("Usage: clipcopy <text>\n");return;}
#ifdef VENOM_WINDOWS
    char cmd[VENOM_MAX_INPUT+64]; snprintf(cmd,sizeof(cmd),"echo %s|clip",a); system(cmd);
    vprint(COL_GREEN,"  Copied to clipboard.\n");
#elif defined(__APPLE__)
    FILE *f=popen("pbcopy","w"); if(f){fputs(a,f);pclose(f);vprint(COL_GREEN,"  Copied.\n");}
#else
    FILE *f=popen("xclip -selection clipboard 2>/dev/null||xsel --clipboard --input 2>/dev/null","w");
    if(f){fputs(a,f);pclose(f);vprint(COL_GREEN,"  Copied.\n");}
#endif
}

static void cmd_clippaste(const char *a) {
    (void)a;
#ifdef VENOM_WINDOWS
    system("powershell -command Get-Clipboard");
#elif defined(__APPLE__)
    system("pbpaste");
#else
    int r=system("xclip -selection clipboard -o 2>/dev/null||xsel --clipboard --output 2>/dev/null"); (void)r;
#endif
}

/* ================================================================
   FUN — MATRIX UPGRADED
   ================================================================ */
static void cmd_matrix(const char *a) {
    /* Parse args: matrix [fast|slow|speed N] [color X] [chars X] */
    int speed=g_cfg.matrix_speed;
    char color[32]; strncpy(color,g_cfg.matrix_color,31);
    char chars[32]; strncpy(chars,g_cfg.matrix_chars,31);
    int dur=0; /* 0 = infinite */

    if(a&&*a){
        if(!strcmp(a,"fast"))  speed=9;
        else if(!strcmp(a,"slow")) speed=2;
        else {
            int v=atoi(a);
            if(v>0&&v<=10) speed=v;
            else if(v>10)  dur=v;
        }
    }

    /* Speed → sleep ms */
    int ms=110-(speed*10);
    if(ms<10) ms=10;

    int cols=80;
#ifdef VENOM_POSIX
    struct winsize ws;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==0&&ws.ws_col>0) cols=ws.ws_col;
#endif

    printf("  Matrix — speed:%d color:%s chars:%s  (Ctrl+C to stop)\n\n",
           speed,color,chars);

    srand((unsigned int)time(NULL));
    time_t start=time(NULL);
    int col_idx=0;

    venom_set_color(COL_GREEN); /* default */

    while(1){
        if(dur>0&&(int)(time(NULL)-start)>=dur) break;

        for(int i=0;i<cols;i++){
            /* Rainbow: change color per character */
            if(!strcmp(color,"rainbow")){
                venom_set_color(rainbow_color(col_idx++));
            } else if(!strcmp(color,"red"))     venom_set_color(COL_RED);
            else if(!strcmp(color,"cyan"))      venom_set_color(COL_CYAN);
            else if(!strcmp(color,"white"))     venom_set_color(COL_WHITE);
            else if(!strcmp(color,"blue"))      venom_set_color(COL_BLUE);
            else                                venom_set_color(COL_GREEN);

            /* Character set */
            char ch;
            if(!strcmp(chars,"hex")){
                const char *hx="0123456789ABCDEF";
                ch=hx[rand()%16];
            } else if(!strcmp(chars,"ascii")){
                ch=(char)(33+rand()%94);
            } else {
                /* binary */
                ch=(rand()%3==0)?' ':(rand()%2?'1':'0');
            }
            putchar(ch);
        }
        putchar('\n');
        venom_reset_color();
        fflush(stdout);
        venom_sleep_ms(ms);
    }
    venom_reset_color();
    printf("Matrix stopped.\n");
}

/* ================================================================
   HISTORY
   ================================================================ */
static void cmd_history(const char *a) {
    venom_box_label("Command History",COL_CYAN);
    printf("  File: %s\n\n",g_hist_path);
    FILE *f=fopen(g_hist_path,"r");
    if(!f){printf("  (no history yet)\n");return;}
    char line[VENOM_MAX_INPUT]; int n=1;
    /* If search term given */
    const char *term=(a&&*a)?a:NULL;
    while(fgets(line,sizeof(line),f)){
        str_trim(line);
        if(term&&!strstr(line,term)) continue;
        vprint(COL_DIM,"%4d  ",n++);
        printf("%s\n",line);
    }
    fclose(f);
}

/* ================================================================
   CONFIG COMMANDS
   ================================================================ */
static void cmd_config(const char *a) {
    char sub[64]; sub[0]='\0';
    if(!a||!*a){
        vprint(COL_CYAN,"Config commands:\n");
        printf("  config show              — view all settings\n");
        printf("  config set <key> <val>   — change a value\n");
        printf("  config clear <key>       — clear a value\n");
        printf("  config path              — show config file path\n");
        printf("\nKeys: hf_key, ai_provider, ollama_host, ollama_model,\n");
        printf("  remote_endpoint, remote_key, remote_model,\n");
        printf("  matrix_color, matrix_speed, matrix_chars,\n");
        printf("  ls_colors, ping_graph, tree_style,\n");
        printf("  util_sidebyside, guid_count, calc_precision,\n");
        printf("  timer_style, typewriter_speed,\n");
        printf("  serve_port, serve_dir, serve_autostart, pin\n");
        return;
    }
    sscanf(a,"%63s",sub);
    const char *rest=a+strlen(sub); while(*rest==' ') rest++;

    if(!strcmp(sub,"path")){
        printf("  %s\n",g_config_path);
    } else if(!strcmp(sub,"show")){
        venom_box_label("venom.console config",COL_CYAN);
        printf("  File        : %s\n\n",g_config_path);
        /* Mask keys */
        char mk[CFG_HF_KEY_MAX]="(not set)";
        if(g_cfg.hf_key[0]){
            size_t kl=strlen(g_cfg.hf_key);
            if(kl>8) snprintf(mk,sizeof(mk),"%c%c%c%c****%s",
                g_cfg.hf_key[0],g_cfg.hf_key[1],g_cfg.hf_key[2],g_cfg.hf_key[3],
                g_cfg.hf_key+kl-4);
            else strcpy(mk,"****");
        }
        printf("  %-22s %s\n","hf_key:",mk);
        printf("  %-22s %s\n","ai_provider:",g_cfg.ai_provider);
        printf("  %-22s %s\n","ollama_host:",g_cfg.ollama_host);
        printf("  %-22s %s\n","ollama_model:",g_cfg.ollama_model);
        printf("  %-22s %s\n","remote_endpoint:",g_cfg.remote_endpoint[0]?g_cfg.remote_endpoint:"(not set)");
        printf("  %-22s %s\n","remote_model:",g_cfg.remote_model[0]?g_cfg.remote_model:"(not set)");
        printf("  %-22s %s\n","matrix_color:",g_cfg.matrix_color);
        printf("  %-22s %d\n","matrix_speed:",g_cfg.matrix_speed);
        printf("  %-22s %s\n","matrix_chars:",g_cfg.matrix_chars);
        printf("  %-22s %s\n","tree_style:",g_cfg.tree_style);
        printf("  %-22s %s\n","ls_colors:",g_cfg.ls_colors?"on":"off");
        printf("  %-22s %d\n","guid_count:",g_cfg.guid_count);
        printf("  %-22s %d dp\n","calc_precision:",g_cfg.calc_precision);
        printf("  %-22s %s\n","timer_style:",g_cfg.timer_style?"bar":"number");
        printf("  %-22s %dms\n","typewriter_speed:",g_cfg.typewriter_speed);
        printf("  %-22s %d\n","serve_port:",g_cfg.serve_port);
        printf("  %-22s %s\n","serve_dir:",g_cfg.serve_dir);
        printf("  %-22s %s\n","serve_autostart:",g_cfg.serve_autostart?"on":"off");
        printf("  %-22s %s\n","pin:"    ,g_cfg.pin[0]?"set":"not set");
        printf("  %-22s %s\n","killswitch:",g_cfg.killswitch_enabled?"ARMED":"disarmed");
    } else if(!strcmp(sub,"set")){
        char key[64]; key[0]='\0';
        sscanf(rest,"%63s",key);
        const char *val=rest+strlen(key); while(*val==' ') val++;
        if(!*val){
            printf("Value for '%s': ",key);
            char vbuf[CFG_VAL_MAX]; fgets(vbuf,sizeof(vbuf),stdin); str_trim(vbuf);
            val=vbuf;
            /* Store since val points to local */
            #define SET_CFG_FIELD(field,maxlen) strncpy(g_cfg.field,vbuf,maxlen-1)
        }
        if(!strcmp(key,"hf_key"))          strncpy(g_cfg.hf_key,val,CFG_HF_KEY_MAX-1);
        else if(!strcmp(key,"ai_provider"))strncpy(g_cfg.ai_provider,val,31);
        else if(!strcmp(key,"ollama_host"))strncpy(g_cfg.ollama_host,val,127);
        else if(!strcmp(key,"ollama_model"))strncpy(g_cfg.ollama_model,val,63);
        else if(!strcmp(key,"remote_endpoint"))strncpy(g_cfg.remote_endpoint,val,CFG_VAL_MAX-1);
        else if(!strcmp(key,"remote_key"))strncpy(g_cfg.remote_key,val,CFG_HF_KEY_MAX-1);
        else if(!strcmp(key,"remote_model"))strncpy(g_cfg.remote_model,val,63);
        else if(!strcmp(key,"matrix_color"))strncpy(g_cfg.matrix_color,val,31);
        else if(!strcmp(key,"matrix_speed"))g_cfg.matrix_speed=atoi(val);
        else if(!strcmp(key,"matrix_chars"))strncpy(g_cfg.matrix_chars,val,31);
        else if(!strcmp(key,"tree_style"))  strncpy(g_cfg.tree_style,val,15);
        else if(!strcmp(key,"ls_colors"))   g_cfg.ls_colors=atoi(val);
        else if(!strcmp(key,"guid_count"))  g_cfg.guid_count=atoi(val);
        else if(!strcmp(key,"calc_precision"))g_cfg.calc_precision=atoi(val);
        else if(!strcmp(key,"timer_style")) g_cfg.timer_style=atoi(val);
        else if(!strcmp(key,"typewriter_speed"))g_cfg.typewriter_speed=atoi(val);
        else if(!strcmp(key,"serve_port"))  g_cfg.serve_port=atoi(val);
        else if(!strcmp(key,"serve_dir"))   strncpy(g_cfg.serve_dir,val,MAX_PATH_LEN-1);
        else if(!strcmp(key,"serve_autostart"))g_cfg.serve_autostart=atoi(val);
        else if(!strcmp(key,"pin")){
            if(strlen(val)<4){vprint(COL_RED,"PIN must be 4+ chars\n");return;}
            strncpy(g_cfg.pin,val,CFG_PIN_MAX-1);
        }
        else { printf("Unknown key '%s'\n",key); return; }
        config_save();
        vprint(COL_GREEN,"  Saved: %s = %s\n",key,
               (!strcmp(key,"hf_key")||!strcmp(key,"pin")||!strcmp(key,"remote_key"))?"****":val);
    } else if(!strcmp(sub,"clear")){
        char key[64]; sscanf(rest,"%63s",key);
        if(!strcmp(key,"hf_key"))    memset(g_cfg.hf_key,0,sizeof(g_cfg.hf_key));
        else if(!strcmp(key,"pin"))  memset(g_cfg.pin,0,sizeof(g_cfg.pin));
        else if(!strcmp(key,"remote_key"))memset(g_cfg.remote_key,0,sizeof(g_cfg.remote_key));
        else if(!strcmp(key,"remote_endpoint"))memset(g_cfg.remote_endpoint,0,sizeof(g_cfg.remote_endpoint));
        else{printf("Cannot clear '%s'\n",key);return;}
        config_save();
        vprint(COL_YELLOW,"  Cleared: %s\n",key);
    } else {
        printf("Unknown sub-command '%s'\n",sub);
    }
}

/* ================================================================
   KILL SWITCH
   ================================================================ */
static void cmd_killswitch(const char *a) {
    char sub[64]; sub[0]='\0';
    if(a&&*a) sscanf(a,"%63s",sub);

    if(!*sub){
        vprint(COL_CYAN,"Kill switch commands:\n");
        printf("  killswitch status    — show state\n");
        printf("  killswitch set-pin   — set/change PIN\n");
        printf("  killswitch arm       — arm (PIN required)\n");
        printf("  killswitch trigger   — LOCK immediately (PIN required)\n");
        printf("  killswitch disarm    — remove lock (PIN required)\n");
        return;
    }

    if(!strcmp(sub,"status")){
        int triggered=killswitch_triggered();
        venom_box_label("Kill Switch",COL_CYAN);
        printf("  PIN set   : %s\n",g_cfg.pin[0]?"yes":"no");
        printf("  Armed     : %s\n",g_cfg.killswitch_enabled?"YES":"no");
        if(triggered) vprint(COL_RED,"  Lock file : PRESENT — LOCKED\n");
        else          vprint(COL_GREEN,"  Lock file : not present\n");
        return;
    }

    if(!strcmp(sub,"set-pin")){
        char p1[CFG_PIN_MAX],p2[CFG_PIN_MAX];
        if(g_cfg.pin[0]){
            printf("Current PIN: "); fgets(p1,sizeof(p1),stdin); str_trim(p1);
            if(strcmp(p1,g_cfg.pin)!=0){vprint(COL_RED,"Incorrect PIN.\n");return;}
        }
        printf("New PIN (4+ chars): "); fgets(p1,sizeof(p1),stdin); str_trim(p1);
        if(strlen(p1)<4){vprint(COL_RED,"Too short.\n");return;}
        printf("Confirm PIN: ");        fgets(p2,sizeof(p2),stdin); str_trim(p2);
        if(strcmp(p1,p2)!=0){vprint(COL_RED,"PINs don't match.\n");return;}
        strncpy(g_cfg.pin,p1,CFG_PIN_MAX-1);
        config_save();
        vprint(COL_GREEN,"PIN set.\n");
        return;
    }

    if(!g_cfg.pin[0]){vprint(COL_YELLOW,"Set a PIN first: killswitch set-pin\n");return;}
    char entered[CFG_PIN_MAX];
    printf("PIN: "); fgets(entered,sizeof(entered),stdin); str_trim(entered);
    if(strcmp(entered,g_cfg.pin)!=0){vprint(COL_RED,"Incorrect PIN.\n");return;}

    if(!strcmp(sub,"arm")){
        g_cfg.killswitch_enabled=1; config_save();
        vprint(COL_YELLOW,"Kill switch ARMED.\n");
    } else if(!strcmp(sub,"trigger")){
        char c[16]; vprint(COL_RED,"Type CONFIRM to lock venom: ");
        fgets(c,sizeof(c),stdin); str_trim(c);
        if(strcmp(c,"CONFIRM")!=0){printf("Aborted.\n");return;}
        g_cfg.killswitch_enabled=1; config_save();
        killswitch_arm_files();
        vprint(COL_RED,"\n*** KILL SWITCH TRIGGERED — venom is LOCKED ***\n");
        vprint(COL_YELLOW,"Run 'killswitch disarm' to restore.\n");
        exit(1);
    } else if(!strcmp(sub,"disarm")){
        killswitch_disarm_files();
        g_cfg.killswitch_enabled=0; config_save();
        vprint(COL_GREEN,"Kill switch DISARMED.\n");
    } else {
        printf("Unknown sub-command '%s'\n",sub);
    }
}

/* ================================================================
   AI SYSTEM
   ================================================================ */

/* Ollama HTTP POST to local API */
static void ollama_chat(const char *host, int port, const char *model,
                        const char *user_msg, char *out, int outlen) {
    venom_sock_t s; struct sockaddr_in sa={0};
    sa.sin_family=AF_INET; sa.sin_port=htons((unsigned short)port);
    struct hostent *he=gethostbyname(host);
    if(!he){snprintf(out,outlen,"Cannot resolve %s",host);return;}
    memcpy(&sa.sin_addr,he->h_addr,he->h_length);
    s=socket(AF_INET,SOCK_STREAM,0);
#ifdef VENOM_WINDOWS
    DWORD tv=30000; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
#else
    struct timeval tv={30,0}; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
#endif
    if(connect(s,(struct sockaddr*)&sa,sizeof(sa))!=0){
        snprintf(out,outlen,"Cannot connect to Ollama at %s:%d",host,port);
        venom_closesocket(s);return;
    }
    char json[VENOM_MAX_INPUT+512];
    snprintf(json,sizeof(json),
        "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
        "\"stream\":false}",model,user_msg);
    char req[VENOM_MAX_INPUT+1024];
    snprintf(req,sizeof(req),
        "POST /api/chat HTTP/1.0\r\nHost: %s:%d\r\n"
        "Content-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
        host,port,strlen(json),json);
    send(s,req,(int)strlen(req),0);
    char resp[16384]; int total=0,n;
    while((n=recv(s,resp+total,sizeof(resp)-total-1,0))>0) total+=n;
    resp[total]='\0';
    venom_closesocket(s);
    /* Parse "content" from Ollama response */
    char *body=strstr(resp,"\r\n\r\n");
    if(body) body+=4; else body=resp;
    char *ct=strstr(body,"\"content\":\"");
    if(ct){
        ct+=11;
        char *end=strstr(ct,"\"");
        if(end){int l=(int)(end-ct);if(l>outlen-1)l=outlen-1;strncpy(out,ct,l);out[l]='\0';}
        else strncpy(out,ct,outlen-1);
    } else {
        snprintf(out,outlen,"[No response — is Ollama running? Try: ollama serve]");
    }
}

/* Generic OpenAI-compatible POST */
static void openai_chat(const char *endpoint, const char *key, const char *model,
                        const char *msg, char *out, int outlen) {
    /* Parse host and path from endpoint */
    char host[256],path[512];
    const char *url=endpoint;
    if(strncmp(url,"http://",7)==0) url+=7;
    else if(strncmp(url,"https://",8)==0) url+=8;
    const char *sl=strchr(url,'/');
    if(sl){
        int hl=(int)(sl-url); strncpy(host,url,hl); host[hl]='\0';
        strncpy(path,sl,sizeof(path)-1);
        strncat(path,"/chat/completions",sizeof(path)-strlen(path)-1);
    } else {
        strncpy(host,url,sizeof(host)-1);
        strcpy(path,"/v1/chat/completions");
    }
    char json[VENOM_MAX_INPUT+512];
    snprintf(json,sizeof(json),
        "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
        "\"max_tokens\":500}",model,msg);
    venom_sock_t s; struct sockaddr_in sa={0};
    sa.sin_family=AF_INET; sa.sin_port=htons(80);
    struct hostent *he=gethostbyname(host);
    if(!he){snprintf(out,outlen,"Cannot resolve %s",host);return;}
    memcpy(&sa.sin_addr,he->h_addr,he->h_length);
    s=socket(AF_INET,SOCK_STREAM,0);
#ifdef VENOM_WINDOWS
    DWORD tv=30000; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
#else
    struct timeval tv={30,0}; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
#endif
    if(connect(s,(struct sockaddr*)&sa,sizeof(sa))!=0){
        snprintf(out,outlen,"Connect failed to %s",host);
        venom_closesocket(s);return;
    }
    char req[VENOM_MAX_INPUT+1024];
    snprintf(req,sizeof(req),
        "POST %s HTTP/1.0\r\nHost: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n\r\n%s",
        path,host,key,strlen(json),json);
    send(s,req,(int)strlen(req),0);
    char resp[16384]; int total=0,n;
    while((n=recv(s,resp+total,sizeof(resp)-total-1,0))>0) total+=n;
    resp[total]='\0';
    venom_closesocket(s);
    char *ct=strstr(resp,"\"content\":");
    if(ct){
        ct+=10; while(*ct=='"'||*ct==' ') ct++;
        char *end=strstr(ct,"\","); if(!end) end=strstr(ct,"\"}");
        if(end){int l=(int)(end-ct);if(l>outlen-1)l=outlen-1;strncpy(out,ct,l);out[l]='\0';}
        else strncpy(out,ct,outlen-1);
    } else {
        snprintf(out,outlen,"[Parse error — check endpoint/key]");
    }
}

static void cmd_chatbot(const char *args) {
    (void)args;
    venom_box_label("AI Chat",COL_CYAN);
    printf("  Provider : %s\n",g_cfg.ai_provider);
    if(!strcmp(g_cfg.ai_provider,"ollama"))
        printf("  Model    : %s @ %s\n",g_cfg.ollama_model,g_cfg.ollama_host);
    else if(!strcmp(g_cfg.ai_provider,"remote"))
        printf("  Model    : %s @ %s\n",g_cfg.remote_model,g_cfg.remote_endpoint);
    else
        printf("  (HuggingFace)\n");
    printf("  Type 'exit' to quit\n\n");

    char user_msg[VENOM_MAX_INPUT], ai_resp[8192];
    while(1){
        vprint(COL_CYAN,"You> "); fflush(stdout);
        if(!fgets(user_msg,sizeof(user_msg),stdin)) break;
        str_trim(user_msg);
        if(!*user_msg) continue;
        if(!strcmp(user_msg,"exit")) break;
        history_save(("[AI] " + 0)); /* just note it was AI session */

        ai_resp[0]='\0';
        if(!strcmp(g_cfg.ai_provider,"ollama")){
            /* Parse host:port */
            char host[128]; int port=11434;
            char tmp[128]; strncpy(tmp,g_cfg.ollama_host,127);
            char *colon=strchr(tmp,':');
            if(colon){*colon='\0';port=atoi(colon+1);}
            strncpy(host,tmp,127);
            vprint(COL_DIM,"  [thinking...]\r"); fflush(stdout);
            ollama_chat(host,port,g_cfg.ollama_model,user_msg,ai_resp,sizeof(ai_resp));
        } else if(!strcmp(g_cfg.ai_provider,"remote")){
            if(!g_cfg.remote_endpoint[0]||!g_cfg.remote_model[0]){
                printf("  Set remote_endpoint and remote_model first: config set remote_endpoint ...\n");
                continue;
            }
            const char *key=g_cfg.remote_key[0]?g_cfg.remote_key:getenv("VENOM_AI_KEY");
            if(!key||!*key){printf("  Set remote_key: config set remote_key ...\n");continue;}
            vprint(COL_DIM,"  [thinking...]\r"); fflush(stdout);
            openai_chat(g_cfg.remote_endpoint,key,g_cfg.remote_model,user_msg,ai_resp,sizeof(ai_resp));
        } else {
            /* HuggingFace */
            const char *key=g_cfg.hf_key[0]?g_cfg.hf_key:getenv("VENOM_HF_KEY");
            if(!key||!*key){
                printf("  Set HF key: config set hf_key hf_...\n"); continue;
            }
            vprint(COL_DIM,"  [thinking...]\r"); fflush(stdout);
            openai_chat("router.huggingface.co",key,"MiniMaxAI/MiniMax-M2",user_msg,ai_resp,sizeof(ai_resp));
        }
        printf("             \r"); /* clear "thinking" */
        vprint(COL_GREEN,"\nAI> "); printf("%s\n\n",ai_resp);
    }
    printf("Chat ended.\n");
}

/* Ollama management commands */
static void cmd_ollama(const char *a) {
    char sub[64]; sub[0]='\0';
    if(a&&*a) sscanf(a,"%63s",sub);

    if(!*sub||!strcmp(sub,"help")){
        vprint(COL_CYAN,"Ollama commands:\n");
        printf("  ollama status            — check if Ollama is running\n");
        printf("  ollama list              — list downloaded models\n");
        printf("  ollama pull <model>      — download a model\n");
        printf("  ollama recommended       — show recommended models\n");
        printf("  ollama set-default <m>   — set default model\n");
        printf("  ollama set-host <h:p>    — set Ollama host\n");
        return;
    }

    if(!strcmp(sub,"status")){
        char out[256]="";
        char host[128]; int port=11434;
        char tmp[128]; strncpy(tmp,g_cfg.ollama_host,127);
        char *colon=strchr(tmp,':'); if(colon){*colon='\0';port=atoi(colon+1);}
        strncpy(host,tmp,127);
        if(http_get(host,port,"/api/tags",out,sizeof(out))>0)
            vprint(COL_GREEN,"  ✓ Ollama is running at %s\n",g_cfg.ollama_host);
        else
            vprint(COL_RED,"  ✗ Ollama not running. Start with: ollama serve\n");

    } else if(!strcmp(sub,"list")){
        char cmd[256];
        snprintf(cmd,sizeof(cmd),"ollama list"); system(cmd);

    } else if(!strcmp(sub,"pull")){
        const char *rest=a+4; while(*rest==' ') rest++;
        if(!*rest){printf("Usage: ollama pull <model>\n");return;}
        char cmd[256]; snprintf(cmd,sizeof(cmd),"ollama pull %s",rest);
        vprint(COL_CYAN,"  Downloading %s...\n",rest);
        system(cmd);
        vprint(COL_GREEN,"  Done. Run: config set ollama_model %s\n",rest);

    } else if(!strcmp(sub,"recommended")){
        venom_box_label("Recommended Models",COL_CYAN);
        printf("  %-20s %-8s %s\n","Model","Size","Notes");
        venom_rule(COL_DIM,50);
        printf("  %-20s %-8s %s\n","llama3.2",    "2GB", "Meta — great all-rounder");
        printf("  %-20s %-8s %s\n","mistral",     "4GB", "Fast and smart");
        printf("  %-20s %-8s %s\n","phi3",        "2GB", "Microsoft — tiny and quick");
        printf("  %-20s %-8s %s\n","gemma2",      "5GB", "Google — very capable");
        printf("  %-20s %-8s %s\n","tinyllama",   "638MB","Ultra-light, weak hardware");
        printf("  %-20s %-8s %s\n","codellama",   "4GB", "Code-focused");
        printf("  %-20s %-8s %s\n","deepseek-r1", "4GB", "Strong reasoning");
        venom_rule(COL_DIM,50);
        printf("\n  Pull with: ollama pull <model>\n");

    } else if(!strcmp(sub,"set-default")){
        const char *rest=a+11; while(*rest==' ') rest++;
        if(!*rest){printf("Usage: ollama set-default <model>\n");return;}
        strncpy(g_cfg.ollama_model,rest,63);
        config_save();
        vprint(COL_GREEN,"  Default model set to: %s\n",g_cfg.ollama_model);

    } else if(!strcmp(sub,"set-host")){
        const char *rest=a+8; while(*rest==' ') rest++;
        if(!*rest){printf("Usage: ollama set-host <host:port>\n");return;}
        strncpy(g_cfg.ollama_host,rest,127);
        config_save();
        vprint(COL_GREEN,"  Ollama host set to: %s\n",g_cfg.ollama_host);
    } else {
        printf("Unknown ollama sub-command '%s'\n",sub);
    }
}

/* ================================================================
   SETUP WIZARD
   ================================================================ */
static void cmd_setup(const char *a) {
    int reset=a&&strstr(a,"reset");
    if(reset){
        char c[8]; printf("Reset config to defaults? (type YES): ");
        fgets(c,sizeof(c),stdin); str_trim(c);
        if(strcmp(c,"YES")!=0){printf("Aborted.\n");return;}
        /* Re-init config to defaults */
        memset(&g_cfg,0,sizeof(g_cfg));
        strcpy(g_cfg.ai_provider,"ollama");
        strcpy(g_cfg.ollama_host,"localhost:11434");
        strcpy(g_cfg.ollama_model,"llama3.2");
        strcpy(g_cfg.matrix_color,"rainbow");
        g_cfg.matrix_speed=5;
        strcpy(g_cfg.matrix_chars,"binary");
        g_cfg.ls_colors=1; g_cfg.ping_graph=1;
        strcpy(g_cfg.tree_style,"box");
        g_cfg.util_sidebyside=1; g_cfg.guid_count=1;
        g_cfg.calc_precision=4; g_cfg.timer_style=1;
        g_cfg.typewriter_speed=50;
        g_cfg.serve_port=8080; strcpy(g_cfg.serve_dir,".");
        config_save();
        vprint(COL_GREEN,"Config reset to defaults.\n\n");
    }

    system(CLEAR_CMD);
    venom_box_label("venom.console Setup Wizard",COL_GREEN);
    printf("\n");

    /* Step 1: System check */
    vprint(COL_CYAN,"[1/4] Checking system...\n");
#ifdef VENOM_WINDOWS
    BOOL admin=FALSE; HANDLE tok=NULL;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&tok)){
        TOKEN_ELEVATION e; DWORD sz=sizeof(e);
        if(GetTokenInformation(tok,TokenElevation,&e,sizeof(e),&sz)) admin=(BOOL)e.TokenIsElevated;
        CloseHandle(tok);
    }
    printf("  Platform  : Windows\n");
    printf("  Admin     : "); vprint(admin?COL_GREEN:COL_YELLOW,"%s\n",admin?"YES":"NO (some features limited)");
#else
    struct utsname u; uname(&u);
    printf("  Platform  : %s %s\n",u.sysname,u.release);
    printf("  Root      : "); vprint(getuid()==0?COL_GREEN:COL_YELLOW,"%s\n",getuid()==0?"YES":"NO");
#endif
    printf("  Config    : %s\n\n",g_config_path);

    /* Step 2: AI provider */
    vprint(COL_CYAN,"[2/4] Choose AI provider:\n");
    printf("  1. Ollama  (local, free, private — recommended)\n");
    printf("  2. HuggingFace (remote, free tier)\n");
    printf("  3. Remote/Custom  (Minimax, OpenAI, Groq, etc.)\n");
    printf("  4. All of the above\n");
    printf("  5. Skip\n\n");
    printf("Choice [1-5]: ");
    char choice[8]; fgets(choice,sizeof(choice),stdin); str_trim(choice);
    int ai_choice=atoi(choice); if(ai_choice<1||ai_choice>5) ai_choice=5;

    /* Step 3: Install / configure */
    vprint(COL_CYAN,"\n[3/4] Configuring...\n");

    if(ai_choice==1||ai_choice==4){
        /* Ollama */
        printf("\n  Ollama setup:\n");
        printf("  Checking if Ollama is installed... ");
        fflush(stdout);
        int r=system("ollama --version >nul 2>&1");
        if(r!=0){
            vprint(COL_YELLOW,"not found\n");
            printf("  Install Ollama? (y/N): ");
            char yn[4]; fgets(yn,sizeof(yn),stdin); str_trim(yn);
            if(yn[0]=='y'||yn[0]=='Y'){
#ifdef VENOM_WINDOWS
                printf("  Downloading Ollama installer...\n");
                system("powershell -command \"Invoke-WebRequest https://ollama.com/download/OllamaSetup.exe -OutFile OllamaSetup.exe\"");
                system("OllamaSetup.exe");
#elif defined(__APPLE__)
                printf("  Download from: https://ollama.com/download\n");
#else
                system("curl -fsSL https://ollama.com/install.sh | sh");
#endif
            }
        } else {
            vprint(COL_GREEN,"found\n");
        }
        /* Pick model */
        printf("\n  Choose Ollama model:\n");
        printf("  1. llama3.2   (2GB — recommended)\n");
        printf("  2. mistral    (4GB)\n");
        printf("  3. phi3       (2GB — lightweight)\n");
        printf("  4. tinyllama  (638MB — minimal hardware)\n");
        printf("  5. Custom\n");
        printf("  Model [1-5]: ");
        char mc[8]; fgets(mc,sizeof(mc),stdin); str_trim(mc);
        const char *models[]={"llama3.2","mistral","phi3","tinyllama"};
        int mi=atoi(mc)-1;
        char chosen_model[64];
        if(mi>=0&&mi<4) strncpy(chosen_model,models[mi],63);
        else {
            printf("  Model name: ");
            fgets(chosen_model,sizeof(chosen_model),stdin); str_trim(chosen_model);
        }
        strncpy(g_cfg.ollama_model,chosen_model,63);
        printf("\n  Pulling %s (this may take a while)...\n",chosen_model);
        char cmd[128]; snprintf(cmd,sizeof(cmd),"ollama pull %s",chosen_model);
        system(cmd);
        if(ai_choice==1) strcpy(g_cfg.ai_provider,"ollama");
    }

    if(ai_choice==2||ai_choice==4){
        printf("\n  HuggingFace setup:\n");
        printf("  HF API key (hf_...): ");
        char hfk[CFG_HF_KEY_MAX]; fgets(hfk,sizeof(hfk),stdin); str_trim(hfk);
        if(*hfk) strncpy(g_cfg.hf_key,hfk,CFG_HF_KEY_MAX-1);
        if(ai_choice==2) strcpy(g_cfg.ai_provider,"huggingface");
    }

    if(ai_choice==3||ai_choice==4){
        printf("\n  Remote provider setup:\n");
        printf("  Presets:\n");
        printf("    1. HuggingFace   — https://router.huggingface.co/v1\n");
        printf("    2. Minimax       — https://api.minimax.chat/v1\n");
        printf("    3. OpenAI        — https://api.openai.com/v1\n");
        printf("    4. Groq          — https://api.groq.com/openai/v1\n");
        printf("    5. Custom\n");
        printf("  Preset [1-5]: ");
        char pc[8]; fgets(pc,sizeof(pc),stdin); str_trim(pc);
        const char *eps[]={"https://router.huggingface.co/v1",
                           "https://api.minimax.chat/v1",
                           "https://api.openai.com/v1",
                           "https://api.groq.com/openai/v1"};
        const char *mods[]={"MiniMaxAI/MiniMax-M2","MiniMax-Text-01","gpt-4o-mini","llama3-70b-8192"};
        int pi=atoi(pc)-1;
        if(pi>=0&&pi<4){
            strncpy(g_cfg.remote_endpoint,eps[pi],CFG_VAL_MAX-1);
            strncpy(g_cfg.remote_model,mods[pi],63);
            printf("  Endpoint: %s\n",g_cfg.remote_endpoint);
            printf("  Default model: %s\n",g_cfg.remote_model);
            printf("  (Change model: config set remote_model <name>)\n");
        } else {
            printf("  Endpoint URL: "); fgets(g_cfg.remote_endpoint,CFG_VAL_MAX,stdin); str_trim(g_cfg.remote_endpoint);
            printf("  Model name  : "); fgets(g_cfg.remote_model,64,stdin); str_trim(g_cfg.remote_model);
        }
        printf("  API key: "); fgets(g_cfg.remote_key,CFG_HF_KEY_MAX,stdin); str_trim(g_cfg.remote_key);
        if(ai_choice==3) strcpy(g_cfg.ai_provider,"remote");
    }

    /* Step 4: Write config */
    vprint(COL_CYAN,"\n[4/4] Writing config...\n");
    config_save();
    vprint(COL_GREEN,"  ✓ Config saved to %s\n\n",g_config_path);
    vprint(COL_WHITE,"Setup complete!\n");
    printf("  Run 'chatbot' to start chatting\n");
    printf("  Run 'config show' to review settings\n");
    printf("  Run 'help' for all commands\n\n");
}

/* ================================================================
   HELP
   ================================================================ */
static void cmd_help(const char *a) {
    (void)a;
    system(CLEAR_CMD);
    venom_box_label(VENOM_VERSION " — Command Reference",COL_GREEN);
    printf("\n");

    vprint(COL_CYAN,"[Core]\n");
    printf("  help, cls, version, echo, exit/q\n\n");

    vprint(COL_CYAN,"[System]\n");
    printf("  sysinfo, uptime, whoami, whereami, env, storage, battery\n");
    printf("  processes/ps, kill, isadmin\n");
    printf("  shutdown, restart, lock, logoff, hibernate, suspend\n\n");

    vprint(COL_CYAN,"[Files]\n");
    printf("  ls, cd, cat, mkdir, rmdir, copy/cp, move/mv, del/rm, touch\n");
    printf("  find, tree, head, tail, wc, filesize, compress/zip, extract/unzip\n");
    printf("  hexplain\n\n");

    vprint(COL_CYAN,"[Hashing]\n");
    printf("  md5, sha256/sha, sha512\n\n");

    vprint(COL_CYAN,"[Network — Core]\n");
    printf("  ping, tracert, ns, netstat, arp, hostname, dnsflush\n");
    printf("  myip, get-ip/ip, wifi, netinfo, netmon\n");
    printf("  blocksite/block, unblocksite/unblock\n\n");

    vprint(COL_CYAN,"[Network — Port & IP Tools]\n");
    printf("  portscan, tcping, whois, geoip\n");
    printf("  portinfo, localports, banner, reachable\n");
    printf("  ipcalc, rdns, dnscheck, interfaces, routetable\n\n");

    vprint(COL_CYAN,"[Local Server]\n");
    printf("  serve [port] [dir]   — start HTTP server\n");
    printf("  serve restore        — restore last server session\n\n");

    vprint(COL_CYAN,"[Encoding]\n");
    printf("  b64enc, b64dec, hexenc/hex, hexdec/unhex\n");
    printf("  rot13, urlenc, urldec\n\n");

    vprint(COL_CYAN,"[Text & Utils]\n");
    printf("  upper, lower, rev, guid [N], calc <expr>\n");
    printf("  wc, timer, typewriter [fast|slow], clock, lipsum [N]\n");
    printf("  clipcopy, clippaste\n\n");

    vprint(COL_CYAN,"[Fun]\n");
    printf("  matrix [fast|slow|1-10]   — rainbow, configurable\n\n");

    vprint(COL_CYAN,"[AI]\n");
    printf("  chatbot/ai           — chat with AI (uses ai_provider from config)\n");
    printf("  ollama <sub>         — status, list, pull, recommended, set-default\n\n");

    vprint(COL_CYAN,"[Setup & Config]\n");
    printf("  setup                — first-run wizard\n");
    printf("  setup reset          — wipe and redo config\n");
    printf("  config show/set/clear/path\n\n");

    vprint(COL_CYAN,"[History]\n");
    printf("  history [search]     — view history, optional search term\n\n");

    vprint(COL_CYAN,"[Kill Switch]\n");
    printf("  killswitch status/set-pin/arm/trigger/disarm\n");
    printf("  ks                   — alias\n\n");

    vprint(COL_YELLOW,"Ctrl+C stops long-running commands.\n");
    vprint(COL_DIM,"Config: %s\n\n",g_config_path);
}

/* ================================================================
   COMMAND TABLE
   ================================================================ */
typedef void (*CmdFn)(const char*);
typedef struct { const char *name; CmdFn fn; } Command;

static Command COMMANDS[] = {
    /* Core */
    {"help",cmd_help},{"h",cmd_help},{"cls",cmd_cls},{"clear",cmd_cls},
    {"version",cmd_version},{"about",cmd_version},{"echo",cmd_echo},
    /* Time */
    {"time",cmd_time_cmd},{"date",cmd_date_cmd},
    {"timer",cmd_timer},{"clock",cmd_clock_anim},
    /* System */
    {"sysinfo",cmd_sysinfo},{"uptime",cmd_uptime},
    {"whoami",cmd_whoami},{"whereami",cmd_whereami},
    {"env",cmd_env},{"storage",cmd_storage},
    {"processes",cmd_processes},{"ps",cmd_processes},
    {"kill",cmd_kill_proc},{"isadmin",cmd_isadmin},{"battery",cmd_battery},
    {"shutdown",cmd_shutdown},{"restart",cmd_restart},
    {"lock",cmd_lock},{"logoff",cmd_logoff},
    {"hibernate",cmd_hibernate},{"suspend",cmd_suspend},
    /* Files */
    {"ls",cmd_ls},{"dir",cmd_ls},{"cd",cmd_cd},
    {"cat",cmd_cat},{"type",cmd_cat},
    {"mkdir",cmd_mkdir_cmd},{"rmdir",cmd_rmdir_cmd},
    {"copy",cmd_copy},{"cp",cmd_copy},
    {"move",cmd_move},{"mv",cmd_move},{"rename",cmd_move},
    {"del",cmd_delete},{"rm",cmd_delete},
    {"touch",cmd_touch},{"find",cmd_find},{"tree",cmd_tree},
    {"head",cmd_head},{"first",cmd_head},
    {"tail",cmd_tail},{"last",cmd_tail},
    {"wc",cmd_wc},{"count",cmd_wc},
    {"filesize",cmd_filesize},{"size",cmd_filesize},
    {"compress",cmd_compress},{"zip",cmd_compress},
    {"extract",cmd_extract},{"unzip",cmd_extract},
    {"hexplain",cmd_hexplain},
    /* Hashing */
    {"md5",cmd_md5},{"md",cmd_md5},
    {"sha256",cmd_sha256},{"sha",cmd_sha256},
    {"sha512",cmd_sha512},
    /* Network core */
    {"ping",cmd_ping},{"tracert",cmd_tracert},{"traceroute",cmd_tracert},
    {"ns",cmd_ns},{"nslookup",cmd_ns},
    {"netstat",cmd_netstat},{"arp",cmd_arp},{"hostname",cmd_hostname},
    {"dnsflush",cmd_dnsflush},{"myip",cmd_myip},
    {"get-ip",cmd_get_ip},{"ip",cmd_get_ip},
    {"wifi",cmd_wifi},{"netinfo",cmd_netinfo},{"netmon",cmd_netmon},
    {"blocksite",cmd_blocksite},{"block",cmd_blocksite},
    {"unblocksite",cmd_unblocksite},{"unblock",cmd_unblocksite},
    /* Network advanced */
    {"portscan",cmd_portscan},{"pscan",cmd_portscan},
    {"tcping",cmd_tcping},{"whois",cmd_whois},{"wh",cmd_whois},
    {"geoip",cmd_geoip},{"portinfo",cmd_portinfo},
    {"localports",cmd_localports},{"banner",cmd_banner},
    {"reachable",cmd_reachable},{"ipcalc",cmd_ipcalc},{"rdns",cmd_rdns},
    {"dnscheck",cmd_dnscheck},{"interfaces",cmd_interfaces},
    {"routetable",cmd_routetable},
    /* Server */
    {"serve",cmd_serve},{"serve-restore",cmd_serve_restore},
    /* Encoding */
    {"b64enc",cmd_b64enc},{"b64e",cmd_b64enc},
    {"b64dec",cmd_b64dec},{"b64d",cmd_b64dec},
    {"hexenc",cmd_hexenc},{"hex",cmd_hexenc},
    {"hexdec",cmd_hexdec},{"unhex",cmd_hexdec},
    {"rot13",cmd_rot13},{"rot",cmd_rot13},
    {"urlenc",cmd_urlenc},{"urlencode",cmd_urlenc},
    {"urldec",cmd_urldec},{"urldecode",cmd_urldec},
    /* Text */
    {"upper",cmd_upper},{"toupper",cmd_upper},
    {"lower",cmd_lower},{"tolower",cmd_lower},
    {"rev",cmd_rev},{"reverse",cmd_rev},
    {"guid",cmd_guid},{"uuid",cmd_guid},
    {"calc",cmd_calcpy},{"typewriter",cmd_typewriter},
    {"lipsum",cmd_lipsum},{"lorem",cmd_lipsum},
    /* Clipboard */
    {"clipcopy",cmd_clipcopy},{"clippaste",cmd_clippaste},
    /* Fun */
    {"matrix",cmd_matrix},
    /* History */
    {"history",cmd_history},
    /* Config */
    {"config",cmd_config},{"cfg",cmd_config},{"settings",cmd_config},
    /* Kill switch */
    {"killswitch",cmd_killswitch},{"ks",cmd_killswitch},
    /* AI */
    {"chatbot",cmd_chatbot},{"ai",cmd_chatbot},
    {"ollama",cmd_ollama},
    /* Setup */
    {"setup",cmd_setup},
    {NULL,NULL}
};

static void dispatch(const char *verb, const char *args) {
    char v[256]; strncpy(v,verb,255); str_lower(v);
    for(int i=0;COMMANDS[i].name;i++){
        if(!strcmp(COMMANDS[i].name,v)){
            COMMANDS[i].fn(args?args:"");
            return;
        }
    }
    vprint(COL_RED,"  '%s' is not recognized. Type 'help'.\n",verb);
}

/* ================================================================
   MAIN
   ================================================================ */
#ifdef VENOM_WINDOWS
static void enable_vt(void){
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD m=0; GetConsoleMode(h,&m);
    SetConsoleMode(h,m|0x0004);
}
#endif

int main(void) {
#ifdef VENOM_WINDOWS
    enable_vt();
    SetConsoleOutputCP(CP_UTF8);
#else
    signal(SIGINT,sig_handler);
#endif
    net_init();

    /* ---- Setup paths ---- */
#ifdef VENOM_WINDOWS
    GetCurrentDirectoryA(sizeof(g_cwd),g_cwd);
    {
        char tmp[MAX_PATH_LEN]; GetTempPathA(sizeof(tmp),tmp);
        snprintf(g_hist_path,sizeof(g_hist_path),"%svenom_history.txt",tmp);
        char ad[MAX_PATH_LEN];
        if(SUCCEEDED(SHGetFolderPathA(NULL,CSIDL_APPDATA,NULL,0,ad))){
            char cd[MAX_PATH_LEN]; snprintf(cd,sizeof(cd),"%s\\venom_console",ad);
            CreateDirectoryA(cd,NULL);
            snprintf(g_config_path,sizeof(g_config_path),"%s\\venom.cfg",cd);
        } else {
            snprintf(g_config_path,sizeof(g_config_path),"%s\\venom.cfg",g_cwd);
        }
    }
#else
    getcwd(g_cwd,sizeof(g_cwd));
    {
        const char *tmp=getenv("TMPDIR"); if(!tmp) tmp="/tmp";
        snprintf(g_hist_path,sizeof(g_hist_path),"%s/venom_history.txt",tmp);
        const char *home=getenv("HOME");
        if(home){
            char cd[MAX_PATH_LEN]; snprintf(cd,sizeof(cd),"%s/.config/venom_console",home);
            mkdir(cd,0700);
            snprintf(g_config_path,sizeof(g_config_path),"%s/venom.cfg",cd);
        } else {
            snprintf(g_config_path,sizeof(g_config_path),"%s/venom.cfg",tmp);
        }
    }
#endif

    config_load();

    /* ---- Kill switch check ---- */
    if(killswitch_triggered()){
        fprintf(stderr,
            "\n*** venom.console is LOCKED ***\n"
            "The kill switch has been triggered.\n"
            "Remove '%s' from working/config dir,\n"
            "then run: killswitch disarm\n\n",
            KILLSWITCH_FILE);
#ifdef VENOM_WINDOWS
        Sleep(3000);
#endif
        return 1;
    }

    /* ---- Server autostart ---- */
    if(g_cfg.serve_autostart){
        FILE *sf=fopen(SERVE_STATE_FILE,"r");
        if(sf){
            fclose(sf);
            vprint(COL_YELLOW,
                "  [!] Server was running — use 'serve restore' to bring it back.\n\n");
        }
    }

    /* ---- Banner ---- */
    system(CLEAR_CMD);
    vprint(COL_GREEN,
"  ██╗   ██╗███████╗███╗   ██╗ ██████╗ ███╗   ███╗\n"
"  ██║   ██║██╔════╝████╗  ██║██╔═══██╗████╗ ████║\n"
"  ██║   ██║█████╗  ██╔██╗ ██║██║   ██║██╔████╔██║\n"
"  ╚██╗ ██╔╝██╔══╝  ██║╚██╗██║██║   ██║██║╚██╔╝██║\n"
"   ╚████╔╝ ███████╗██║ ╚████║╚██████╔╝██║ ╚═╝ ██║\n"
"    ╚═══╝  ╚══════╝╚═╝  ╚═══╝ ╚═════╝ ╚═╝     ╚═╝\n");
    vprint(COL_CYAN,"                         console  ");
    vprint(COL_DIM,"%s\n\n",VENOM_VERSION);
    vprint(COL_YELLOW,"  Type 'help' for commands.  'setup' for first-run wizard.\n\n");

    /* ---- Main loop ---- */
    char line[VENOM_MAX_INPUT];
    while(1){
        /* Prompt */
        vprint(COL_BLUE,"venom");
        vprint(COL_DIM,".");
        vprint(COL_CYAN,"console");
        vprint(COL_WHITE," > ");
        fflush(stdout);

        if(!fgets(line,sizeof(line),stdin)){printf("\nEOF.\n");break;}
        str_trim(line);
        if(!*line) continue;
        history_save(line);

        if(!strcmp(line,"exit")||!strcmp(line,"q")){
            vprint(COL_CYAN,"  Goodbye.\n"); break;
        }

        char verb[256]; char *rest;
        char *sp=strchr(line,' ');
        if(sp){
            size_t vl=(size_t)(sp-line);
            if(vl>=sizeof(verb)) vl=sizeof(verb)-1;
            strncpy(verb,line,vl); verb[vl]='\0';
            rest=sp+1; while(*rest==' ') rest++;
        } else {
            strncpy(verb,line,sizeof(verb)-1);
            verb[sizeof(verb)-1]='\0';
            rest=(char*)"";
        }
        dispatch(verb,rest);
        printf("\n");
    }

#ifdef VENOM_WINDOWS
    WSACleanup();
    printf("Press Enter to exit...");
    getchar();
#endif
    return 0;
}
