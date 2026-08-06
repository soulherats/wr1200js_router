#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <crypto/aes.h>
#include <crypto/algapi.h>

#include "mtk_crypto_api.h"
#include "mtk_ipsec.h"

#define MTK_EIP93_MAX_CRYPT_LEN ((1U << 20) - 1)

static atomic_t mtk_crypto_jobs_pending = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(mtk_crypto_jobs_wait);

struct mtk_crypto_ctx {
	u8 key[AES_MAX_KEY_SIZE];
	unsigned int keylen;
};

struct mtk_crypto_job {
	struct ablkcipher_request *req;
	struct eip93DescpHandler_s desc;
	struct saRecord_s *sa;
	struct saState_s *state;
	void *src;
	void *dst;
	dma_addr_t sa_dma;
	dma_addr_t state_dma;
	dma_addr_t src_dma;
	dma_addr_t dst_dma;
	unsigned int len;
};

static int mtk_crypto_sg_nents_for_len(struct scatterlist *sg,
					       unsigned int len)
{
	int nents = 0;

	while (sg) {
		nents++;
		if (sg->length >= len)
			return nents;
		len -= sg->length;
		sg = sg_next(sg);
	}

	return -EINVAL;
}

static void mtk_crypto_job_free(struct mtk_crypto_job *job)
{
	if (!job)
		return;

	if (job->dst)
		dma_free_coherent(NULL, job->len, job->dst, job->dst_dma);
	if (job->src)
		dma_free_coherent(NULL, job->len, job->src, job->src_dma);
	if (job->state)
		dma_free_coherent(NULL, sizeof(*job->state), job->state,
				   job->state_dma);
	if (job->sa)
		dma_free_coherent(NULL, sizeof(*job->sa), job->sa,
				   job->sa_dma);
	kfree(job);
}

static int mtk_crypto_setkey(struct crypto_ablkcipher *tfm,
				     const u8 *key, unsigned int keylen)
{
	struct mtk_crypto_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memset(ctx, 0, sizeof(*ctx));
	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;
	return 0;
}

static void mtk_crypto_prepare_sa(struct mtk_crypto_job *job,
				  const struct mtk_crypto_ctx *ctx,
				  int decrypt, const u8 *iv)
{
	struct saRecord_s *sa = job->sa;

	/* Raw cipher mode, matching EIP93's basic crypto operation. */
	sa->saCmd0.bits.opGroup = 0;
	sa->saCmd0.bits.opCode = 0;
	sa->saCmd0.bits.direction = decrypt ? 1 : 0;
	sa->saCmd0.bits.cipher = 3;
	sa->saCmd0.bits.hash = 0xf;
	sa->saCmd0.bits.hdrProc = 0;
	sa->saCmd0.bits.padType = 3;
	sa->saCmd0.bits.ivSource = 2;
	sa->saCmd0.bits.saveIv = 1;
	sa->saCmd0.bits.digestLength = 0;

	sa->saCmd1.bits.cipherMode = 1;
	sa->saCmd1.bits.copyDigest = 0;
	sa->saCmd1.bits.copyHeader = 0;
	sa->saCmd1.bits.copyPayload = 0;
	sa->saCmd1.bits.copyPad = 0;
	sa->saCmd1.bits.hmac = 0;
	sa->saCmd1.bits.byteOffset = 0;
	sa->saCmd1.bits.hashCryptOffset = 0;
	switch (ctx->keylen) {
	case AES_KEYSIZE_128:
		sa->saCmd1.bits.arc4KeyLen = 2;
		break;
	case AES_KEYSIZE_192:
		sa->saCmd1.bits.arc4KeyLen = 3;
		break;
	default:
		sa->saCmd1.bits.arc4KeyLen = 4;
		break;
	}
	sa->saCmd1.bits.seqNumCheck = 0;

	memcpy(sa->saKey, ctx->key, ctx->keylen);
	sa->saSpi = 0;
	sa->saSeqNumMask[0] = 0xffffffff;
	memset(job->state, 0, sizeof(*job->state));
	memcpy(job->state->stateIv, iv, AES_BLOCK_SIZE);
}

