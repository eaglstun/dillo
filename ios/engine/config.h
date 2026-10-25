/*
 * Minimal config.h for the iOS engine build (replaces the autoconf-generated
 * one; the engine subset only reads these few symbols).
 */
#ifndef __IOS_CONFIG_H__
#define __IOS_CONFIG_H__

#define PACKAGE "dillo"
#define VERSION "3.3.0-rc1"

#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1

/* Apple's libiconv takes char** (the "newer libiconv" case in configure.ac) */
#define inbuf_t char

#endif /* __IOS_CONFIG_H__ */
