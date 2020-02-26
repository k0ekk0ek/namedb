/*
 * tree.h -- adaptive radix tree optimized for domain names
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#ifndef NSD_TREE_H
#define NSD_TREE_H

#include <stdbool.h>
#include <stdint.h>

#define NSD_RETCODES(X) \
  X(ok, 0, "Success") \
  X(no_memory, -1, "Out of memory") \
  X(bad_parameter, -2, "Bad parameter") \
  X(not_found, 1, "Not found")

#define NSD_RETCODE_ENUM(label, value, ...) \
  nsd_ ## label = value,

typedef enum nsd_retcode nsd_retcode_t;
enum nsd_retcode {
  NSD_RETCODES(NSD_RETCODE_ENUM)
};

/* Adaptive Radix Tree (ART) structures cannot store prefixes of other keys.
 * Therefore the tree cannot be used to store domain name data by default. The
 * recommended solution is to terminate every key with a value that does not
 * occur anywhere else in the set. However, domain names consist of labels of
 * octets and each octet can have any value between 0x00 and 0xff. Domain
 * names must therefore be transformed before they can serve as keys. The fact
 * that comparisons between character strings must be done in a
 * case-insensitive manner (RFC 1035 section 2.3.3) is used to avoid
 * multi-byte encoding schemes. Uppercase US-ASCII letters are converted to
 * lowercase US-ASCII letters and 0x01 is added to any octet with a value less
 * than 0x41. 0x00 can then be used to terminate keys and separate labels,
 * preserving canonical name order (RFC 4034 secion 6.1). 0x19 is subtracted
 * from every octet with a value greater than 0x90 so that nodes require less
 * space. The fact that paths to domain names under each cut pass through a
 * single node is also a useful property for concurrent access scenarios and
 * improves lookup speeds.
 *
 * Transformations:
 *  - Order of labels is reversed to maintain hierarchy.
 *  - Uppercase US-ASCII letters are converted to lowercase US-ASCII letters.
 *  - 0x01 is added to octets with values less than 0x41.
 *  - Length octets are converted to 0x00 to preserve order.
 *     Eliminates the need to keep pointers to adjacent domain names.
 *  - 0x19 is subtracted from octets with values greater than 0x90.
 *  - Key is null-terminated so that it is never a prefix for subsequent keys.
 *     0 (zero) serves as an index in inner nodes as well.
 *
 * Examples (numbers are bytes, letters are ascii):
 *  - root:        dname: "0",             key: "0"
 *  - fOo.:        dname: "3fOo0",         key: "MVV00"
 *  - bAr.foo:     dname: "3bAr3foo0",     key: "MVV0IHY00"
 *  - a.bar.fOo:   dname: "1a3bar3fOo0",   key: "MVV0IHY0H00"
 *  - ab.bAr.foo:  dname: "2ab3bAr3foo0",  key: "MVV0IHY0HI00"
 *  - b.bar.fOo:   dname: "1b3bar3fOo0",   key: "MVV0IHY0I00"
 */

#define NSD_MAX_HEIGHT (255) /* Domain names are limited to 255 octets. */

typedef uint8_t nsd_key_t[NSD_MAX_HEIGHT];

/* Octets can have any value between 0 and 255, but uppercase letters are
 * converted to lowercase for lookup and 0 is reserved as a terminator, hence
 * the maximum width after conversion is 230.
 */
#define NSD_MAX_WIDTH (230)

#define NSD_MAX_PREFIX (8)

typedef enum nsd_node_type nsd_node_type_t;
/** Node types used in the tree */
enum nsd_node_type {
  nsd_node4, /**< Default (smallest) node */
  nsd_node16, /**< Node to leverage 128-bit SIMD instructions */
  nsd_node32, /**< Node to leverage 256-bit SIMD instructions (if available) */
  /* Octets can have any value between 0x00 and 0xff, but most domain names
   * stick to the preferred syntax as outlined in RFC 1035 section 2.3.1.
   */
  nsd_node38, /**< Node that stores hostnames exclusively */
  nsd_node48,
  nsd_node256
};

