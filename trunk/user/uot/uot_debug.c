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

// 系统全局常量与限制
#define BUFFER_SIZE        2048
#define MAX_EVENTS         64
#define MAX_SESSIONS       4096
#define TCP_HASH_BUCKETS    512
#define UDP_TIMEOUT_SEC     40
#define TCP_IDLE_TIMEOUT    600
#define MAX_EARLY_PACKETS   512 // 最多同时允许 512 个连接处于冷启动暂存阶段
#define PKT_BUF_SIZE      2048  // 每个槽位固定分配 2KB，完美容纳任何 UDP 包 + 长度字段

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
    int dead;               // 核心修复：死亡标记位
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
pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER; // 轻量级互斥锁，或使用原子操作

void hex_dump(const char *label, const uint8_t *buf, size_t len) {
    printf("---- [HEX DUMP] %s (Len: %zu) ----\n", label, len);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0 || i == len - 1) {
            // 每 16 个字节换一行，顺便打印人类可读的 ASCII 字符
            size_t start = i - (i % 16);
            for (size_t j = len - 1 - i; j < 15 - (i % 16); j++) printf("   ");
            // 补齐空格
            printf(" | ");
            for (size_t j = start; j <= i; j++) {
                printf("%c", (buf[j] >= 32 && buf[j] <= 126) ? buf[j] : '.');
            }
            printf("\n");
        }
    }
    printf("----------------------------------------\n");
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
    return NULL; // 池子满了
}

void free_early_slot(uint8_t *ptr) {
    // 依据指针地址的偏移量，O(1) 反推索引，连遍历都不需要！
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

int make_nonblocking_and_keepalive(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    int opt = 1, idle = 5, intv = 2, cnt = 3;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
    setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &intv, sizeof(intv));
    setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
    return 0;
}

// ---------------- 模块一：双向会话哈希映射与查表 ----------------
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

