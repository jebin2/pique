// pique-watch: remembers what was highlighted in each window, for pique-peek.
//
// Wayland has one highlighted-text buffer (the primary selection). It keeps
// text after it is unhighlighted, and a highlight in another window replaces
// it while the first window still shows its own. So this keeps the last
// highlight of each window, in memory only:
//
//   pique-watch                       the watcher (one per session; the
//                                     Hyprland block starts it)
//   pique-watch --selection-changed   run by wl-paste --watch on each new
//                                     highlight, with the text on stdin
//   pique-watch --forget ADDRESS      run by the Hyprland click hook: a plain
//                                     click unhighlighted that window
//
// pique-peek asks over $XDG_RUNTIME_DIR/pique-highlights, one request per
// connection: "COMMAND\nWINDOW\n" then the text for "set"; "get" answers
// with the text. Only the window addresses are written to disk
// ($XDG_RUNTIME_DIR/pique-primary, for the click hook); the text never is.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHAR_LIMIT 2000                 // characters kept from one highlight
#define BYTE_LIMIT (CHAR_LIMIT * 4)     // the most those take in UTF-8
#define REMEMBERED 32                   // windows whose last highlight is kept
#define ADDRESS_MAX 32

struct entry {
    char window[ADDRESS_MAX];
    char *text;                         // NULL: slot unused
    unsigned long used;                 // for dropping the oldest
};

static struct entry entries[REMEMBERED];
static unsigned long clock_tick;
static char owners_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static volatile sig_atomic_t child_gone;

static const char *runtime_dir(void) {
    const char *dir = getenv("XDG_RUNTIME_DIR");
    return dir && *dir ? dir : NULL;
}

