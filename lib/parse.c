/* Copyright 2007-2010 Jozsef Kadlecsik (kadlec@blackhole.kfki.hu)
 *
 * This program is free software; you can redistribute it and/or modify   
 * it under the terms of the GNU General Public License version 2 as 
 * published by the Free Software Foundation.
 */
#include <assert.h>				/* assert */
#include <errno.h>				/* errno */
#include <limits.h>				/* ULLONG_MAX */
#include <netdb.h>				/* getservbyname, getaddrinfo */
#include <stdlib.h>				/* strtoull, etc. */
#include <sys/types.h>				/* getaddrinfo */
#include <sys/socket.h>				/* getaddrinfo, AF_ */
#include <net/ethernet.h>			/* ETH_ALEN */

#include <libipset/data.h>			/* IPSET_OPT_* */
#include <libipset/pfxlen.h>			/* prefixlen_netmask_map */
#include <libipset/session.h>			/* ipset_err */
#include <libipset/types.h>			/* ipset_type_get */
#include <libipset/utils.h>			/* string utilities */
#include <libipset/parse.h>			/* prototypes */

/* Parse input data */

#define ipset_cidr_separator(str)	ipset_strchr(str, IPSET_CIDR_SEPARATOR)
#define ipset_range_separator(str)	ipset_strchr(str, IPSET_RANGE_SEPARATOR)
#define ipset_elem_separator(str)	ipset_strchr(str, IPSET_ELEM_SEPARATOR)
#define ipset_name_separator(str)	ipset_strchr(str, IPSET_NAME_SEPARATOR)

#define syntax_err(fmt, args...) \
	ipset_err(session, "Syntax error: " fmt , ## args)

/* 
 * Parser functions, shamelessly taken from iptables.c, ip6tables.c 
 * and parser.c from libnetfilter_conntrack.
 */

/*
 * Parse numbers
 */
static int
string_to_number_ll(struct ipset_session *session,
		    const char *str, 
		    unsigned long long min,
		    unsigned long long max,
		    unsigned long long *ret)
{
	unsigned long long number = 0;
	char *end;

	/* Handle hex, octal, etc. */
	errno = 0;
	number = strtoull(str, &end, 0);
	if (*end == '\0' && end != str && errno != ERANGE) {
		/* we parsed a number, let's see if we want this */
		if (min <= number && (!max || number <= max)) {
			*ret = number;
			return 0;
		} else
			errno = ERANGE;
	}
	if (errno == ERANGE && max)
		return syntax_err("'%s' is out of range %llu-%llu",
				  str, min, max);
	else if (errno == ERANGE)
		return syntax_err("'%s' is out of range %llu-%llu",
				  str, min, ULLONG_MAX);
	else
		return syntax_err("'%s' is invalid as number", str);
}

static int
string_to_number_l(struct ipset_session *session,
		   const char *str, 
		   unsigned long min,
		   unsigned long max,
		   unsigned long *ret)
{
	int err;
	unsigned long long number = 0;

	err = string_to_number_ll(session, str, min, max, &number);
	*ret = (unsigned long) number;

	return err;
}

static int
string_to_number(struct ipset_session *session,
		 const char *str, 
		 unsigned int min, 
		 unsigned int max,
		 unsigned int *ret)
{
	int err;
	unsigned long number = 0;

	err = string_to_number_l(session, str, min, max, &number);
	*ret = (unsigned int) number;

	return err;
}

/**
 * ipset_parse_ether - parse ethernet address
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as an ethernet address. The parsed ethernet
 * address is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_ether(struct ipset_session *session,
		  enum ipset_opt opt, const char *str)
{
	unsigned int i = 0;
	unsigned char ether[ETH_ALEN];
	
	assert(session);
	assert(opt == IPSET_OPT_ETHER);
	assert(str);

	if (strlen(str) != ETH_ALEN * 3 - 1)
		goto error;

	for (i = 0; i < ETH_ALEN; i++) {
		long number;
		char *end;

		number = strtol(str + i * 3, &end, 16);

		if (end == str + i * 3 + 2
		    && (*end == ':' || *end == '\0')
		    && number >= 0 && number <= 255)
			ether[i] = number;
		else
			goto error;
	}
	return ipset_session_data_set(session, opt, ether);

error:
	return syntax_err("cannot parse '%s' as ethernet address", str);
}

/*
 * Parse TCP service names or port numbers
 */
static int
parse_portname(struct ipset_session *session, const char *str, uint16_t *port)
{
	struct servent *service;

	if ((service = getservbyname(str, "tcp")) != NULL) {
		*port = ntohs((uint16_t) service->s_port);
		return 0;
	}
	
	return syntax_err("cannot parse '%s' as a (TCP) port", str);
}

static int
parse_portnum(struct ipset_session *session, const char *str, uint16_t *port)
{
	return string_to_number(session, str, 0, 65535, (unsigned int *)port);
}

/**
 * ipset_parse_single_port - parse a single (TCP) port number or name
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as a single (TCP) port number or name. The parsed port
 * number is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_single_port(struct ipset_session *session,
			enum ipset_opt opt, const char *str)
{
	uint16_t port;
	int err;

	assert(session);
	assert(opt == IPSET_OPT_PORT || opt == IPSET_OPT_PORT_TO);
	assert(str);

	if ((err = parse_portnum(session, str, &port)) == 0
	    || (err = parse_portname(session, str, &port)) == 0)
		err = ipset_session_data_set(session, opt, &port);

	if (!err)
		/* No error, so reset session messages! */
		ipset_session_report_reset(session);

	return err;
}

