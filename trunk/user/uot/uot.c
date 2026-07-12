#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <getopt.h> 
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <linux/netfilter_ipv4.h>
#include <pthread.h>
#include <sys/uio.h>
#include <signal.h>

#define BUFFER_SIZE        2048
#define MAX_EVENTS         64
#define MAX_SESSIONS       4096
#define TCP_HASH_BUCKETS    512
#define UDP_TIMEOUT_SEC     40
#define TCP_IDLE_TIMEOUT    600
#define MAX_EARLY_PACKETS   512 
#define PKT_BUF_SIZE      2048  

typedef enum {
    CONN_STATE_CONNECTING,
    CONN_STATE_SOCKS5_AUTH,
    CONN_STATE_HANDSHAKING,
    CONN_STATE_STREAMING
} ConnState;

#pragma pack(push, 1)
typedef struct {
    uint8_t  atyp;       
    uint32_t dest_ip;    
    uint16_t dest_port;  
    uint16_t length;     
} UotHeaderV1;
#pragma pack(pop)

struct TcpNode;

typedef struct EpollContext {
    int fd;
    int dead;               
    struct TcpNode *node_ptr;
    uint8_t buf[BUFFER_SIZE * 2];
    size_t buf_len;
} EpollContext;

typedef struct TcpNode {
    uint32_t dest_ip;
    uint16_t dest_port;
    int tcp_fd;
    ConnState state;
    uint64_t last_used;
    EpollContext ctx_ptr; 
    uint8_t *early_buf;
    size_t early_len;
    struct TcpNode *next;
} TcpNode;

typedef struct {
    TcpNode *head;
    pthread_rwlock_t rwlock;
} TcpBucket;

typedef struct UdpNode {
    uint32_t client_ip;
    uint16_t client_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint64_t last_active;
    struct UdpNode *next;
} UdpNode;

typedef struct {
    UdpNode *head;
    pthread_rwlock_t rwlock;
} UdpBucket;

typedef struct {
    int tproxy_port;          
    char ss_redir_ip[64];     
    int ss_redir_port;
    struct sockaddr_in ss_redir_sockaddr;
} EngineConfig;

typedef struct {
    uint8_t data[PKT_BUF_SIZE];
    int in_use;
} EarlySlot;

EngineConfig g_config; 
int global_epoll_fd = -1;
UdpBucket udp_hash_table[MAX_SESSIONS];
TcpBucket tcp_hash_table[TCP_HASH_BUCKETS];
EarlySlot g_early_pool[MAX_EARLY_PACKETS];
pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER; 

static volatile sig_atomic_t g_shutdown_requested = 0;

void sig_handler(int sig) {
    (void)sig;
    g_shutdown_requested = 1;
}

uint8_t* alloc_early_slot() {
    pthread_mutex_lock(&g_pool_lock);
    for (int i = 0; i < MAX_EARLY_PACKETS; i++) {
        if (!g_early_pool[i].in_use) {
            g_early_pool[i].in_use = 1;
            pthread_mutex_unlock(&g_pool_lock);
            return g_early_pool[i].data;
        }
    }
    pthread_mutex_unlock(&g_pool_lock);
    return NULL; 
}

void free_early_slot(uint8_t *ptr) {
    int idx = ((uintptr_t)ptr - (uintptr_t)g_early_pool) / sizeof(EarlySlot);
    if (idx >= 0 && idx < MAX_EARLY_PACKETS) {
        pthread_mutex_lock(&g_pool_lock);
        g_early_pool[idx].in_use = 0;
        pthread_mutex_unlock(&g_pool_lock);
    }
}

uint32_t hash_fnv1a(const void *key, size_t len) {
    const uint8_t *data = (const uint8_t *)key;
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < len; i++) { 
        hash ^= data[i]; 
        hash *= 16777619U;
    }
    return hash;
}

uint64_t get_mono_time() {
    struct timespec ts; 
    clock_gettime(CLOCK_MONOTONIC, &ts); 
    return (uint64_t)ts.tv_sec;
}