static int runtime_path(char *out, size_t size, const char *name) {
    const char *dir = runtime_dir();
    if (!dir)
        return -1;
    int n = snprintf(out, size, "%s/%s", dir, name);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

static int valid_window(const char *w) {
    if (strncmp(w, "0x", 2) != 0 || !w[2] || strlen(w) >= ADDRESS_MAX)
        return 0;
    for (const char *p = w + 2; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
            return 0;
    return 1;
}

// Reads until EOF or limit bytes; returns the count, or -1.
static ssize_t read_all(int fd, char *buf, size_t limit) {
    size_t got = 0;
    while (got < limit) {
        ssize_t n = read(fd, buf + got, limit - got);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return got ? (ssize_t)got : -1;
        if (n == 0)
            break;
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static int write_all(int fd, const char *buf, size_t len) {
    while (len) {
        ssize_t n = write(fd, buf, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static int connect_unix(const char *path) {
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(addr.sun_path))
        return -1;
    strcpy(addr.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct timeval timeout = {.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// The focused window's address, from Hyprland's control socket.
static int active_window(char *out) {
    const char *dir = runtime_dir();
    const char *instance = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!dir || !instance)
        return -1;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    int n = snprintf(path, sizeof(path), "%s/hypr/%s/.socket.sock", dir, instance);
    if (n < 0 || (size_t)n >= sizeof(path))
        return -1;
    int fd = connect_unix(path);
    if (fd < 0)
        return -1;
    static const char request[] = "j/activewindow";
    char reply[16384];
    ssize_t got = -1;
    if (write_all(fd, request, sizeof(request) - 1) == 0)
        got = read_all(fd, reply, sizeof(reply) - 1);
    close(fd);
    if (got <= 0)
        return -1;
    reply[got] = '\0';
    const char *key = strstr(reply, "\"address\": \"");
    if (!key)
        return -1;
    key += strlen("\"address\": \"");
    const char *end = strchr(key, '"');
    if (!end || end - key >= ADDRESS_MAX)
        return -1;
    memcpy(out, key, (size_t)(end - key));
    out[end - key] = '\0';
    return valid_window(out) ? 0 : -1;
}

static struct entry *find(const char *window) {
    for (int i = 0; i < REMEMBERED; i++)
        if (entries[i].text && strcmp(entries[i].window, window) == 0)
            return &entries[i];
    return NULL;
}

static void drop(struct entry *e) {
    if (!e)
        return;
    free(e->text);
    e->text = NULL;
}

// The window list for the click hook, written atomically.
static void publish(void) {
    char tmp[sizeof(owners_path) + 4];
    snprintf(tmp, sizeof(tmp), "%s.tmp", owners_path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return;
    int ok = 1;
    for (int i = 0; i < REMEMBERED && ok; i++) {
        if (!entries[i].text)
            continue;
        ok = write_all(fd, entries[i].window, strlen(entries[i].window)) == 0 &&
             write_all(fd, "\n", 1) == 0;
    }
    close(fd);
    if (ok)
        rename(tmp, owners_path);
    else
        unlink(tmp);
}

// Trims ASCII whitespace and cuts to CHAR_LIMIT characters (UTF-8).
static char *clean_text(const char *text, size_t len) {
    while (len && (*text == ' ' || (*text >= '\t' && *text <= '\r'))) {
        text++;
        len--;
    }
    size_t chars = 0, cut = 0;
    while (cut < len) {
        if (((unsigned char)text[cut] & 0xC0) != 0x80 && ++chars > CHAR_LIMIT)
            break;
        cut++;
    }
    len = cut;
    while (len && (text[len - 1] == ' ' || (text[len - 1] >= '\t' && text[len - 1] <= '\r')))
        len--;
    if (!len)
        return NULL;
    char *copy = malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

static void remember(const char *window, const char *text, size_t len) {
    drop(find(window));
    char *copy = clean_text(text, len);
    if (copy) {
        struct entry *slot = NULL;
        for (int i = 0; i < REMEMBERED && !slot; i++)
            if (!entries[i].text)
                slot = &entries[i];
        if (!slot) {                    // all full: replace the oldest
            slot = &entries[0];
            for (int i = 1; i < REMEMBERED; i++)
                if (entries[i].used < slot->used)
                    slot = &entries[i];
            drop(slot);
        }
        strcpy(slot->window, window);
        slot->text = copy;
        slot->used = ++clock_tick;
    }
    publish();
}

static void serve(int conn) {
    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    if (getsockopt(conn, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) != 0 || cred.uid != getuid())
        return;
    struct timeval timeout = {.tv_sec = 1};
    setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(conn, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    static char buf[BYTE_LIMIT + 256];
    ssize_t got = read_all(conn, buf, sizeof(buf) - 1);
    if (got <= 0)
        return;
    buf[got] = '\0';
    char *command = buf;
    char *nl = memchr(command, '\n', (size_t)got);
    if (!nl)
        return;
    *nl = '\0';
    char *window = nl + 1;
    nl = memchr(window, '\n', (size_t)(buf + got - window));
    if (!nl)
        return;
    *nl = '\0';
    char *text = nl + 1;
    size_t text_len = (size_t)(buf + got - text);

    char active[ADDRESS_MAX];
    if (strcmp(command, "set") == 0 && !*window) {
        // From --selection-changed: the highlight came from the focused window.
        if (active_window(active) != 0)
            return;
        window = active;
    }
    if (!valid_window(window))
        return;

    if (strcmp(command, "get") == 0) {
        struct entry *e = find(window);
        if (e) {
            e->used = ++clock_tick;
            write_all(conn, e->text, strlen(e->text));
        }
    } else if (strcmp(command, "set") == 0) {
        remember(window, text, text_len);
    } else if (strcmp(command, "forget") == 0) {
        struct entry *e = find(window);
        if (e) {
            drop(e);
            publish();
        }
    }
}

static int ask(const char *command, const char *window, const char *payload, size_t len) {
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    if (runtime_path(path, sizeof(path), "pique-highlights") != 0)
        return 1;
    int fd = connect_unix(path);
    if (fd < 0)
        return 1;
    char head[64 + ADDRESS_MAX];
    int n = snprintf(head, sizeof(head), "%s\n%s\n", command, window);
    int ok = n > 0 && (size_t)n < sizeof(head) && write_all(fd, head, (size_t)n) == 0 &&
             (!len || write_all(fd, payload, len) == 0);
    close(fd);
    return ok ? 0 : 1;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static int selection_changed(void) {
    static char text[BYTE_LIMIT];
    ssize_t got = read_all(STDIN_FILENO, text, sizeof(text));
    // wl-paste also runs this once at start for the old highlight, whose
    // window is unknown: that one is not noted.
    const char *since = getenv("PIQUE_WATCH_SINCE");
    if (since && now() - strtod(since, NULL) < 0.5)
        return 0;
    return ask("set", "", text, got > 0 ? (size_t)got : 0);
}

static void on_child(int sig) {
    (void)sig;
    child_gone = 1;
}

static int watch(const char *self) {
    char lock_path[sizeof(owners_path)], sock_path[sizeof(owners_path)];
    if (runtime_path(lock_path, sizeof(lock_path), "pique-watch.lock") != 0 ||
        runtime_path(sock_path, sizeof(sock_path), "pique-highlights") != 0 ||
        runtime_path(owners_path, sizeof(owners_path), "pique-primary") != 0)
        return 1;
    umask(077);
    int lock = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock < 0)
        return 1;
    if (flock(lock, LOCK_EX | LOCK_NB) != 0)
        return 0;                       // a watcher already runs

    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strcpy(addr.sun_path, sock_path);
    unlink(sock_path);                  // holding the lock: any socket there is stale
    if (server < 0 || bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(server, 8) != 0)
        return 1;
    publish();

    struct sigaction sa = {.sa_handler = on_child};   // no SA_RESTART: poll wakes up
    sigaction(SIGCHLD, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    char since[32];
    snprintf(since, sizeof(since), "%.3f", now());
    pid_t child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);   // wl-paste goes when the watcher does
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0)
            dup2(null, STDIN_FILENO);
        setenv("PIQUE_WATCH_SINCE", since, 1);
        execlp("wl-paste", "wl-paste", "--primary", "--watch", self, "--selection-changed", (char *)NULL);
        _exit(127);
    }

    for (;;) {
        if (child_gone && waitpid(child, NULL, WNOHANG) == child)
            return 0;                   // without wl-paste nothing new arrives
        child_gone = 0;
        struct pollfd pfd = {.fd = server, .events = POLLIN};
        if (poll(&pfd, 1, 60000) <= 0)   // also rechecks the child now and then
            continue;
        int conn = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (conn < 0)
            continue;
        serve(conn);
        close(conn);
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--selection-changed") == 0)
        return selection_changed();
    if (argc == 3 && strcmp(argv[1], "--forget") == 0)
        return valid_window(argv[2]) ? ask("forget", argv[2], NULL, 0) : 1;
    if (argc == 1) {
        char self[4096];
        ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (n <= 0)
            return 1;
        self[n] = '\0';
        return watch(self);
    }
    fprintf(stderr, "usage: pique-watch [--selection-changed | --forget ADDRESS]\n");
    return 2;
}
