/*
 * Copyright (c) 2014 Arturo Borrero Gonzalez <arturo.borrero.glez@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nft_masq.h>

const struct nla_policy nft_masq_policy[NFTA_MASQ_MAX + 1] = {
	[NFTA_MASQ_FLAGS]	= { .type = NLA_U32 },
};
EXPORT_SYMBOL_GPL(nft_masq_policy);

int nft_masq_init(const struct nft_ctx *ctx,
		  const struct nft_expr *expr,
		  const struct nlattr * const tb[])
{
	struct nft_masq *priv = nft_expr_priv(expr);
	int err;

	err = nft_chain_validate_dependency(ctx->chain, NFT_CHAIN_T_NAT);
	if (err < 0)
		return err;

	if (tb[NFTA_MASQ_FLAGS] == NULL)
		return 0;

	priv->flags = ntohl(nla_get_be32(tb[NFTA_MASQ_FLAGS]));
	if (priv->flags & ~NF_NAT_RANGE_MASK)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(nft_masq_init);

int nft_masq_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_masq *priv = nft_expr_priv(expr);

	if (priv->flags == 0)
		return 0;

	if (nla_put_be32(skb, NFTA_MASQ_FLAGS, htonl(priv->flags)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}
EXPORT_SYMBOL_GPL(nft_masq_dump);

int nft_masq_validate(const struct nft_ctx *ctx, const struct nft_expr *expr,
		      const struct nft_data **data)
{
	return nft_chain_validate_dependency(ctx->chain, NFT_CHAIN_T_NAT);
}
EXPORT_SYMBOL_GPL(nft_masq_validate);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arturo Borrero Gonzalez <arturo.borrero.glez@gmail.com>");
