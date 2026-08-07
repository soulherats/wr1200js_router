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
#include <crypto/internal/hash.h>
#include <crypto/sha.h>

#include "mtk_crypto_api.h"
#include "mtk_ipsec.h"

#define MTK_EIP93_MAX_CRYPT_LEN ((1U << 20) - 1)
#define MTK_EIP93_MAX_HASH_LEN (MTK_EIP93_MAX_CRYPT_LEN - 64)
#define MTK_EIP93_HASH_BLOCK_SIZE 64

#define MTK_EIP93_HASH_MD5 0
#define MTK_EIP93_HASH_SHA1 1
#define MTK_EIP93_HASH_SHA224 2
#define MTK_EIP93_HASH_SHA256 3

static atomic_t mtk_crypto_jobs_pending = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(mtk_crypto_jobs_wait);

struct mtk_crypto_ctx {
	u8 key[AES_MAX_KEY_SIZE];
	unsigned int keylen;
};

struct mtk_hash_req_ctx {
	u8 *data;
	unsigned int len;
	unsigned int alloc;
};

struct mtk_crypto_job {
	struct ablkcipher_request *req;
	struct ahash_request *hash_req;
	unsigned int hash_alg;
	unsigned int digestsize;
	bool is_hash;
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

static unsigned int mtk_hash_digestsize(unsigned int hash_alg)
{
	switch (hash_alg) {
	case MTK_EIP93_HASH_SHA1:
		return SHA1_DIGEST_SIZE;
	case MTK_EIP93_HASH_SHA224:
		return SHA224_DIGEST_SIZE;
	case MTK_EIP93_HASH_SHA256:
		return SHA256_DIGEST_SIZE;
	default:
		return 0;
	}
}

static unsigned int mtk_hash_words(unsigned int hash_alg)
{
	return mtk_hash_digestsize(hash_alg) / sizeof(u32);
}

/* EIP93 exposes digest state words in CPU-endian memory.  The Crypto API
 * returns the standard big-endian byte representation of SHA digests. */
static void mtk_hash_digest_copy(u8 *dst, const u32 *src,
				 unsigned int digestsize)
{
	unsigned int i;

	for (i = 0; i < digestsize; i += sizeof(u32)) {
		u32 word = src[i / sizeof(u32)];

		dst[i] = (u8)(word >> 24);
		dst[i + 1] = (u8)(word >> 16);
		dst[i + 2] = (u8)(word >> 8);
		dst[i + 3] = (u8)word;
	}
}

static unsigned int mtk_hash_alg_from_name(struct crypto_ahash *tfm)
{
	const char *name = crypto_tfm_alg_name(crypto_ahash_tfm(tfm));

	if (!strcmp(name, "sha1"))
		return MTK_EIP93_HASH_SHA1;
	if (!strcmp(name, "sha224"))
		return MTK_EIP93_HASH_SHA224;
	if (!strcmp(name, "sha256"))
		return MTK_EIP93_HASH_SHA256;
	return UINT_MAX;
}

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
	struct ahash_request *hash_req;
	struct mtk_hash_req_ctx *hash_ctx;
	crypto_completion_t complete;
	int dst_nents;

	if (!job)
		return;