void register_udp_session(uint32_t c_ip, uint16_t c_port, uint32_t r_ip, uint16_t r_port) {
    uint64_t key = ((uint64_t)r_ip << 16) | r_port;
    uint32_t idx = hash_fnv1a(&key, sizeof(key)) % MAX_SESSIONS;
    pthread_rwlock_wrlock(&udp_hash_table[idx].rwlock);
    UdpNode *curr = udp_hash_table[idx].head;
    while (curr) {
        if (curr->remote_ip == r_ip && curr->remote_port == r_port) {
            curr->client_ip = c_ip;
            curr->client_port = c_port;
            curr->last_active = get_mono_time(); 
            pthread_rwlock_unlock(&udp_hash_table[idx].rwlock); 
            return;
        }
        curr = curr->next;
    }
    UdpNode *new_node = malloc(sizeof(UdpNode));
    if (!new_node) {
        pthread_rwlock_unlock(&udp_hash_table[idx].rwlock);
        return;
    }
    new_node->client_ip = c_ip; 
    new_node->client_port = c_port;
    new_node->remote_ip = r_ip; 
    new_node->remote_port = r_port;
    new_node->last_active = get_mono_time();
    new_node->next = udp_hash_table[idx].head; 
    udp_hash_table[idx].head = new_node;
    pthread_rwlock_unlock(&udp_hash_table[idx].rwlock);
}

int lookup_udp_session(uint32_t r_ip, uint16_t r_port, uint32_t *c_ip, uint16_t *c_port) {
    uint64_t key = ((uint64_t)r_ip << 16) | r_port;
    uint32_t idx = hash_fnv1a(&key, sizeof(key)) % MAX_SESSIONS;
    pthread_rwlock_rdlock(&udp_hash_table[idx].rwlock);
    UdpNode *curr = udp_hash_table[idx].head;
    while (curr) {
        if (curr->remote_ip == r_ip && curr->remote_port == r_port) {
            *c_ip = curr->client_ip;
            *c_port = curr->client_port;
            curr->last_active = get_mono_time(); 
            pthread_rwlock_unlock(&udp_hash_table[idx].rwlock); 
            return 0;
        }
        curr = curr->next;
    }
    pthread_rwlock_unlock(&udp_hash_table[idx].rwlock); 
    return -1;
}

