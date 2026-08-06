#ifndef MTK_CRYPTO_API_H
#define MTK_CRYPTO_API_H

#include <net/mtk_esp.h>

int mtk_crypto_packet_put(eip93DescpHandler_t *cmdDescp);
void mtk_crypto_complete(unsigned int userId, int err);
int mtk_crypto_api_init(void);
void mtk_crypto_api_exit(void);

#endif
