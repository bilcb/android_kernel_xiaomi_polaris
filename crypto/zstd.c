/*
 * Cryptographic API.
 *
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>


#define ZSTD_DEF_LEVEL	3
#define ZSTD_MAX_WINDOWLOG	18
#define ZSTD_MAX_SIZE		BIT(ZSTD_MAX_WINDOWLOG)

struct zstd_ctx {
	zstd_cctx *cctx;
	zstd_dctx *dctx;
	void *wksp;
	size_t wksp_size;
	zstd_parameters params;
};

static int zstd_init(struct crypto_tfm *tfm)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);
	zstd_parameters params;

	params = zstd_get_params(ZSTD_DEF_LEVEL, ZSTD_MAX_SIZE);
	ctx->wksp_size = max(zstd_cstream_workspace_bound(&params.cParams),
			zstd_dstream_workspace_bound(ZSTD_MAX_SIZE));

	ctx->wksp = vzalloc(ctx->wksp_size);
	if (!ctx->wksp)
		return -ENOMEM;

	ctx->params = params;

	return 0;
}

static void zstd_exit(struct crypto_tfm *tfm)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);

	vfree(ctx->wksp);
}

static int zstd_compress_crypto(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);
	size_t out_len;

	ctx->cctx = zstd_init_cctx(ctx->wksp, ctx->wksp_size);
	if (!ctx->cctx)
		return -EINVAL;

	out_len = zstd_compress_cctx(ctx->cctx, dst, *dlen, src, slen,
			&ctx->params);
	if (zstd_is_error(out_len))
		return -EINVAL;

	*dlen = out_len;
	return 0;
}

static int zstd_decompress_crypto(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);
	size_t out_len;

	ctx->dctx = zstd_init_dctx(ctx->wksp, ctx->wksp_size);
	if (!ctx->dctx)
		return -EINVAL;

	out_len = zstd_decompress_dctx(ctx->dctx, dst, *dlen, src, slen);
	if (zstd_is_error(out_len))
		return -EINVAL;

	*dlen = out_len;
	return 0;
}

static struct crypto_alg alg_zstd = {
	.cra_name		= "zstd",
	.cra_driver_name	= "zstd-generic",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct zstd_ctx),
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(alg_zstd.cra_list),
	.cra_init		= zstd_init,
	.cra_exit		= zstd_exit,
	.cra_u			= { .compress = {
	.coa_compress		= zstd_compress_crypto,
	.coa_decompress		= zstd_decompress_crypto } }
};

static int __init zstd_mod_init(void)
{
	return crypto_register_alg(&alg_zstd);
}

static void __exit zstd_mod_fini(void)
{
	crypto_unregister_alg(&alg_zstd);
}

module_init(zstd_mod_init);
module_exit(zstd_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Zstd Compression Algorithm");
MODULE_ALIAS_CRYPTO("zstd");
