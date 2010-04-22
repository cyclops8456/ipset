/* Copyright 2007-2010 Jozsef Kadlecsik (kadlec@blackhole.kfki.hu)
 *
 * This program is free software; you can redistribute it and/or modify   
 * it under the terms of the GNU General Public License version 2 as 
 * published by the Free Software Foundation.
 */
#include <libipset/data.h>			/* IPSET_OPT_* */
#include <libipset/parse.h>			/* parser functions */
#include <libipset/print.h>			/* printing functions */
#include <libipset/types.h>			/* prototypes */

/* Parse commandline arguments */
static const struct ipset_arg bitmap_port_create_args[] = {
	{ .name = { "range", "--range", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_PORT,
	  .parse = ipset_parse_port,		.print = ipset_print_port,
	},
	{ .name = { "timeout", "--timeout", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_TIMEOUT,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	/* Backward compatibility */
	{ .name = { "--from", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_PORT,
	  .parse = ipset_parse_single_port,
	},
	{ .name = { "--to", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_PORT_TO,
	  .parse = ipset_parse_single_port,
	},
	{ },
}; 

static const struct ipset_arg bitmap_port_add_args[] = {
	{ .name = { "timeout", "--timeout", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_TIMEOUT,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	{ },
}; 

static const char bitmap_port_usage[] =
"create SETNAME bitmap:port range FROM-TO\n"
"               [timeout VALUE]\n"
"add    SETNAME PORT|FROM-TO [timeout VALUE]\n"
"del    SETNAME PORT|FROM-TO\n"
"test   SETNAME PORT\n";

struct ipset_type ipset_bitmap_port0 = {
	.name = "bitmap:port",
	.alias = "portmap",
	.revision = 0,
	.family = AF_UNSPEC,
	.dimension = IPSET_DIM_ONE,
	.elem = { 
		[IPSET_DIM_ONE] = { 
			.parse = ipset_parse_port,
			.print = ipset_print_port,
			.opt = IPSET_OPT_PORT
		},
	},
	.args = {
		[IPSET_CREATE] = bitmap_port_create_args,
		[IPSET_ADD] = bitmap_port_add_args,
	},
	.mandatory = {
		[IPSET_CREATE] = IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO),
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_PORT),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_PORT),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_PORT),
	},
	.full = {
		[IPSET_CREATE] = IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_PORT),
	},

	.usage = bitmap_port_usage,
};