void purge_dead_tunnel_in_reactor(EpollContext *ctx) {
    if (!ctx) return;
    int fd = ctx->fd;
    for (int i = 0; i < TCP_HASH_BUCKETS; i++) {
        pthread_rwlock_wrlock(&tcp_hash_table[i].rwlock);
        TcpNode *curr = tcp_hash_table[i].head; 
        TcpNode *prev = NULL;
        while (curr) {
            if (curr->tcp_fd == fd) {
                if (prev == NULL) tcp_hash_table[i].head = curr->next;
                else prev->next = curr->next;
                if (curr->early_buf) {
                    free_early_slot(curr->early_buf);
                }
                free(curr);
                break;
            }
            prev = curr; 
            curr = curr->next;
        }
        pthread_rwlock_unlock(&tcp_hash_table[i].rwlock);
    }
    
    epoll_ctl(global_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    free(ctx); 
}

int get_uot_tunnel_via_sslocal(uint32_t ip, uint16_t port, uint8_t *pkt_ptr, size_t real_udp_len) {
    uint64_t key = ((uint64_t)ip << 16) | port;
    uint32_t idx = hash_fnv1a(&key, sizeof(key)) % TCP_HASH_BUCKETS;

    pthread_rwlock_rdlock(&tcp_hash_table[idx].rwlock);
    TcpNode *curr = tcp_hash_table[idx].head;
    while (curr) {
        if (curr->dest_ip == ip && curr->dest_port == port && curr->tcp_fd != -1) {
            if (curr->state == CONN_STATE_STREAMING && !curr->ctx_ptr.dead) { 
                int fd = curr->tcp_fd;
                curr->last_used = get_mono_time();
                pthread_rwlock_unlock(&tcp_hash_table[idx].rwlock); 
                return fd;
            }
        }
        curr = curr->next;
    }
    pthread_rwlock_unlock(&tcp_hash_table[idx].rwlock);

    int new_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0); 
    if (new_fd < 0) return -1;

    int opt_on = 1;
    setsockopt(new_fd, IPPROTO_TCP, TCP_NODELAY, &opt_on, sizeof(opt_on));

    int keepidle = 30, keepintvl = 5, keepcnt = 3;
    setsockopt(new_fd, SOL_SOCKET, SO_KEEPALIVE, &opt_on, sizeof(opt_on));
    setsockopt(new_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(new_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(new_fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));

    struct sockaddr_in sslocal_addr = g_config.ss_redir_sockaddr;
    int ret = connect(new_fd, (struct sockaddr *)&sslocal_addr, sizeof(sslocal_addr));

    pthread_rwlock_wrlock(&tcp_hash_table[idx].rwlock);
    curr = tcp_hash_table[idx].head;
    while (curr) {
        if (curr->dest_ip == ip && curr->dest_port == port && curr->tcp_fd != -1 && !curr->ctx_ptr.dead) {
            if (curr->state == CONN_STATE_STREAMING) {
                int fd = curr->tcp_fd;
                curr->last_used = get_mono_time();
                pthread_rwlock_unlock(&tcp_hash_table[idx].rwlock);
                close(new_fd); 
                return fd;
            }

            if (curr->state == CONN_STATE_CONNECTING || curr->state == CONN_STATE_SOCKS5_AUTH) {
                size_t this_pkt_total = 2 + real_udp_len;
                if (curr->early_buf && (curr->early_len + this_pkt_total < PKT_BUF_SIZE)) {
                    uint8_t *append_ptr = curr->early_buf + curr->early_len;
                    uint16_t u16_len = htons((uint16_t)real_udp_len);
                    memcpy(append_ptr, &u16_len, 2);
                    memcpy(append_ptr + 2, pkt_ptr + 2, real_udp_len);
                    curr->early_len += this_pkt_total;
                }
            }

            pthread_rwlock_unlock(&tcp_hash_table[idx].rwlock);
            close(new_fd);
            return 0;
        }
        curr = curr->next;
    }

    TcpNode *new_node = calloc(1, sizeof(TcpNode));
    if (!new_node) {
        pthread_rwlock_unlock(&tcp_hash_table[idx].rwlock);
        close(new_fd);
        return -1;
    }

    new_node->dest_ip = ip; 
    new_node->dest_port = port; 
    new_node->tcp_fd = new_fd;
    new_node->last_used = get_mono_time();
    new_node->early_buf = pkt_ptr;
    new_node->early_len = 2 + real_udp_len; 

    new_node->ctx_ptr.fd = new_fd;
    new_node->ctx_ptr.node_ptr = new_node;

    if (ret < 0 && errno == EINPROGRESS) {
        new_node->state = CONN_STATE_CONNECTING;
    } else if (ret == 0) {
        new_node->state = CONN_STATE_SOCKS5_AUTH;
        send(new_fd, "\x05\x01\x00", 3, MSG_NOSIGNAL);
    } else {
        pthread_rwlock_unlock(&tcp_hash_table[idx].rwlock); 
        close(new_fd); 
        free(new_node); 
        return -1;
    }
    
    new_node->next = tcp_hash_table[idx].head; 
    tcp_hash_table[idx].head = new_node;
    pthread_rwlock_unlock(&tcp_hash_table[idx].rwlock);

    struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.ptr = &(new_node->ctx_ptr)};
    if (epoll_ctl(global_epoll_fd, EPOLL_CTL_ADD, new_fd, &ev) < 0)
        new_node->ctx_ptr.dead = 1;

    return -1;
}