	if (job->is_hash) {
		hash_req = job->hash_req;
		hash_ctx = ahash_request_ctx(hash_req);
		if (!err) {
			dma_cache_sync(NULL, job->state, sizeof(*job->state),
				       DMA_FROM_DEVICE);
			mtk_hash_digest_copy(hash_req->result,
					     job->state->stateIDigest,
					     job->digestsize);
		}
		kfree(hash_ctx->data);
		hash_ctx->data = NULL;
		hash_ctx->len = 0;
		hash_ctx->alloc = 0;
		complete = hash_req->base.complete;
		if (complete)
			complete(&hash_req->base, err);
		mtk_crypto_job_free(job);
		if (atomic_dec_and_test(&mtk_crypto_jobs_pending))
			wake_up(&mtk_crypto_jobs_wait);
		return;
	}

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

static int mtk_hash_append(struct ahash_request *req)
{
	struct mtk_hash_req_ctx *ctx = ahash_request_ctx(req);
	unsigned int nents;
	unsigned int new_len;
	unsigned int alloc;
	u8 *data;

	if (!req->nbytes)
		return 0;
	if (req->nbytes > MTK_EIP93_MAX_HASH_LEN - ctx->len)
		return -E2BIG;

	new_len = ctx->len + req->nbytes;
	alloc = PAGE_ALIGN(new_len);
	if (alloc != ctx->alloc) {
		data = krealloc(ctx->data, alloc, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		ctx->data = data;
		ctx->alloc = alloc;
	}

	nents = mtk_crypto_sg_nents_for_len(req->src, req->nbytes);
	if (nents <= 0 || sg_copy_to_buffer(req->src, nents,
					ctx->data + ctx->len, req->nbytes) != req->nbytes)
		return -EIO;
	ctx->len = new_len;
	return 0;
}

static int mtk_hash_submit(struct ahash_request *req, const u8 *data,
				   unsigned int len)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct mtk_crypto_job *job;
	struct saRecord_s *sa;
	u8 *src;
	unsigned int hash_alg = mtk_hash_alg_from_name(tfm);
	unsigned int digestsize = mtk_hash_digestsize(hash_alg);
	unsigned int words = mtk_hash_words(hash_alg);
	unsigned int padded_len;
	unsigned int bitlen_hi;
	unsigned int bitlen_lo;
	gfp_t gfp;
	int ret;

	if (hash_alg == UINT_MAX || !digestsize || len > MTK_EIP93_MAX_HASH_LEN)
		return -EINVAL;
	if (len > (UINT_MAX - 9 - (MTK_EIP93_HASH_BLOCK_SIZE - 1)))
		return -E2BIG;

	padded_len = (len + 9 + MTK_EIP93_HASH_BLOCK_SIZE - 1) &
		     ~(MTK_EIP93_HASH_BLOCK_SIZE - 1);
	if (padded_len > MTK_EIP93_MAX_CRYPT_LEN)
		return -E2BIG;

	gfp = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		GFP_KERNEL : GFP_ATOMIC;
	job = kzalloc(sizeof(*job), gfp);
	if (!job)
		return -ENOMEM;

	job->hash_req = req;
	job->hash_alg = hash_alg;
	job->digestsize = digestsize;
	job->is_hash = true;
	job->len = padded_len;
	job->sa = dma_alloc_coherent(NULL, sizeof(*job->sa), &job->sa_dma,
					gfp);
	job->state = dma_alloc_coherent(NULL, sizeof(*job->state),
					&job->state_dma, gfp);
	job->src = dma_alloc_coherent(NULL, padded_len, &job->src_dma, gfp);
	job->dst = dma_alloc_coherent(NULL, padded_len, &job->dst_dma, gfp);
	if (!job->sa || !job->state || !job->src || !job->dst)
		goto nomem;

	memset(job->src, 0, padded_len);
	if (len)
		memcpy(job->src, data, len);
	src = job->src;
	src[len] = 0x80;
	bitlen_hi = 0;
	bitlen_lo = len << 3;
	src[padded_len - 8] = (bitlen_hi >> 24) & 0xff;
	src[padded_len - 7] = (bitlen_hi >> 16) & 0xff;
	src[padded_len - 6] = (bitlen_hi >> 8) & 0xff;
	src[padded_len - 5] = bitlen_hi & 0xff;
	src[padded_len - 4] = (bitlen_lo >> 24) & 0xff;
	src[padded_len - 3] = (bitlen_lo >> 16) & 0xff;
	src[padded_len - 2] = (bitlen_lo >> 8) & 0xff;
	src[padded_len - 1] = bitlen_lo & 0xff;

	sa = job->sa;
	sa->saCmd0.bits.opGroup = 0;
	sa->saCmd0.bits.opCode = 3;
	sa->saCmd0.bits.direction = 0;
	sa->saCmd0.bits.cipher = 0xf;
	sa->saCmd0.bits.hash = hash_alg;
	sa->saCmd0.bits.hdrProc = 0;
	sa->saCmd0.bits.padType = 0;
	sa->saCmd0.bits.hashSource = 3;
	sa->saCmd0.bits.saveHash = 1;
	sa->saCmd0.bits.digestLength = words;

	job->desc.peCrtlStat.bits.hostReady = 1;
	job->desc.peCrtlStat.bits.hashFinal = 0;
	job->desc.peCrtlStat.bits.peReady = 0;
	job->desc.peLength.bits.hostReady = 1;
	job->desc.peLength.bits.peReady = 0;
	job->desc.peLength.bits.length = padded_len;
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

	dma_cache_wback_inv((unsigned long)job->src, padded_len);
	atomic_inc(&mtk_crypto_jobs_pending);
	ret = mtk_crypto_packet_put(&job->desc);
	if (ret) {
		atomic_dec(&mtk_crypto_jobs_pending);
		mtk_crypto_job_free(job);
		return ret;
	}
	return -EINPROGRESS;

nomem:
	mtk_crypto_job_free(job);
	return -ENOMEM;
}

static int mtk_hash_init_tfm(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct mtk_hash_req_ctx));
	return 0;
}