/**
 * ipset_parse_port - parse (TCP) port name, number, or range of them
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as a TCP port name or number or range of them.
 * separated by a dash. The parsed port numbers are stored
 * in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_port(struct ipset_session *session,
		 enum ipset_opt opt, const char *str)
{
	char *a, *saved, *tmp;
	int err = 0;

	assert(session);
	assert(opt == IPSET_OPT_PORT);
	assert(str);

	saved = tmp = strdup(str);
	if (tmp == NULL)
		return ipset_err(session,
				 "Cannot allocate memory to duplicate %s.",
				 str);

	a = ipset_range_separator(tmp);
	if (a != NULL) {
		/* port-port */
		*a++ = '\0';
		err = ipset_parse_single_port(session, IPSET_OPT_PORT_TO, a);
		if (err)
			goto error;
	}
	err = ipset_parse_single_port(session, opt, tmp);

error:
	free(saved);
	return err;
}

/**
 * ipset_parse_family - parse INET|INET6 family names
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as an INET|INET6 family name.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_family(struct ipset_session *session, int opt, const char *str)
{
	uint8_t family;
	
	assert(session);
	assert(opt == IPSET_OPT_FAMILY);
	assert(str);

	if (STREQ(str, "inet") || STREQ(str, "ipv4") || STREQ(str, "-4"))
		family = AF_INET;
	else if (STREQ(str, "inet6") || STREQ(str, "ipv6") || STREQ(str, "-6"))
		family = AF_INET6;
	else if (STREQ(str, "any") || STREQ(str, "unspec"))
		family = AF_UNSPEC;
	else
		return syntax_err("unknown INET family %s", str);
				
	return ipset_session_data_set(session, opt, &family);
}

/*
 * Parse IPv4/IPv6 addresses, networks and ranges
 * We resolve hostnames but just the first IP address is used.
 */
 
static struct addrinfo *
get_addrinfo(struct ipset_session *session, const char *str, uint8_t family)
{
	struct addrinfo hints;
        struct addrinfo *res;
	int err;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
        hints.ai_family = family;
        hints.ai_socktype = SOCK_RAW;
        hints.ai_protocol = 0;
        hints.ai_next = NULL;

        if ((err = getaddrinfo(str, NULL, &hints, &res)) != 0) {
        	syntax_err("cannot resolve '%s' to an %s address: %s",
        		   str, family == AF_INET6 ? "IPv6" : "IPv4",
        		   gai_strerror(err));
        	return NULL;
	} else
		return res;
}