void tproxy_native_delivery(UotHeaderV1 *header, const uint8_t *payload, uint16_t len) {
    uint32_t c_ip;
    uint16_t c_port;
    if (lookup_udp_session(header->dest_ip, header->dest_port, &c_ip, &c_port) == 0) {
        int tx_udp = socket(AF_INET, SOCK_DGRAM, 0);
        if (tx_udp < 0) return;
        int opt = 1;
        setsockopt(tx_udp, SOL_IP, IP_TRANSPARENT, &opt, sizeof(opt));
        setsockopt(tx_udp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(tx_udp, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        
        struct sockaddr_in fake_src = { .sin_family = AF_INET, .sin_addr.s_addr = header->dest_ip, .sin_port = header->dest_port };
        if (bind(tx_udp, (struct sockaddr *)&fake_src, sizeof(fake_src)) == 0) {
            struct sockaddr_in dst = { .sin_family = AF_INET, .sin_addr.s_addr = c_ip, .sin_port = c_port };
            sendto(tx_udp, payload, len, 0, (struct sockaddr *)&dst, sizeof(dst));
        }
        close(tx_udp);
    }
}

void process_reactive_stream(EpollContext *ctx) {
    TcpNode *node = ctx->node_ptr;
    while (1) {
        if (ctx->dead) goto dead;
        ssize_t n = recv(ctx->fd, ctx->buf + ctx->buf_len, sizeof(ctx->buf) - ctx->buf_len, 0);
        if (n > 0) {
            ctx->buf_len += n;
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            goto dead;
        } else {
            goto dead;
        }

        if (node->state == CONN_STATE_SOCKS5_AUTH) {
            if (ctx->buf_len >= 2) {
                if (ctx->buf[0] == 0x05 && ctx->buf[1] == 0x00) {
                    node->state = CONN_STATE_HANDSHAKING;
                    uint8_t cmd[] = "\x05\x01\x00\x03\x14sp.udp-over-tcp.arpa\x00\x00";
                    send(ctx->fd, cmd, sizeof(cmd) - 1, MSG_NOSIGNAL);
                    memmove(ctx->buf, ctx->buf + 2, ctx->buf_len - 2);
                    ctx->buf_len -= 2;
                } else {
                    goto dead;
                }
            } else {
                break;
            }
        }

        if (node->state == CONN_STATE_HANDSHAKING) {
            if (ctx->buf_len >= 10) {
                size_t resp_len = 10;
                if (ctx->buf[3] == 0x04) resp_len = 22;
                else if (ctx->buf[3] == 0x03) resp_len = 7 + ctx->buf[4];
                if (ctx->buf_len < resp_len) break;

                if (ctx->buf[0] == 0x05 && ctx->buf[1] == 0x00) {
                    uint8_t *stolen_buf = NULL;
                    size_t stolen_len = 0;
                    uint32_t stolen_ip = 0;
                    uint16_t stolen_port = 0;
    
                    uint64_t hash_key = ((uint64_t)node->dest_ip << 16) | node->dest_port;
                    uint32_t bucket_idx = hash_fnv1a(&hash_key, sizeof(hash_key)) % TCP_HASH_BUCKETS;
                    pthread_rwlock_wrlock(&tcp_hash_table[bucket_idx].rwlock);

                    node->state = CONN_STATE_STREAMING;

                    if (node->early_buf && node->early_len > 0) {
                        stolen_buf = node->early_buf;
                        stolen_len = node->early_len;
                        stolen_ip = node->dest_ip;
                        stolen_port = node->dest_port;
                        node->early_buf = NULL;
                        node->early_len = 0;
                    }
                    pthread_rwlock_unlock(&tcp_hash_table[bucket_idx].rwlock);

                    if (stolen_buf && stolen_len > 0) { 
                        size_t offset = 0;
                        while (offset < stolen_len) {
                            uint16_t u16_len;
                            memcpy(&u16_len, stolen_buf + offset, 2);
                            uint16_t real_pkt_len = ntohs(u16_len);

                            uint8_t uot_header[9];
                            uot_header[0] = 0x00; 
                            memcpy(&uot_header[1], &stolen_ip, 4);
                            memcpy(&uot_header[5], &stolen_port, 2);
                            memcpy(&uot_header[7], &u16_len, 2);         

                            struct iovec tx_iov[2];
                            tx_iov[0].iov_base = uot_header;
                            tx_iov[0].iov_len = 9;
                            tx_iov[1].iov_base = stolen_buf + offset + 2; 
                            tx_iov[1].iov_len = real_pkt_len;

                            writev(ctx->fd, tx_iov, 2);
                            offset += (2 + real_pkt_len);
                        }
                        free_early_slot(stolen_buf);
                    }

                    struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.ptr = ctx };
                    epoll_ctl(global_epoll_fd, EPOLL_CTL_MOD, ctx->fd, &ev);

                    memmove(ctx->buf, ctx->buf + resp_len, ctx->buf_len - resp_len);
                    ctx->buf_len -= resp_len;
                } else {
                    goto dead;
                }
            } else {
                break;
            }
        }

        if (node->state == CONN_STATE_STREAMING) {
            const size_t UOT_V1_MIN_HEADER = 9;
            while (ctx->buf_len >= UOT_V1_MIN_HEADER) {
                uint8_t atyp = ctx->buf[0];
                if (atyp) { 
                    goto dead;
                }

                uint16_t data_len_net;
                memcpy(&data_len_net, &ctx->buf[7], 2);
                uint16_t payload_len = ntohs(data_len_net);

                if (payload_len > BUFFER_SIZE) {
                    goto dead;
                }

                size_t total_frame_len = UOT_V1_MIN_HEADER + payload_len;
                if (ctx->buf_len < total_frame_len) {
                    break;
                }

                UotHeaderV1 *fake_header_ptr = (UotHeaderV1 *)ctx->buf;
                if (payload_len > 0) {
                    tproxy_native_delivery(fake_header_ptr, ctx->buf + UOT_V1_MIN_HEADER, payload_len);
                }

                memmove(ctx->buf, ctx->buf + total_frame_len, ctx->buf_len - total_frame_len);
                ctx->buf_len -= total_frame_len;
            }
            break;
        }
    }
    return;

dead:
    purge_dead_tunnel_in_reactor(ctx);
}

void* epoll_reactor_thread(void *arg) {
    (void)arg; 
    struct epoll_event events[MAX_EVENTS];
    while (!g_shutdown_requested) {
        int nfds = epoll_wait(global_epoll_fd, events, MAX_EVENTS, 200);
        if (nfds < 0 && errno != EINTR) break;
        for (int i = 0; i < nfds; i++) {
            EpollContext *ctx = (EpollContext *)events[i].data.ptr;
            if (ctx->dead || (events[i].events & (EPOLLERR | EPOLLHUP))) { 
                purge_dead_tunnel_in_reactor(ctx);
                continue; 
            }
            
            if (events[i].events & EPOLLOUT) {
                if (ctx->node_ptr && ctx->node_ptr->state == CONN_STATE_CONNECTING) {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    if (getsockopt(ctx->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
                        purge_dead_tunnel_in_reactor(ctx);
                        continue;
                    }
                    ctx->node_ptr->state = CONN_STATE_SOCKS5_AUTH;
                    send(ctx->fd, "\x05\x01\x00", 3, MSG_NOSIGNAL);
                }
            }

            if (events[i].events & EPOLLIN) { 
                process_reactive_stream(ctx);
            }
        }
    }
    return NULL;
}

void *tproxy_inbound_loop(void *arg) {
    int tproxy_fd = *(int *)arg;
    struct sockaddr_in client_addr;
    struct sockaddr_in orig_dst; 
    uint8_t control_buf[CMSG_SPACE(sizeof(struct sockaddr_in))];

    while (!g_shutdown_requested) {
        uint8_t *pkt_ptr = alloc_early_slot();
        if (!pkt_ptr) {
            uint8_t drop_buf[128];
            struct iovec drop_iov = { .iov_base = drop_buf, .iov_len = sizeof(drop_buf) };
            struct msghdr drop_msg = { .msg_iov = &drop_iov, .msg_iovlen = 1 };
            recvmsg(tproxy_fd, &drop_msg, 0);
            continue;
        }

        struct iovec iov[1];
        iov[0].iov_base = pkt_ptr + 2;
        iov[0].iov_len = PKT_BUF_SIZE - 2;

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &client_addr;
        msg.msg_namelen = sizeof(client_addr);
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control_buf;
        msg.msg_controllen = sizeof(control_buf);

        ssize_t len = recvmsg(tproxy_fd, &msg, 0);
        if (len < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                free_early_slot(pkt_ptr);
                continue;
            }
            free_early_slot(pkt_ptr);
            break;
        }

        struct cmsghdr *cmsg;
        int found = 0;
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVORIGDSTADDR) {
                memcpy(&orig_dst, CMSG_DATA(cmsg), sizeof(struct sockaddr_in));
                found = 1;
                break;
            }
        }

        if (!found) {
            free_early_slot(pkt_ptr);
            continue;
        }

        register_udp_session(client_addr.sin_addr.s_addr, client_addr.sin_port, orig_dst.sin_addr.s_addr, orig_dst.sin_port);

        uint16_t u16_len = htons((uint16_t)len);
        memcpy(pkt_ptr, &u16_len, 2);

        uint32_t dest_ip = orig_dst.sin_addr.s_addr;
        uint16_t dest_port = orig_dst.sin_port;

        int tunnel_fd = get_uot_tunnel_via_sslocal(dest_ip, dest_port, pkt_ptr, len);

        if (tunnel_fd > 0) {
            uint8_t uot_header[9];
            uot_header[0] = 0x00; 
            memcpy(&uot_header[1], &dest_ip, 4);
            memcpy(&uot_header[5], &dest_port, 2);
            memcpy(&uot_header[7], &u16_len, 2);

            struct iovec tx_iov[2] = {
                { .iov_base = uot_header, .iov_len = 9 },
                { .iov_base = pkt_ptr + 2, .iov_len = len } 
            };

            writev(tunnel_fd, tx_iov, 2);
            free_early_slot(pkt_ptr);
        }
        else if (tunnel_fd == 0) {
            continue;
        }
        else {
            free_early_slot(pkt_ptr);
        }
    }
    return NULL;
}