static int mtk_crypto_crypt(struct ablkcipher_request *req, int decrypt)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct mtk_crypto_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct mtk_crypto_job *job;
	gfp_t gfp;
	int src_nents;
	int dst_nents;
	int ret;

	if (!ctx->keylen || !req->nbytes ||
	    req->nbytes > MTK_EIP93_MAX_CRYPT_LEN ||
	    req->nbytes & (AES_BLOCK_SIZE - 1) || !req->info)
		return -EINVAL;

	src_nents = mtk_crypto_sg_nents_for_len(req->src, req->nbytes);
	if (src_nents <= 0)
		return -EINVAL;
	dst_nents = mtk_crypto_sg_nents_for_len(req->dst, req->nbytes);
	if (dst_nents <= 0)
		return -EINVAL;

	gfp = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		GFP_KERNEL : GFP_ATOMIC;
	job = kzalloc(sizeof(*job), gfp);
	if (!job)
		return -ENOMEM;

	job->req = req;
	job->len = req->nbytes;
	job->sa = dma_alloc_coherent(NULL, sizeof(*job->sa), &job->sa_dma,
					gfp);
	if (!job->sa)
		goto nomem;
	job->state = dma_alloc_coherent(NULL, sizeof(*job->state),
					&job->state_dma, gfp);
	if (!job->state)
		goto nomem;
	job->src = dma_alloc_coherent(NULL, job->len, &job->src_dma, gfp);
	if (!job->src)
		goto nomem;
	job->dst = dma_alloc_coherent(NULL, job->len, &job->dst_dma, gfp);
	if (!job->dst)
		goto nomem;

	if (sg_copy_to_buffer(req->src, src_nents, job->src, job->len) !=
	    job->len)
		goto bad_request;

	mtk_crypto_prepare_sa(job, ctx, decrypt, req->info);

	job->desc.peCrtlStat.bits.hostReady = 1;
	job->desc.peCrtlStat.bits.hashFinal = 0;
	job->desc.peCrtlStat.bits.peReady = 0;
	job->desc.peLength.bits.hostReady = 1;
	job->desc.peLength.bits.peReady = 0;
	job->desc.peLength.bits.length = job->len;
	job->desc.srcAddr.addr = (unsigned int)job->src;
	job->desc.srcAddr.phyAddr = job->src_dma;
	job->desc.dstAddr.addr = (unsigned int)job->dst;
	job->desc.dstAddr.phyAddr = job->dst_dma;
	job->desc.saAddr.addr = (unsigned int)job->sa;
	job->desc.saAddr.phyAddr = job->sa_dma;
	job->desc.stateAddr.addr = (unsigned int)job->state;
	job->desc.stateAddr.phyAddr = job->state_dma;
	job->desc.arc4Addr.addr = (unsigned int)job->state;
	job->desc.arc4Addr.phyAddr = job->state_dma;
	job->desc.userId = (unsigned int)job;

	/* Coherent memory is already visible, but retain the MIPS cache sync. */
	dma_cache_wback_inv((unsigned long)job->src, job->len);
	atomic_inc(&mtk_crypto_jobs_pending);
	ret = mtk_crypto_packet_put(&job->desc);
	if (ret) {
		atomic_dec(&mtk_crypto_jobs_pending);
		mtk_crypto_job_free(job);
		return ret;
	}

	return -EINPROGRESS;

bad_request:
	mtk_crypto_job_free(job);
	return -EIO;
nomem:
	mtk_crypto_job_free(job);
	return -ENOMEM;
}

static int mtk_crypto_encrypt(struct ablkcipher_request *req)
{
	return mtk_crypto_crypt(req, 0);
}

static int mtk_crypto_decrypt(struct ablkcipher_request *req)
{
	return mtk_crypto_crypt(req, 1);
}

void mtk_crypto_complete(unsigned int userId, int err)
{
	struct mtk_crypto_job *job = (struct mtk_crypto_job *)userId;
	struct ablkcipher_request *req;
	crypto_completion_t complete;
	int dst_nents;

	if (!job)
		return;

	req = job->req;
	if (!err) {
		dst_nents = mtk_crypto_sg_nents_for_len(req->dst, job->len);
		dma_cache_sync(NULL, job->dst, job->len, DMA_FROM_DEVICE);
		if (dst_nents <= 0 || sg_copy_from_buffer(req->dst, dst_nents,
					job->dst, job->len) != job->len)
			err = -EIO;
		else
			memcpy(req->info, job->state->stateIv, AES_BLOCK_SIZE);
	}

	complete = req->base.complete;
	if (complete)
		complete(&req->base, err);
	mtk_crypto_job_free(job);
	if (atomic_dec_and_test(&mtk_crypto_jobs_pending))
		wake_up(&mtk_crypto_jobs_wait);
}

static struct crypto_alg mtk_cbc_aes_alg = {
	.cra_name = "cbc(aes)",
	.cra_driver_name = "cbc-aes-eip93",
	.cra_priority = 300,
	.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct mtk_crypto_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_ablkcipher_type,
	.cra_module = THIS_MODULE,
	.cra_u.ablkcipher = {
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = mtk_crypto_setkey,
		.encrypt = mtk_crypto_encrypt,
		.decrypt = mtk_crypto_decrypt,
	},
};

int mtk_crypto_api_init(void)
{
	return crypto_register_alg(&mtk_cbc_aes_alg);
}

void mtk_crypto_api_exit(void)
{
	crypto_unregister_alg(&mtk_cbc_aes_alg);
	wait_event(mtk_crypto_jobs_wait,
		   atomic_read(&mtk_crypto_jobs_pending) == 0);
}