#define GET_ADDRINFO(family, IP, f, n)					\
static int								\
get_addrinfo##f(struct ipset_session *session,				\
		const char *str,					\
        	struct addrinfo **info,					\
		struct in##n##_addr **inaddr)				\
{									\
        struct addrinfo *i;						\
	struct sockaddr_in##n *saddr;					\
        int found;							\
									\
	if ((*info = get_addrinfo(session, str, family)) == NULL) {	\
		syntax_err("cannot parse %s: resolving "		\
			   IP " failed", str);				\
		return EINVAL;						\
	}								\
									\
	for (i = *info, found = 0; i != NULL; i = i->ai_next) {		\
		if (i->ai_family != family)				\
			continue;					\
		if (found == 0) {					\
			saddr = (struct sockaddr_in##n *)i->ai_addr;	\
			*inaddr = &saddr->sin##n##_addr;		\
		} else if (found == 1) {					\
			ipset_warn(session,				\
				   "%s resolves to multiple addresses: "  \
				   "using only the first one returned by the resolver", \
				   str);			\
		}							\
		found++;						\
	}								\
	if (found == 0)							\
		return syntax_err("cannot parse %s: "			\
				  IP "address could not be resolved",	\
				  str);					\
	return 0;							\
}

#define PARSE_IP(mask, f, n)						\
static int								\
parse_ipv##f(struct ipset_session *session,				\
	     enum ipset_opt opt, const char *str) 			\
{									\
        unsigned int m = mask;						\
        int aerr = EINVAL, err = 0, range = 0;				\
        char *saved = strdup(str);					\
        char *a, *tmp = saved;						\
        struct addrinfo *info;						\
        struct in##n##_addr *inaddr;					\
        struct ipset_data *data = ipset_session_data(session);		\
        enum ipset_opt copt = opt == IPSET_OPT_IP ? IPSET_OPT_CIDR	\
        			: IPSET_OPT_CIDR2;			\
									\
	if (tmp == NULL)						\
		return ipset_err(session,				\
				 "Cannot allocate memory to duplicate %s.",\
				 str);					\
	if ((a = ipset_cidr_separator(tmp)) != NULL) {			\
		/* IP/mask */						\
		*a++ = '\0';						\
									\
		if ((err = string_to_number(session, a, 0, m, &m)) != 0	\
		    || (err = ipset_data_set(data, copt, &m)) != 0)	\
			goto out;					\
	} else if ((a = ipset_range_separator(tmp)) != NULL) {		\
		/* IP-IP */						\
		*a++ = '\0';						\
		D("range %s", a);					\
		range++;						\
	}								\
	if ((aerr = get_addrinfo##f(session, tmp, &info, &inaddr)) != 0	\
	    || (err = ipset_data_set(data, opt, inaddr)) != 0		\
	    || !range)							\
		goto out;						\
	freeaddrinfo(info);						\
	if ((aerr = get_addrinfo##f(session, a, &info, &inaddr)) == 0)	\
		err = ipset_data_set(data, IPSET_OPT_IP_TO, inaddr);	\
									\
out:									\
	if (aerr != EINVAL)						\
		/* getaddrinfo not failed */				\
		freeaddrinfo(info);					\
	else if (aerr)							\
		err = -1;						\
	free(saved);							\
	return err;							\
} 

GET_ADDRINFO(AF_INET, "IPv4", 4, )
PARSE_IP(32, 4, )

GET_ADDRINFO(AF_INET6, "IPv6", 6, 6)
PARSE_IP(128, 6, 6)

enum ipaddr_type {
	IPADDR_ANY,
	IPADDR_PLAIN,
	IPADDR_NET,
	IPADDR_RANGE,
};

static int
parse_ip(struct ipset_session *session,
	 enum ipset_opt opt, const char *str, enum ipaddr_type addrtype)
{
	int err = 0;
	struct ipset_data *data = ipset_session_data(session);
	uint8_t family = ipset_data_family(data);

	if (family == AF_UNSPEC) {
		family = AF_INET;
		ipset_data_set(data, IPSET_OPT_FAMILY, &family);
	}

	switch (addrtype) {
	case IPADDR_PLAIN:
		if (ipset_range_separator(str) || ipset_cidr_separator(str))
			return syntax_err("plain IP address must be supplied: %s",
					  str);
		break;
	case IPADDR_NET:
		if (!ipset_cidr_separator(str) || ipset_range_separator(str))
			return syntax_err("IP/netblock must be supplied: %s",
					  str);
		break;
	case IPADDR_RANGE:
		if (!ipset_range_separator(str) || ipset_cidr_separator(str))
			return syntax_err("IP-IP range must supplied: %s",
					  str);
		break;
	case IPADDR_ANY:
	default:
		break;
	}

	if (family == AF_INET)
		err = parse_ipv4(session, opt, str);
	else
		err = parse_ipv6(session, opt, str);

	return err;
}

/**
 * ipset_parse_ip - parse IPv4|IPv6 address, range or netblock
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as an IPv4|IPv6 address or address range
 * or netblock. Hostnames are resolved. If family is not set
 * yet in the data blob, INET is assumed.
 * The values are stored in the data blob of the session.
 *
 * FIXME: if the hostname resolves to multiple addresses,
 * the first one is used only.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_ip(struct ipset_session *session,
	       enum ipset_opt opt, const char *str)
{
	assert(session);
	assert(opt == IPSET_OPT_IP || opt == IPSET_OPT_IP2);
	assert(str);

	return parse_ip(session, opt, str, IPADDR_ANY);
}

/**
 * ipset_parse_single_ip - parse a single IPv4|IPv6 address
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as an IPv4|IPv6 address or hostname. If family 
 * is not set yet in the data blob, INET is assumed.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_single_ip(struct ipset_session *session,
		      enum ipset_opt opt, const char *str)
{
	assert(session);
	assert(opt == IPSET_OPT_IP
	       || opt == IPSET_OPT_IP_TO
	       || opt == IPSET_OPT_IP2);
	assert(str);

	return parse_ip(session, opt, str, IPADDR_PLAIN);
}

/**
 * ipset_parse_net - parse IPv4|IPv6 address/cidr
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as an IPv4|IPv6 address/cidr pattern. If family 
 * is not set yet in the data blob, INET is assumed.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_net(struct ipset_session *session,
		enum ipset_opt opt, const char *str)
{
	assert(session);
	assert(opt == IPSET_OPT_IP || opt == IPSET_OPT_IP2);
	assert(str);

	return parse_ip(session, opt, str, IPADDR_NET);
}

/**
 * ipset_parse_range - parse IPv4|IPv6 ranges
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as an IPv4|IPv6 range separated by a dash. If family
 * is not set yet in the data blob, INET is assumed.
 * The values are stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_range(struct ipset_session *session,
		  enum ipset_opt opt, const char *str)
{
	assert(session);
	assert(opt == IPSET_OPT_IP);
	assert(str);

	return parse_ip(session, IPSET_OPT_IP, str, IPADDR_RANGE);
}

/**
 * ipset_parse_netrange - parse IPv4|IPv6 address/cidr or range
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as an IPv4|IPv6 address/cidr pattern or a range
 * of addresses separated by a dash. If family is not set yet in
 * the data blob, INET is assumed.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_netrange(struct ipset_session *session,
		     enum ipset_opt opt, const char *str)
{
	assert(session);
	assert(opt == IPSET_OPT_IP);
	assert(str);

	if (!(ipset_range_separator(str) || ipset_cidr_separator(str)))
		return syntax_err("IP/net or IP-IP range must be specified: %s",
				  str);
	return parse_ip(session, opt, str, IPADDR_ANY);
}

#define check_setname(str, saved)					\
do {									\
    if (strlen(str) > IPSET_MAXNAMELEN - 1) {				\
    	if (saved != NULL)						\
    		free(saved);						\
	return syntax_err("setname '%s' is longer than %u characters",	\
			  str, IPSET_MAXNAMELEN - 1);			\
    }									\
} while (0)


/**
 * ipset_parse_name - parse setname as element
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as a setname or a setname element to add to a set.
 * The pattern "setname,before|after,setname" is recognized and
 * parsed.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_name(struct ipset_session *session,
		 enum ipset_opt opt, const char *str)
{
	char *saved;
	char *a = NULL, *b = NULL, *tmp;
	int err, before = 0;
	const char *sep = IPSET_ELEM_SEPARATOR;
	struct ipset_data *data;

	assert(session);
	assert(opt == IPSET_OPT_NAME || opt == IPSET_OPT_SETNAME2);
	assert(str);

	data = ipset_session_data(session);
	if (opt == IPSET_OPT_SETNAME2) {
		check_setname(str, NULL);
		
		return ipset_data_set(data, opt, str);
	}

	tmp = saved = strdup(str);	
	if (saved == NULL)
		return ipset_err(session,
				 "Cannot allocate memory to duplicate %s.",
				 str);
	if ((a = ipset_elem_separator(tmp)) != NULL) {
		/* setname,[before|after,setname */
		*a++ = '\0';
		if ((b = ipset_elem_separator(a)) != NULL)
			*b++ = '\0';
		if (b == NULL
		    || !(STREQ(a, "before") || STREQ(a, "after"))) {
			err = ipset_err(session, "you must specify elements "
					"as setname%s[before|after]%ssetname",
					sep, sep);
			goto out;
		}
		before = STREQ(a, "before");
	}
	check_setname(tmp, saved);
	if ((err = ipset_data_set(data, opt, tmp)) != 0 || b == NULL)
		goto out;

	check_setname(b, saved);
	if ((err = ipset_data_set(data,
				  IPSET_OPT_NAMEREF, b)) != 0)
		goto out;
	
	if (before)
		err = ipset_data_set(data, IPSET_OPT_BEFORE, &before);

out:
	free(saved);
	return err;
}

/**
 * ipset_parse_setname - parse name as the name of the (current) set
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as the name of the (current) set.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_setname(struct ipset_session *session,
		    enum ipset_opt opt, const char *str)
{
	assert(session);
	assert(opt == IPSET_SETNAME);
	assert(str);

	check_setname(str, NULL);

	return ipset_session_data_set(session, opt, str);
}

/**
 * ipset_parse_uint32 - parse string as an unsigned integer
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as an unsigned integer number.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_uint32(struct ipset_session *session,
		   enum ipset_opt opt, const char *str)
{
	uint32_t value;
	int err;
	
	assert(session);
	assert(str);

	if ((err = string_to_number(session, str, 0, 0, &value)) == 0)
		return ipset_session_data_set(session, opt, &value);
	
	return err;
}

/**
 * ipset_parse_uint8 - parse string as an unsigned short integer
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as an unsigned short integer number.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_uint8(struct ipset_session *session,
		  enum ipset_opt opt, const char *str)
{
	unsigned int value;
	int err;
	
	assert(session);
	assert(str);

	if ((err = string_to_number(session, str, 0, 255, &value)) == 0)
		return ipset_session_data_set(session, opt, &value);

	return err;
}

/**
 * ipset_parse_netmask - parse string as a CIDR netmask value
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as a CIDR netmask value, depending on family type.
 * If family is not set yet, INET is assumed.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_netmask(struct ipset_session *session,
		    enum ipset_opt opt, const char *str)
{
	unsigned int family, cidr;
	struct ipset_data *data;
	int err = 0;
	
	assert(session);
	assert(opt == IPSET_OPT_NETMASK);
	assert(str);

	data = ipset_session_data(session);
	family = ipset_data_family(data);
	if (family == AF_UNSPEC) {
		family = AF_INET;
		ipset_data_set(data, IPSET_OPT_FAMILY, &family);
	}

	err = string_to_number(session, str,
			       family == AF_INET ? 1 : 4, 
			       family == AF_INET ? 31 : 124,
			       &cidr);

	if (err)
		return syntax_err("netmask is out of the inclusive range "
				  "of %u-%u",
				  family == AF_INET ? 1 : 4,
				  family == AF_INET ? 31 : 124);

	return ipset_data_set(data, opt, &cidr);
}

/**
 * ipset_parse_flag - "parse" option flags
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse option flags :-)
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_flag(struct ipset_session *session,
		 enum ipset_opt opt, const char *str UNUSED)
{
	assert(session);
	
	return ipset_session_data_set(session, opt, NULL);
}

/**
 * ipset_parse_type - parse ipset type name
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse ipset module type: supports both old and new formats.
 * The type name is looked up and the type found is stored
 * in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_typename(struct ipset_session *session,
		     enum ipset_opt opt, const char *str)
{
	const struct ipset_type *type;
	const char *typename;

	assert(session);
	assert(opt == IPSET_OPT_TYPENAME);
	assert(str);

	if (strlen(str) > IPSET_MAXNAMELEN - 1)
		return syntax_err("typename '%s' is longer than %u characters",
				  str, IPSET_MAXNAMELEN - 1);

	/* Find the corresponding type */
	typename = ipset_typename_resolve(str);
	if (typename == NULL)
		return syntax_err("typename '%s' is unkown", str);
	ipset_session_data_set(session, IPSET_OPT_TYPENAME, typename);
	type = ipset_type_get(session, IPSET_CMD_CREATE);