void* double_drive_gc_daemon(void *arg) {
    (void)arg;
    while (!g_shutdown_requested) {
        for (int sleep_cnt = 0; sleep_cnt < 10 && !g_shutdown_requested; sleep_cnt++) {
            usleep(1000000); 
        }
        if (g_shutdown_requested) break;

        uint64_t now = get_mono_time();

        for (int i = 0; i < MAX_SESSIONS; i++) {
            pthread_rwlock_wrlock(&udp_hash_table[i].rwlock);
            UdpNode *curr = udp_hash_table[i].head; 
            UdpNode *prev = NULL;
            while (curr) {
                if (now - curr->last_active > UDP_TIMEOUT_SEC) {
                    UdpNode *tmp = curr;
                    if (prev == NULL) udp_hash_table[i].head = curr->next;
                    else prev->next = curr->next;
                    curr = curr->next;
                    free(tmp);
                } else {
                    prev = curr; 
                    curr = curr->next;
                }
            }
            pthread_rwlock_unlock(&udp_hash_table[i].rwlock);
        }

        for (int i = 0; i < TCP_HASH_BUCKETS; i++) {
            pthread_rwlock_wrlock(&tcp_hash_table[i].rwlock);
            TcpNode *curr = tcp_hash_table[i].head;
            while (curr) {
                if (curr->state == CONN_STATE_STREAMING && (now - curr->last_used > TCP_IDLE_TIMEOUT)) {
                    if (!curr->ctx_ptr.dead) {
                        curr->ctx_ptr.dead = 1;
                        shutdown(curr->tcp_fd, SHUT_RDWR);
                    }
                }
                curr = curr->next;
            }
            pthread_rwlock_unlock(&tcp_hash_table[i].rwlock);
        }
    }
    return NULL;
}