static int mtk_hash_init(struct ahash_request *req)
{
	struct mtk_hash_req_ctx *ctx = ahash_request_ctx(req);

	memset(ctx, 0, sizeof(*ctx));
	return 0;
}

static int mtk_hash_update(struct ahash_request *req)
{
	return mtk_hash_append(req);
}

static int mtk_hash_final(struct ahash_request *req)
{
	struct mtk_hash_req_ctx *ctx = ahash_request_ctx(req);
	int ret;

	ret = mtk_hash_submit(req, ctx->data, ctx->len);
	if (ret != -EINPROGRESS)
		kfree(ctx->data);
	if (ret != -EINPROGRESS)
		ctx->data = NULL;
	return ret;
}

static int mtk_hash_finup(struct ahash_request *req)
{
	struct mtk_hash_req_ctx *ctx = ahash_request_ctx(req);
	int ret;

	ret = mtk_hash_append(req);
	if (ret) {
		kfree(ctx->data);
		ctx->data = NULL;
		ctx->len = 0;
		ctx->alloc = 0;
		return ret;
	}
	return mtk_hash_final(req);
}

static int mtk_hash_digest(struct ahash_request *req)
{
	struct mtk_hash_req_ctx *ctx = ahash_request_ctx(req);
	int ret;

	ret = mtk_hash_init(req);
	if (ret)
		return ret;
	ret = mtk_hash_append(req);
	if (ret) {
		kfree(ctx->data);
		ctx->data = NULL;
		ctx->len = 0;
		ctx->alloc = 0;
		return ret;
	}
	return mtk_hash_final(req);
}

#define MTK_HASH_ALG(_name, _driver, _size) \
{	.init = mtk_hash_init, \
	.update = mtk_hash_update, \
	.final = mtk_hash_final, \
	.finup = mtk_hash_finup, \
	.digest = mtk_hash_digest, \
	.halg.digestsize = (_size), \
	.halg.statesize = sizeof(struct mtk_hash_req_ctx), \
	.halg.base.cra_name = (_name), \
	.halg.base.cra_driver_name = (_driver), \
	.halg.base.cra_priority = 300, \
	.halg.base.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC, \
	.halg.base.cra_blocksize = MTK_EIP93_HASH_BLOCK_SIZE, \
	.halg.base.cra_ctxsize = 0, \
	.halg.base.cra_type = &crypto_ahash_type, \
	.halg.base.cra_init = mtk_hash_init_tfm, \
	.halg.base.cra_module = THIS_MODULE, \
}

static struct ahash_alg mtk_sha1_alg = MTK_HASH_ALG("sha1",
		"sha1-eip93", SHA1_DIGEST_SIZE);
static struct ahash_alg mtk_sha224_alg = MTK_HASH_ALG("sha224",
		"sha224-eip93", SHA224_DIGEST_SIZE);
static struct ahash_alg mtk_sha256_alg = MTK_HASH_ALG("sha256",
		"sha256-eip93", SHA256_DIGEST_SIZE);

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
	int ret;

	ret = crypto_register_alg(&mtk_cbc_aes_alg);
	if (ret)
		return ret;
	ret = crypto_register_ahash(&mtk_sha1_alg);
	if (ret)
		goto unregister_cbc;
	ret = crypto_register_ahash(&mtk_sha224_alg);
	if (ret)
		goto unregister_sha1;
	ret = crypto_register_ahash(&mtk_sha256_alg);
	if (ret)
		goto unregister_sha224;
	return 0;

unregister_sha224:
	crypto_unregister_ahash(&mtk_sha224_alg);
unregister_sha1:
	crypto_unregister_ahash(&mtk_sha1_alg);
unregister_cbc:
	crypto_unregister_alg(&mtk_cbc_aes_alg);
	return ret;
}

void mtk_crypto_api_exit(void)
{
	crypto_unregister_ahash(&mtk_sha256_alg);
	crypto_unregister_ahash(&mtk_sha224_alg);
	crypto_unregister_ahash(&mtk_sha1_alg);
	crypto_unregister_alg(&mtk_cbc_aes_alg);
	wait_event(mtk_crypto_jobs_wait,
		   atomic_read(&mtk_crypto_jobs_pending) == 0);
}