	if (type == NULL)
		return -1;
	
	return ipset_session_data_set(session, IPSET_OPT_TYPE, type);
}

/**
 * ipset_parse_output - parse output format name
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse output format names and set session mode.
 * The value is stored in the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_output(struct ipset_session *session,
		   int opt UNUSED, const char *str)
{
	assert(session);
	assert(str);

	if (STREQ(str, "plain"))
		return ipset_session_output(session, IPSET_LIST_PLAIN);
	else if (STREQ(str, "xml"))
		return ipset_session_output(session, IPSET_LIST_XML);
	else if (STREQ(str, "save"))
		return ipset_session_output(session, IPSET_LIST_SAVE);

	return syntax_err("unkown output mode '%s'", str);
}

#define parse_elem(s, t, d, str)					\
do { 									\
	if (!t->elem[d].parse)						\
		goto internal;						\
	err = t->elem[d].parse(s, t->elem[d].opt, str);			\
	if (err)							\
		goto out;						\
} while (0)

/**
 * ipset_parse_elem - parse ADT elem, depending on settype
 * @session: session structure
 * @opt: option kind of the data
 * @str: string to parse
 *
 * Parse string as a (multipart) element according to the settype.
 * The value is stored in the data blob of the session.
 *
 * Returns 0 on success or a negative error code.
 */
