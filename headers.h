#include <aio.h>
#include <arpa/inet.h>
#include <assert.h>
#include <complex.h>
#include <cpio.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <float.h>
#include <fmtmsg.h>
#include <fnmatch.h>
#include <ftw.h>
#include <glob.h>
#include <grp.h>
#include <iconv.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <iso646.h>
#include <langinfo.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <monetary.h>
// #include <mqueue.h>
// #include <ndbm.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <nl_types.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <regex.h>
#include <sched.h>
#include <search.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
// #include <stropts.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <syslog.h>
#include <tar.h>
#include <termios.h>
#include <tgmath.h>
#include <time.h>
// #include <trace.h>
#include <ulimit.h>
#include <unistd.h>
#include <utime.h>
#include <utmpx.h>
#include <wchar.h>
#include <wctype.h>
#include <wordexp.h>

extern char naming_ip[16];

typedef struct ss {
    int idx;
    char ip[16];
    int port_for_ss;
    int port_for_client;
    int port_for_nm;
    char accessible_paths[4096];
} ss_t;

typedef struct client_req {
    int sync; // sync = 1 then it is synchronous
    char filename[100];
    char destpath[100];
    char srcpath[250];
    int clientsocket;
    char oper_name[50];
} client_req_t;

typedef struct client_response {
    char ip[16];
    int port;
} client_response_t;

enum ErrorCodes {
    ECS = 99,  // Error Connecting to Server
    FCF = 101, // Failed to create file
    FDF = 102, // failed to delete File
    FRF = 103, // Failed to Read File
    FWF = 104, // Failed to Write File
    II  = 108, // Invalid Input
    CFF = 109, // Failed to Copy File
    FNF = 404, // file not found
    RFM = 110, // error opening file stat
    FAE = 111,  // File already exists
    RFD = 112, // Read requested from directory
    PERM = 113, // Permission denied
    OK = 0
};

void logMsg(char *msg);