typedef struct nsd_node nsd_node_t;
struct nsd_node {
  nsd_node_type_t type;
  uint8_t width;
  uint8_t prefix_len;
  uint8_t prefix[NSD_MAX_PREFIX];
};

typedef struct nsd_node4 nsd_node4_t;
struct nsd_node4 {
  nsd_node_t base;
  uint8_t keys[4];
  nsd_node_t *children[4];
};

typedef struct nsd_node16 nsd_node16_t;
struct nsd_node16 {
  nsd_node_t base;
  uint8_t keys[16];
  nsd_node_t *children[16];
};

/* Used if CPU supports AVX2 extensions. */
typedef struct nsd_node32 nsd_node32_t;
struct nsd_node32 {
  nsd_node_t base;
  uint8_t keys[32];
  nsd_node_t *children[32];
};

/* Used if a node stores hostname keys exclusively. */
typedef struct nsd_node38 nsd_node38_t;
struct nsd_node38 {
  nsd_node_t base;
  nsd_node_t *children[38];
};

typedef struct nsd_node48 nsd_node48_t;
struct nsd_node48 {
  nsd_node_t base;
  uint8_t keys[NSD_MAX_WIDTH];
  nsd_node_t *children[48];
};

typedef struct nsd_node256 nsd_node256_t;
struct nsd_node256 {
  nsd_node_t base;
  nsd_node_t *children[NSD_MAX_WIDTH];
};

typedef struct nsd_leaf nsd_leaf_t;
struct nsd_leaf {
  void *data;
  uint8_t key_len;
  uint8_t key[]; /* dynamically sized, avoids use of a pointer */
};

inline bool
nsd_is_leaf(const nsd_node_t *node)
{
  return (((uintptr_t)node & 1));
}

inline nsd_leaf_t *
nsd_leaf_raw(const nsd_node_t *node)
{
  return ((nsd_leaf_t*)((void*)((uintptr_t)node & ~1)));
}

typedef struct nsd_level nsd_level_t;
struct nsd_level {
  uint8_t depth;
  nsd_node_t **noderef;
};

typedef struct nsd_path nsd_path_t;
struct nsd_path {
  uint8_t height;
  nsd_level_t levels[NSD_MAX_HEIGHT];
};

typedef struct nsd_tree nsd_tree_t;
struct nsd_tree {
  nsd_node_t *root;
};

/**
 * @brief Create key suitable for tree
 *
 * @param[out]  key   Key
 * @param[in]   name  Domain name in wire format
 *
 * @returns Length of key in octets or 0 if name is invalid
 */
uint8_t
nsd_make_key(nsd_key_t key, const uint8_t *name)
__attribute__((nonnull));

/**
 * @brief Find key and register nodes in the path
 *
 * @param[in]      tree     Tree
 * @param[in,out]  path     Path
 * @param[in]      key      Key previously created with @nsd_make_key
 * @param[in]      key_len  Length of specified key
 *
 * @returns @nsd_retcode_t indicating success or failure
 *
 * @retval @nsd_ok
 *   Key exists, path recorded in @path
 * @retval @nsd_not_found
 *   Key does not exist, maximum path recorded in @path
 */
nsd_retcode_t
nsd_find_path(
  nsd_tree_t *tree,
  nsd_path_t *path,
  const nsd_key_t key,
  uint8_t key_len)
__attribute__((nonnull(1,2)));

/**
 * @brief Create key and register nodes in path
 *
 * @param[in]      tree     Tree
 * @param[in,out]  path     Path
 * @param[in]      key      Key previously created with @nsd_make_key
 * @param[in]      key_len  Length of specified key
 *
 * @returns @nsd_retcode_t indicating success or failure
 *
 * @retval @nsd_ok
 *   Key is created or existed alread, path registered in @path
 * @retval @nsd_no_memory
 *   Key cannot be created, insufficient memory was available
 */
nsd_retcode_t
nsd_make_path(
  nsd_tree_t *tree,
  nsd_path_t *path,
  const nsd_key_t key,
  uint8_t key_len)
__attribute__((nonnull(1,2)));

#endif /* NSD_TREE_H */