int
ipset_parse_elem(struct ipset_session *session,
		 enum ipset_opt optional, const char *str)
{
	const struct ipset_type *type;
	char *a = NULL, *b = NULL, *tmp, *saved;
	int err;

	assert(session);
	assert(str);

	type = ipset_session_data_get(session, IPSET_OPT_TYPE);
	if (!type)
		return ipset_err(session,
				 "Internal error: set type is unknown!");

	saved = tmp = strdup(str);
	if (tmp == NULL)
		return ipset_err(session,
				 "Cannot allocate memory to duplicate %s.",
				 str);

	a = ipset_elem_separator(tmp);
	if (type->dimension > IPSET_DIM_ONE) {
		if (a != NULL) {
			/* elem,elem */
			*a++ = '\0';
		} else if (type->dimension > IPSET_DIM_TWO && !optional) {
			free(tmp);
			return syntax_err("Second element is missing from %s.",
					  str);
		}
	} else if (a != NULL)
		return syntax_err("Elem separator in %s, "
				  "but settype %s supports none.",
				  str, type->name);

	if (a)
		b = ipset_elem_separator(a);
	if (type->dimension > IPSET_DIM_TWO) {
		if (b != NULL) {
			/* elem,elem,elem */
			*b++ = '\0';
		} else if (!optional) {
			free(tmp);
			return syntax_err("Third element is missing from %s.",
					  str);
		}
	} else if (b != NULL)
		return syntax_err("Two elem separators in %s, "
				  "but settype %s supports one.",
				  str, type->name);
	if (b != NULL && ipset_elem_separator(b))
		return syntax_err("Three elem separators in %s, "
				  "but settype %s supports two.",
				  str, type->name);

	D("parse elem part one: %s", tmp);
	parse_elem(session, type, IPSET_DIM_ONE, tmp);

	if (type->dimension > IPSET_DIM_ONE && a != NULL) {
		D("parse elem part two: %s", a);
		parse_elem(session, type, IPSET_DIM_TWO, a);
	}
	if (type->dimension > IPSET_DIM_TWO && b != NULL)
		parse_elem(session, type, IPSET_DIM_THREE, b);

	goto out;

internal:
	err = ipset_err(session,
			"Internal error: missing parser function for %s",
			type->name);
out:
	free(saved);
	return err;
}