void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -l <port>    Local TPROXY listen port for UDP inbound (e.g., 10801)\n");
    printf("  -s <ip>      Local ss-redir TCP listen IP address (e.g., 127.0.0.1)\n");
    printf("  -p <port>    Local ss-redir TCP listen port (e.g., 10800)\n");
    printf("  -h           Show this help message\n");
}

int main(int argc, char *argv[]) {
    // 注册信号处理器，接收到 SIGINT/SIGTERM 触发优雅退出状态机
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    g_config.tproxy_port = 0;
    g_config.ss_redir_port = 0;
    strcpy(g_config.ss_redir_ip, "127.0.0.1");

    int opt;
    while ((opt = getopt(argc, argv, "l:s:p:h")) != -1) {
        switch (opt) {
            case 'l': g_config.tproxy_port = atoi(optarg); break;
            case 's': snprintf(g_config.ss_redir_ip, sizeof(g_config.ss_redir_ip), "%s", optarg); break;
            case 'p': g_config.ss_redir_port = atoi(optarg); break;
            case 'h':
            default:  print_usage(argv[0]); return 0;
        }
    }

    if (g_config.tproxy_port == 0 || g_config.ss_redir_port == 0 || strlen(g_config.ss_redir_ip) == 0) {
        print_usage(argv[0]);
        return 1;
    }

    g_config.ss_redir_sockaddr.sin_family = AF_INET;
    g_config.ss_redir_sockaddr.sin_port = htons(g_config.ss_redir_port);
    if (inet_pton(AF_INET, g_config.ss_redir_ip, &g_config.ss_redir_sockaddr.sin_addr) <= 0) {
        fprintf(stderr, "[FATAL] Invalid ss-redir target IP address: %s\n", g_config.ss_redir_ip);
        return 1;
    }

    // 🌟 启动日志
    printf("[System] Initializing TPROXY UoT Engine...\n");

    // 初始化哈希表与锁
    memset(udp_hash_table, 0, sizeof(udp_hash_table));
    memset(tcp_hash_table, 0, sizeof(tcp_hash_table));
    for (int i = 0; i < MAX_SESSIONS; i++) pthread_rwlock_init(&udp_hash_table[i].rwlock, NULL);
    for (int i = 0; i < TCP_HASH_BUCKETS; i++) pthread_rwlock_init(&tcp_hash_table[i].rwlock, NULL);

    if ((global_epoll_fd = epoll_create1(0)) < 0) {
        fprintf(stderr, "[FATAL] Epoll instance creation failed (errno: %d)\n", errno);
        return 1;
    }

    int tproxy_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (tproxy_fd < 0) {
        fprintf(stderr, "[FATAL] Create UDP socket failed (errno: %d)\n", errno);
        return 1;
    }

    int flag = 1;
    setsockopt(tproxy_fd, SOL_IP, IP_TRANSPARENT, &flag, sizeof(flag));
    setsockopt(tproxy_fd, SOL_IP, IP_RECVORIGDSTADDR, &flag, sizeof(flag));

    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(g_config.tproxy_port)
    };

    if (bind(tproxy_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        fprintf(stderr, "[FATAL] Bind TPROXY listener port %d failed (errno: %d)\n", g_config.tproxy_port, errno);
        close(tproxy_fd);
        return 1;
    }

    int *pass_fd = malloc(sizeof(int));
    if (!pass_fd) { 
        close(tproxy_fd); 
        return 1; 
    }
    *pass_fd = tproxy_fd;

    // 🌟 异步线程组启动成功日志
    pthread_t r_tid, i_tid, g_tid;
    pthread_create(&r_tid, NULL, epoll_reactor_thread, NULL);
    pthread_create(&i_tid, NULL, tproxy_inbound_loop, pass_fd);
    pthread_create(&g_tid, NULL, double_drive_gc_daemon, NULL);

    printf("[System] Core activated successfully.\n");
    printf("   -> Inbound TPROXY Port : %d\n", g_config.tproxy_port);
    printf("   -> Outbound SOCKS5 Host: %s:%d\n", g_config.ss_redir_ip, g_config.ss_redir_port);
    printf("[System] Proxy running smoothly. Press Ctrl+C to terminate cleanly.\n");

    // 主线程挂起等待终止信号
    while (!g_shutdown_requested) {
        pause();
    }

    // 🌟 优雅退出启动日志
    printf("\n[Shutdown] Termination signal captured. Safely stopping core workers...\n");

    // 通知并终止子线程
    pthread_cancel(i_tid);
    pthread_cancel(r_tid);
    pthread_cancel(g_tid);

    pthread_join(i_tid, NULL);
    pthread_join(r_tid, NULL);
    pthread_join(g_tid, NULL);

    // 关闭核心套接字与文件描述符
    close(tproxy_fd);
    free(pass_fd);
    if (global_epoll_fd != -1) {
        close(global_epoll_fd);
    }

    // 清理并销毁全局 UDP 会话映射表
    for (int i = 0; i < MAX_SESSIONS; i++) {
        pthread_rwlock_wrlock(&udp_hash_table[i].rwlock);
        UdpNode *curr = udp_hash_table[i].head;
        while (curr) {
            UdpNode *next = curr->next;
            free(curr);
            curr = next;
        }
        udp_hash_table[i].head = NULL;
        pthread_rwlock_unlock(&udp_hash_table[i].rwlock);
        pthread_rwlock_destroy(&udp_hash_table[i].rwlock);
    }

    // 清理并销毁全局 TCP 隧道数据表（联动清除 early_buf）
    for (int i = 0; i < TCP_HASH_BUCKETS; i++) {
        pthread_rwlock_wrlock(&tcp_hash_table[i].rwlock);
        TcpNode *curr = tcp_hash_table[i].head;
        while (curr) {
            TcpNode *next = curr->next;
            if (curr->tcp_fd >= 0) {
                close(curr->tcp_fd);
            }
            if (curr->early_buf) {
                free_early_slot(curr->early_buf);
            }
            free(curr);
            curr = next;
        }
        tcp_hash_table[i].head = NULL;
        pthread_rwlock_unlock(&tcp_hash_table[i].rwlock);
        pthread_rwlock_destroy(&tcp_hash_table[i].rwlock);
    }

    pthread_mutex_destroy(&g_pool_lock);
    
    // 🌟 最终退出确认日志
    printf("[Shutdown] All active context and structures cleared. Exit code: 0.\n");
    return 0;
}