// ---------------- 模块二：连接初始化与安全清理 ----------------
// 核心收拢释放：由 Reactor 线程在唯一安全时机调用
void purge_dead_tunnel_in_reactor(EpollContext *ctx) {
    if (!ctx) return;
    int fd = ctx->fd;
    // 从全局哈希表安全剥离
    for (int i = 0; i < TCP_HASH_BUCKETS; i++) {
        pthread_rwlock_wrlock(&tcp_hash_table[i].rwlock);
        TcpNode *curr = tcp_hash_table[i].head; 
        TcpNode *prev = NULL;
        while (curr) {
            if (curr->tcp_fd == fd) {
                if (prev == NULL) tcp_hash_table[i].head = curr->next;
                else prev->next = curr->next;
                printf("[ConnPool] Purged dead tunnel fd %d from hash table.\n", fd);
                if (curr->early_buf) {
                    free(curr->early_buf);
                }
                free(curr);
                // 此时安全释放散列表节点
                break;
            }
            prev = curr; 
            curr = curr->next;
        }
        pthread_rwlock_unlock(&tcp_hash_table[i].rwlock);
    }
    
    epoll_ctl(global_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    free(ctx); // 彻底销毁上下文
}

/**
 * @brief 高性能 ivo 化隧道获取/创建器
 * @param ip         拦截到的原始目的 IPv4 (网络字节序)
 * @param port       拦截到的原始目的 Port (网络字节序)
 * @param pkt_ptr    ivo 化直落了网卡数据的快速内存槽指针 (头部2字节已填长度)
 * @param real_udp_len 原始 UDP 数据的净荷长度
 * @return int       >0: 稳定老连接 fd，外面直接 writev 发射并归还槽位
 * ==0: 冷启动首包暂存成功，所有权已移交，外面直接 continue 绝不能释放槽位
 * <0: 系统级错误（如 socket 创建失败或内存耗尽）
 */
int get_uot_tunnel_via_sslocal(uint32_t ip, uint16_t port, uint8_t *pkt_ptr, size_t real_udp_len) {
    uint64_t key = ((uint64_t)ip << 16) | port;
    uint32_t idx = hash_fnv1a(&key, sizeof(key)) % TCP_HASH_BUCKETS;

    // --- 阶梯一：极速读锁尝试（99% 的稳定流走这里） ---
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

    //非阻塞 TCP 创建与三次握手发起
    int new_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0); // 原地非阻塞创建
    if (new_fd < 0) return -1;

    // 1. 开启 TCP_NODELAY 彻底消灭 40ms 延迟
    int opt_on = 1;
    setsockopt(new_fd, IPPROTO_TCP, TCP_NODELAY, &opt_on, sizeof(opt_on));

    //socket 层的 keepalive
    int keepidle = 30, keepintvl = 5, keepcnt = 3;
    setsockopt(new_fd, SOL_SOCKET, SO_KEEPALIVE, &opt_on, sizeof(opt_on));
    setsockopt(new_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(new_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(new_fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));

    struct sockaddr_in sslocal_addr = g_config.ss_redir_sockaddr;
    int ret = connect(new_fd, (struct sockaddr *)&sslocal_addr, sizeof(sslocal_addr));

    // --- 阶梯二：升级写锁，执行 Double-Check 严密防御 ---
    pthread_rwlock_wrlock(&tcp_hash_table[idx].rwlock);
    curr = tcp_hash_table[idx].head;
    while (curr) {
        if (curr->dest_ip == ip && curr->dest_port == port && curr->tcp_fd != -1 && !curr->ctx_ptr.dead) {
		// 场景 A：另外一个并发线程跑得快，隧道不仅建好，甚至已经 STREAMING 了
		if (curr->state == CONN_STATE_STREAMING) {
			int fd = curr->tcp_fd;
			curr->last_used = get_mono_time();
			pthread_rwlock_unlock(&tcp_hash_table[idx].rwlock);
			close(new_fd); // 丢弃自己刚刚创建的，用现成的
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
    new_node->early_len = 2 + real_udp_len; //完全体 ivo 零拷贝点

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

    // 🌟 核心优化：连接初始化阶段必须保持监听 EPOLLOUT，并在事件触发时进行验证
    struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.ptr = &(new_node->ctx_ptr)};
    if (epoll_ctl(global_epoll_fd, EPOLL_CTL_ADD, new_fd, &ev) < 0)
	    new_node->ctx_ptr.dead = 1;

    return -1;
}

// ---------------- 模块三：双向异步 Reactor 核心反应堆 ----------------
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
            printf("\n[REACTOR RECEIVE] TCP FD: %d, Recv Bytes: %ld, Total Buf: %zu, Current State: %d\n",
                   ctx->fd, (long)n, ctx->buf_len, node->state);
            hex_dump("TCP Buffer Raw", ctx->buf, ctx->buf_len);
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            printf("[Reactor] Socket error on fd %d, errno: %d\n", ctx->fd, errno);
            goto dead;
        } else {
            // n == 0 代表对端真正发送了 FIN 包断开连接
            printf("[Reactor] Remote connection gracefully closed by peer (FIN) on fd %d\n", ctx->fd);
            goto dead;
        }

        // 阶段一：处理 SOCKS5 认证回复
        if (node->state == CONN_STATE_SOCKS5_AUTH) {
            if (ctx->buf_len >= 2) {
                if (ctx->buf[0] == 0x05 && ctx->buf[1] == 0x00) {
                    printf("[DBG] SOCKS5 Auth OK. Sending UoT Connect Request...\n");
                    node->state = CONN_STATE_HANDSHAKING;

                    // 发送 SOCKS5 CMD 请求 (sp.udp-over-tcp.arpa:0)
                    uint8_t cmd[] = "\x05\x01\x00\x03\x14sp.udp-over-tcp.arpa\x00\x00";
                    send(ctx->fd, cmd, sizeof(cmd) - 1, MSG_NOSIGNAL);

                    memmove(ctx->buf, ctx->buf + 2, ctx->buf_len - 2);
                    ctx->buf_len -= 2;
                } else {
                    printf("[Reactor] SOCKS5 Auth failed: %02X %02X\n", ctx->buf[0], ctx->buf[1]);
                    goto dead;
                }
            } else {
                break;
            }
        }

        // 阶段二：等待 ss-local 评估完成 (建立 UoT 隧道)
        if (node->state == CONN_STATE_HANDSHAKING) {
            if (ctx->buf_len >= 10) {
                size_t resp_len = 10;
                if (ctx->buf[3] == 0x04) resp_len = 22;
                else if (ctx->buf[3] == 0x03) resp_len = 7 + ctx->buf[4];
                if (ctx->buf_len < resp_len) break;

                if (ctx->buf[0] == 0x05 && ctx->buf[1] == 0x00) {
		    // 1. 准备本地栈变量，用于“窃取”暂存区的控制权
                    uint8_t *stolen_buf = NULL;
                    size_t stolen_len = 0;
		    uint32_t stolen_ip = 0;
                    uint16_t stolen_port = 0;
	
		    uint64_t hash_key = ((uint64_t)node->dest_ip << 16) | node->dest_port;
                    uint32_t bucket_idx = hash_fnv1a(&hash_key, sizeof(hash_key)) % TCP_HASH_BUCKETS;
                    pthread_rwlock_wrlock(&tcp_hash_table[bucket_idx].rwlock);

                    node->state = CONN_STATE_STREAMING;
                    printf("[Reactor] ss-local accepted SOCKS5 UoT tunnel on fd %d!\n", ctx->fd);

		    // 2. 核心优化：只偷指针，不在这执行网络 I/O
                    if (node->early_buf && node->early_len > 0) {
                        stolen_buf = node->early_buf;
                        stolen_len = node->early_len;

			stolen_ip = node->dest_ip;
                        stolen_port = node->dest_port;

			printf("[TRACE-REACTOR] Lock Stealing SUCCESS. Stole %zu bytes of early data from node.\n", stolen_len);

                        node->early_buf = NULL;
                        node->early_len = 0;
                    }
		    pthread_rwlock_unlock(&tcp_hash_table[bucket_idx].rwlock);

                    // 🟢 现场打包装箱暂存的首包
                    if (stolen_buf && stolen_len > 0) { 
			size_t offset = 0;
			// 循环解析出我们在 inbound 阶段塞进去的每一个包
                        while (offset < stolen_len) {
                            // 1. 提取我们暂存的 2 字节原始 UDP 包长度
                            uint16_t u16_len;
                            memcpy(&u16_len, stolen_buf + offset, 2);
                            uint16_t real_pkt_len = ntohs(u16_len);

                            // 2. 在栈上动态组装 9 字节标准的 UoT v1 帧头部
                            uint8_t uot_header[9];
                            uot_header[0] = 0x00; // ATYP: IPv4

                            // 严格对应标准 UoT v1 协议的字段偏移位置进行映射
			    memcpy(&uot_header[1], &stolen_ip, 4);
                            memcpy(&uot_header[5], &stolen_port, 2);
                            memcpy(&uot_header[7], &u16_len, 2);         // 偏移 7: 2字节 数据载荷长度（大端序）

                            // 3. 组装零拷贝向量表
                            struct iovec tx_iov[2];
                            tx_iov[0].iov_base = uot_header;
                            tx_iov[0].iov_len = 9;
                            tx_iov[1].iov_base = stolen_buf + offset + 2; // 越过暂存的2字节长度前缀，指向真实UDP数据
                            tx_iov[1].iov_len = real_pkt_len;

                            // 4. 将打包装箱后的完整 UoT 帧发送给 ss-local
			    writev(ctx->fd, tx_iov, 2);

                            offset += (2 + real_pkt_len);
			}
                        free(stolen_buf);
                    }

                    // 修改 epoll 动作为只关注可读，不再关注可写
                    struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.ptr = ctx };
                    epoll_ctl(global_epoll_fd, EPOLL_CTL_MOD, ctx->fd, &ev);

                    memmove(ctx->buf, ctx->buf + resp_len, ctx->buf_len - resp_len);
                    ctx->buf_len -= resp_len;
                } else {
                    printf("[Reactor] SOCKS5 CMD rejected: %02X\n", ctx->buf[1]);
                    goto dead;
                }
            } else {
                break;
            }
        }

        // 阶段三：STREAMING 标准 UoT 接收解包传输 (带 2 字节下行长度前缀)
        if (node->state == CONN_STATE_STREAMING) {
            // 🌟 UoT v1 IPv4 最小帧头长度 = 1(ATYP) + 4(IP) + 2(PORT) + 2(LENGTH) = 9 字节
            const size_t UOT_V1_MIN_HEADER = 9;
            // 依靠状态机循环解析可能存在的 TCP 粘包
            while (ctx->buf_len >= UOT_V1_MIN_HEADER) {
                
                // 1. 校验下行包的 ATYP 边界 (IPv4 必须为 0x00)
                uint8_t atyp = ctx->buf[0];
                if (atyp) { 
                    printf("[Reactor] FATAL: Invalid UoT v1 ATYP (0x%02X) on fd %d. Stream desync.\n", atyp, ctx->fd);
                    goto dead;
                }

                // 2. 🌟 提取位于偏移量 7、8 的 2 字节纯 UDP 净荷长度 (大端序)
                uint16_t data_len_net;
                memcpy(&data_len_net, &ctx->buf[7], 2);
                uint16_t payload_len = ntohs(data_len_net);

                // 3. 安全防御：防止畸形巨包导致缓冲区越界或路由器死机
                if (payload_len > BUFFER_SIZE) {
                    printf("[Reactor] Insane UoT v1 payload length detected (%u bytes). Evicting.\n", payload_len);
                    goto dead;
                }

                // 4. 🌟 检查半包：当前缓冲区总长度是否足够 [9字节头 + 实际数据长度]
                size_t total_frame_len = UOT_V1_MIN_HEADER + payload_len;
                if (ctx->buf_len < total_frame_len) {
                    // 数据尚未接收完整，优雅退出循环，保留缓冲区，等待下一次 epoll 触发
                    break;
                }

                // 5. 组装临时头，用于对接你原有的 tproxy_native_delivery 发包结构
                // UoT v1 格式下，回包的源 IP 和源 Port 位于偏移量的 [1-4] 和 [5-6]
		UotHeaderV1 *fake_header_ptr = (UotHeaderV1 *)ctx->buf;

                // 6. 如果有真实数据负载，执行提取与投递
                if (payload_len > 0) {
                    printf("\n[REACTOR DELIVERY] Delivering UDP Reply to Client! Size: %zu\n", (size_t)payload_len);
                    hex_dump("UDP Downstream Payload", ctx->buf + UOT_V1_MIN_HEADER, payload_len);

		    tproxy_native_delivery(fake_header_ptr, ctx->buf + UOT_V1_MIN_HEADER, payload_len);
                }

                // 7. 🌟 内存流平移：平滑清除当前已经处理完毕的完整帧
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
    while (1) {
        int nfds = epoll_wait(global_epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0 && errno != EINTR) break;
        for (int i = 0; i < nfds; i++) {
            EpollContext *ctx = (EpollContext *)events[i].data.ptr;
            if (ctx->dead || (events[i].events & (EPOLLERR | EPOLLHUP))) { 
                purge_dead_tunnel_in_reactor(ctx);
                continue; 
            }
            
            // 🌟 核心修复：针对非阻塞连接事件进行 system 内核层校验
            if (events[i].events & EPOLLOUT) {
                if (ctx->node_ptr && ctx->node_ptr->state == CONN_STATE_CONNECTING) {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    // 询问内核当前 FD 三次握手是否成功
                    if (getsockopt(ctx->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
                        // 发现对端不可达、拒绝连接等错误，在这里统一安全清理
                        printf("[DBG] TCP Connect to ss-local failed, errno: %d\n", err);
                        purge_dead_tunnel_in_reactor(ctx);
                        continue;
                    }
                    
                    // 验证通过，安全演进到下一步
                    printf("[DBG] TCP Connect to ss-local SUCCESS! Sending SOCKS5 Auth...\n");
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

// ---------------- 模块四：入站监听与双向资源保活 GC ----------------
void *tproxy_inbound_loop(void *arg) {
    int tproxy_fd = *(int *)arg;
    struct sockaddr_in client_addr;
    struct sockaddr_in orig_dst; 
    uint8_t control_buf[CMSG_SPACE(sizeof(struct sockaddr_in))];

    printf("[System] TPROXY inbound loop listener fully activated with writev optimization.\n");
    while (1) {
	uint8_t *pkt_ptr = alloc_early_slot();
        if (!pkt_ptr) {
            // 极端 OOM 防御：用一个临时栈 buffer 读掉垃圾流量，防止卡死网卡
            uint8_t drop_buf[128];
            struct iovec drop_iov = { .iov_base = drop_buf, .iov_len = sizeof(drop_buf) };
            struct msghdr drop_msg = { .msg_iov = &drop_iov, .msg_iovlen = 1 };
            recvmsg(tproxy_fd, &drop_msg, 0);
            continue;
        }

        struct iovec iov[1];
        iov[0].iov_base = pkt_ptr + 2;
        iov[0].iov_len = PKT_BUF_SIZE - 2;;

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &client_addr;
        msg.msg_namelen = sizeof(client_addr);
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control_buf;
        msg.msg_controllen = sizeof(control_buf);

        // 1. 接收来自 TPROXY 拦截的本地客户端 UDP 数据包
        ssize_t len = recvmsg(tproxy_fd, &msg, 0);
        if (len < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            perror("[TPROXY] TPROXY recvmsg failed");
            break;
        }

        // 2. 解析 OOB (Out-of-Band) 控制数据，提取被拦截前的原始目的 IP 和 Port
        struct cmsghdr *cmsg;
        int found = 0;
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVORIGDSTADDR) {
                memcpy(&orig_dst, CMSG_DATA(cmsg), sizeof(struct sockaddr_in));
                found = 1;
                break;
            }
        }
        printf("[TRACE-DST] Destination IP: %s, Port: %d, len:%d\n", 
        inet_ntoa(orig_dst.sin_addr), 
        ntohs(orig_dst.sin_port), len);	

        // 如果没有成功获取到原始目的地址（可能非 TPROXY 流量），安全丢弃
        if (!found) {
            continue;
        }

        register_udp_session(client_addr.sin_addr.s_addr, client_addr.sin_port, orig_dst.sin_addr.s_addr, orig_dst.sin_port);

	// 原地在前 2 字节写下网络字节序的包长度，完成 ivo 缓冲区的完美封装
        uint16_t u16_len = htons((uint16_t)len);
        memcpy(pkt_ptr, &u16_len, 2);

	uint32_t dest_ip = orig_dst.sin_addr.s_addr;
        uint16_t dest_port = orig_dst.sin_port;

	int tunnel_fd = get_uot_tunnel_via_sslocal(dest_ip, dest_port, pkt_ptr, len);

	//【核心精简】：直接交托给隧道管理器，内部自行用读写锁做 Double-Check 决策
	if (tunnel_fd > 0) {
            // 🟢 状态一：老连接直通流
            uint8_t uot_header[9];
            uot_header[0] = 0x00; // ATYP: IPv4
            memcpy(&uot_header[1], &dest_ip, 4);
            memcpy(&uot_header[5], &dest_port, 2);
            memcpy(&uot_header[7], &u16_len, 2);

            struct iovec tx_iov[2] = {
                { .iov_base = uot_header, .iov_len = 9 },
                { .iov_base = pkt_ptr + 2, .iov_len = len } // 零拷贝直发快速内存槽
            };

            writev(tunnel_fd, tx_iov, 2);

            // 直发完毕，快速内存槽完成历史使命，原地归还
            free_early_slot(pkt_ptr);
        }
        else if (tunnel_fd == 0) {
            //  状态二：冷启动首包，或连接正在建立中
            // 此时 get_uot_tunnel 内部已经通过“指针所有权移交”把 pkt_ptr 扣留并挂在了 TcpNode 上。
            // 这里的任务完美闭环，绝对不要释放指针，直接进入下一次循环接收后续包。
            continue;
        }
        else {
	    //  状态三：系统级错误资源耗尽，回收槽位
            free_early_slot(pkt_ptr);
        }
    }
    return NULL;
}

void* double_drive_gc_daemon(void *arg) {
    (void)arg;
    printf("[System] Optimised Double Drive GC Daemon fully integrated.\n");
    while (1) {
        sleep(10);
        uint64_t now = get_mono_time();
        int udp_cleaned = 0;

        // ==================== 1. UDP 映射表清理 ====================
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
                    udp_cleaned++;
                } else {
                    prev = curr; 
                    curr = curr->next;
                }
            }
            pthread_rwlock_unlock(&udp_hash_table[i].rwlock);
        }

        // ==================== 2. TCP 隧道长连接动态退化/钝化清理 ====================
        for (int i = 0; i < TCP_HASH_BUCKETS; i++) {
            pthread_rwlock_wrlock(&tcp_hash_table[i].rwlock);
            TcpNode *curr = tcp_hash_table[i].head;
            while (curr) {
                // 🌟 安全修正：只有处于 CONN_STATE_STREAMING (已就绪的长连接) 且空闲超时，才触发回收
                if (curr->state == CONN_STATE_STREAMING && (now - curr->last_used > TCP_IDLE_TIMEOUT)) {
                    if (!curr->ctx_ptr.dead) {
                        printf("[GC_Daemon] Idle tunnel to %d.%d.%d.%d:%d expired. Marking dead.\n", 
                               curr->dest_ip & 0xFF, (curr->dest_ip >> 8) & 0xFF, (curr->dest_ip >> 16) & 0xFF, (curr->dest_ip >> 24) & 0xFF, ntohs(curr->dest_port));
                        // 确保不要因为 GC 和入站流量并发导致重复创建
                        uint32_t target_ip = curr->dest_ip;
                        uint16_t target_port = curr->dest_port;
                        // 1. 标记死亡
                        curr->ctx_ptr.dead = 1;
                        // 2. 通过 shutdown 迫使内核向 epoll 投递 EPOLLHUP / EPOLLIN 异常事件
                        // 从而让 Reactor 线程在无锁/安全状态下执行真正的 free(node) 和 close(fd)
                        shutdown(curr->tcp_fd, SHUT_RDWR);
                    }
                }
                curr = curr->next;
            }
            pthread_rwlock_unlock(&tcp_hash_table[i].rwlock);
        }
        if (udp_cleaned > 0) {
            printf("[GC_Daemon] Evicted %d expired UDP NAT sessions.\n", udp_cleaned);
        }
    }
    return NULL;
}

// ---------------- 模块五：命令行参数解析与系统入口 ----------------
void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -l <port>    Local TPROXY listen port for UDP inbound (e.g., 10801)\n");
    printf("  -s <ip>      Local ss-redir TCP listen IP address (e.g., 127.0.0.1)\n");
    printf("  -p <port>    Local ss-redir TCP listen port (e.g., 10800)\n");
    printf("  -h           Show this help message\n");
}

int main(int argc, char *argv[]) {
    // 默认配置初始化
    g_config.tproxy_port = 0;
    g_config.ss_redir_port = 0;
    strcpy(g_config.ss_redir_ip, "127.0.0.1");

    // 解析命令行参数（去除了未使用的 f 和 P 参数）
    int opt;
    while ((opt = getopt(argc, argv, "l:s:p:h")) != -1) {
        switch (opt) {
            case 'l': g_config.tproxy_port = atoi(optarg); break;
            case 's': strncpy(g_config.ss_redir_ip, optarg, sizeof(g_config.ss_redir_ip) - 1); break;
            case 'p': g_config.ss_redir_port = atoi(optarg); break;
            case 'h':
            default:  print_usage(argv[0]); return 0;
        }
    }

    // 参数合规性校验
    if (g_config.tproxy_port == 0 || g_config.ss_redir_port == 0 || strlen(g_config.ss_redir_ip) == 0) {
        fprintf(stderr, "Error: Missing required arguments.\n");
        print_usage(argv[0]);
        return 1;
    }

    g_config.ss_redir_sockaddr.sin_family = AF_INET;
    g_config.ss_redir_sockaddr.sin_port = htons(g_config.ss_redir_port);

    // 初始化哈希表与锁
    memset(udp_hash_table, 0, sizeof(udp_hash_table));
    memset(tcp_hash_table, 0, sizeof(tcp_hash_table));
    for (int i = 0; i < MAX_SESSIONS; i++) pthread_rwlock_init(&udp_hash_table[i].rwlock, NULL);
    for (int i = 0; i < TCP_HASH_BUCKETS; i++) pthread_rwlock_init(&tcp_hash_table[i].rwlock, NULL);

    // 创设 Epoll 实例
    if ((global_epoll_fd = epoll_create1(0)) < 0) {
        perror("Fatal: Epoll instance initialization failed");
        return 1;
    }

    // 创建 TPROXY 监听套接字
    int tproxy_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (tproxy_fd < 0) { perror("Fatal: Create UDP socket failed"); return 1; }

    int flag = 1;
    setsockopt(tproxy_fd, SOL_IP, IP_TRANSPARENT, &flag, sizeof(flag));
    setsockopt(tproxy_fd, SOL_IP, IP_RECVORIGDSTADDR, &flag, sizeof(flag));

    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(g_config.tproxy_port)
    };

    if (bind(tproxy_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        perror("Fatal: Bind TPROXY listener port failed");
        close(tproxy_fd);
        return 1;
    }

    // 分配常驻线程安全副本，防止共享污染
    int *pass_fd = malloc(sizeof(int));
    if (!pass_fd) { perror("Fatal: Memory allocation failed"); close(tproxy_fd); return 1; }
    *pass_fd = tproxy_fd;

    // 启动异步网络引擎线程组
    pthread_t r_tid, i_tid, g_tid;
    pthread_create(&r_tid, NULL, epoll_reactor_thread, NULL);   pthread_detach(r_tid);
    pthread_create(&i_tid, NULL, tproxy_inbound_loop, pass_fd); pthread_detach(i_tid);
    pthread_create(&g_tid, NULL, double_drive_gc_daemon, NULL); pthread_detach(g_tid);

    printf("[System] Parametric async network engine successfully deployed.\n");
    printf(" -> Inbound TPROXY Port  : %d\n", g_config.tproxy_port);
    printf(" -> Outbound Proxy Target: %s:%d\n", g_config.ss_redir_ip, g_config.ss_redir_port);

    // 主线程挂起常驻
    while (1) pause();

    return 0;
}
