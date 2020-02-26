/*
 * tree.c -- adaptive radix tree optimized for domain names
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "simd.h"
#include "tree.h"

#define SET_LEAF(x) ((void*)((uintptr_t)x | 1))

extern inline bool nsd_is_leaf(const nsd_node_t *);
extern inline nsd_leaf_t *nsd_leaf_raw(const nsd_node_t *);

static uint8_t
xlat(uint8_t oct)
{
  if (oct < 0x41u) {
    return oct + 0x01u;
  } else if (oct < 0x5bu) {
    return oct + 0x07u;
  }
  return oct - 0x19u;
}

/* translate key to node38 index */
static inline uint8_t
node38_xlat(uint8_t key)
{
  if (key >= 0x48u && key <= 0x61u) { /* "a..z" */
    return key - 0x3cu;
  } else if (key >= 0x31u && key <= 0x3au) { /* "0..9" */
    return key - 0x2fu;
  } else if (key == 0x2eu) { /* "-" */
    return 0x01u;
  } else if (key == 0x00u) {
    return 0x00u;
  }

  return (uint8_t)-1;
}

/* translate node38 index to key */
static inline uint8_t
node38_unxlat(uint8_t key)
{
  if (key >= 0x0bu && key <= 0x25u) { /* "a..z" */
    return key + 0x3cu;
  } else if (key >= 0x02u && key <= 0x0bu) { /* "0..9" */
    return key + 0x2fu;
  } else if (key == 0x01u) { /* "-" */
    return 0x2eu;
  } else if (key == 0x00u) {
    return 0x00u;
  }

  return (uint8_t)-1;
}

uint8_t
nsd_make_key(nsd_key_t key, const uint8_t *name)
{
  size_t cnt = 0, len = 0;
  uint8_t *ptr;

  assert(key != NULL);
  assert(name != NULL);

  ptr = key - 1; /* 1st octet never written */
  while (name[cnt] != 0x00) {
    if ((name[cnt] & 0xc0u) || ((len += name[cnt] + 1) > 0xffu)) {
      return 0;
    }
    for (++cnt; cnt < len; cnt++) {
      ptr[cnt] = xlat(name[cnt]);
    }
    ptr[cnt] = 0x00u; /* null-terminate label */
  }
  ptr[++cnt] = 0x00u; /* null-terminate key */

  return cnt;
}

static uint8_t
compare_keys(
  const uint8_t *restrict key1,
  uint8_t key1_len,
  const uint8_t *restrict key2,
  uint8_t key2_len)
{
  uint8_t cnt = 0, len = key1_len < key2_len ? key1_len : key2_len;

  for (cnt = 0; cnt < len && key1[cnt] == key2[cnt]; cnt++) {
    /* do nothing */
  }

  return cnt;
}

static void *alloc_node(nsd_node_type_t type)
{
  size_t size;
  nsd_node_t *node;

  switch (type) {
    case nsd_node4:
      size = sizeof(nsd_node4_t);
      break;
    case nsd_node16:
      size = sizeof(nsd_node16_t);
      break;
    case nsd_node32:
      size = sizeof(nsd_node32_t);
      break;
    case nsd_node38:
      size = sizeof(nsd_node38_t);
      break;
    case nsd_node48:
      size = sizeof(nsd_node48_t);
      break;
    case nsd_node256:
      size = sizeof(nsd_node256_t);
      break;
    default:
      abort();
  }

  if ((node = calloc(1, size)) != NULL) {
    node->type = type;
  }

  return node;
}

static void copy_header(nsd_node_t *dest, nsd_node_t *src)
{
  dest->width = src->width;
  memcpy(dest->prefix, src->prefix, src->prefix_len);
  dest->prefix_len = src->prefix_len;
}

static nsd_leaf_t *make_leaf(const nsd_key_t key, uint8_t key_len)
{
  size_t size;
  nsd_leaf_t *leaf;

  size = sizeof(nsd_leaf_t) + key_len;
  if ((leaf = malloc(size)) == NULL) {
    return NULL;
  }

  leaf->data = NULL;
  leaf->key_len = key_len;
  memcpy(leaf->key, key, key_len);

  return leaf;
}

static void free_node(void *node)
{
  if (node != NULL) {
    free(nsd_is_leaf(node) ? nsd_leaf_raw(node) : node);
  }
}

static inline nsd_node_t **
find_child256(const nsd_node256_t *node256, uint8_t key)
{
  return (nsd_node_t **)&node256->children[key];
}

static inline nsd_node_t **
find_child48(const nsd_node48_t *node48, uint8_t key)
{
  uint8_t idx = node48->keys[key];
  return idx != 0 ? (nsd_node_t **)&node48->children[idx - 1] : NULL;
}

static inline nsd_node_t **
find_child38(const nsd_node38_t *node38, uint8_t key)
{
  uint8_t idx = node38_xlat(key);
  return idx != (uint8_t)-1 ? (nsd_node_t **)&node38->children[idx] : NULL;
}

static inline nsd_node_t **
find_child32(const nsd_node32_t *node32, uint8_t key)
{
#if HAVE_AVX2
  uint8_t idx = nsd_v32_findeq_u8(key, node32->keys, node32->base.width);
  return idx != 0 ? (nsd_node_t **)&node32->children[ idx - 1 ] : NULL;
#else
  abort();
#endif
}

static inline nsd_node_t **
find_child16(const nsd_node16_t *node16, uint8_t key)
{
  uint8_t idx = nsd_v16_findeq_u8(key, node16->keys, node16->base.width);
  return idx != 0 ? (nsd_node_t **)&node16->children[ idx - 1 ] : NULL;
}

static inline nsd_node_t **
find_child4(const nsd_node4_t *node4, uint8_t key)
{
  for (uint8_t idx = 0; idx < node4->base.width; idx++) {
    if (node4->keys[idx] == key) {
      return (nsd_node_t **)&node4->children[idx];
    }
  }

  return NULL;
}

static nsd_node_t **
find_child(const nsd_node_t *node, uint8_t key)
{
  assert(node != NULL);
  switch (node->type) {
    case nsd_node4:
      return find_child4((const nsd_node4_t *)node, key);
    case nsd_node16:
      return find_child16((const nsd_node16_t *)node, key);
    case nsd_node32:
      return find_child32((const nsd_node32_t *)node, key);
    case nsd_node38:
      return find_child38((const nsd_node38_t *)node, key);
    case nsd_node48:
      return find_child48((const nsd_node48_t *)node, key);
    case nsd_node256:
      return find_child256((const nsd_node256_t *)node, key);
    default:
      break;
  }

  abort();
}

static inline nsd_node_t **
add_child256(nsd_node_t **noderef, uint8_t key, nsd_node_t *node)
{
  nsd_node256_t *node256 = (nsd_node256_t *)*noderef;

  assert(node256 != NULL);
  assert(node256->base.type == nsd_node256);
  assert(node256->children[key] == NULL);

  node256->base.width++;
  node256->children[key] = node;

  return &node256->children[key];
}

static inline nsd_node_t **
add_child48(nsd_node_t **noderef, uint8_t key, nsd_node_t *node)
{
  nsd_node48_t *node48 = (nsd_node48_t *)*noderef;

  assert(node48 != NULL);
  assert(node48->base.type == nsd_node48);

  if (node48->base.width == 48) {
    uint8_t cnt, idx;
    nsd_node256_t *node256;

    if ((node256 = alloc_node(nsd_node256)) == NULL) {
      return NULL;
    }
    copy_header((nsd_node_t *)node256, (nsd_node_t *)node48);
    for (idx = 0, cnt = 0; idx < 256; idx++) {
      if (node48->keys[idx] != 0) {
        node256->children[idx] = node48->children[node48->keys[idx] - 1];
        if (++cnt == node48->base.width) {
          break;
        }
      }
    }

    assert(cnt == node48->base.width);
    *noderef = (nsd_node_t *)node256;
    free_node(node48);
    return add_child256(noderef, key, node);
  }

  assert(node48->base.width < 48);
  assert(node48->keys[key] == 0);
  node48->keys[key] = ++node48->base.width;
  node48->children[node48->base.width - 1] = node;
  return &node48->children[node48->base.width - 1];
}

static inline nsd_node_t **
add_child38(nsd_node_t **noderef, uint8_t key, nsd_node_t *child)
{
  uint8_t idx;
  nsd_node38_t *node38 = (nsd_node38_t *)*noderef;

  assert(node38 != NULL);
  assert(node38->base.type == nsd_node38);

  if ((idx = node38_xlat(key)) == (uint8_t)-1) {
    uint8_t cnt;
    nsd_node48_t *node48;

    if ((node48 = alloc_node(nsd_node48)) == NULL) {
      return NULL;
    }
    copy_header((nsd_node_t *)node48, (nsd_node_t *)node38);
    for (idx = 0, cnt = 0; idx < 38; idx++) {
      if (node38->children[idx] != NULL) {
        node48->children[cnt++] = node38->children[idx];
        node48->keys[ node38_unxlat(idx) ] = cnt;
        if (cnt == node38->base.width) {
          break;
        }
      }
    }

    assert(cnt == node38->base.width);
    *noderef = (nsd_node_t *)node48;
    free_node(node38);
    return add_child48(noderef, key, child);
  }

  assert(node38->base.width < 38);
  assert(node38->children[idx] == NULL);
  node38->children[idx] = child;
  node38->base.width++;
  return &node38->children[idx];
}

static nsd_node_t **
add_child32(nsd_node_t **noderef, uint8_t key, nsd_node_t *child)
{
#if HAVE_AVX2
  uint8_t idx;
  nsd_node32_t *node32 = (nsd_node32_t *)*noderef;

  assert(node32 != NULL);
  assert(node32->base.type == nsd_node32);

  if (node32->base.width == 32) {
    uint8_t cnt;
    nsd_node38_t *node38;
    nsd_node48_t *node48;
    int ishost = (node38_xlat(key) != (uint8_t)-1);

    for (idx = 0; ishost && idx < 32; idx++) {
      ishost = (node38_xlat(node32->keys[idx]) != (uint8_t)-1);
    }

    if (ishost) {
      if ((node38 = alloc_node(nsd_node38)) == NULL) {
        return NULL;
      }

      copy_header((nsd_node_t *)node38, (nsd_node_t *)node32);
      for (idx = 0; idx < 32; idx++) {
         node38->children[ node38_xlat(node32->keys[idx]) ]
           = node32->children[idx];
      }
      *noderef = (nsd_node_t *)node38;
      free_node(node32);
      return add_child38(noderef, key, child);
    } else {
      if ((node48 = alloc_node(nsd_node48)) == NULL) {
        return NULL;
      }

      copy_header((nsd_node_t *)node48, (nsd_node_t *)node32);
      for (idx = 0, cnt = 0; idx < 32; idx++) {
        node48->children[cnt++] = node32->children[idx];
        node48->keys[ node32->keys[idx] ] = cnt;
      }
      *noderef = (nsd_node_t *)node48;
      free_node(node32);
      return add_child48(noderef, key, child);
    }
  }

  assert(node32->base.width < 32);

  idx = nsd_v32_findgt_u8(key, node32->keys, node32->base.width);
  if (idx != 0) {
    assert(idx < node32->base.width);
    memmove(&node32->keys[idx + 1],
            &node32->keys[idx],
            sizeof(uint8_t) * (node32->base.width - idx));
    memmove(&node32->children[idx + 1],
            &node32->children[idx],
            sizeof(void*) * (node32->base.width - idx));
  } else {
    idx = node32->base.width;
  }

  node32->keys[idx] = key;
  node32->children[idx] = child;
  node32->base.width++;

  return &node32->children[idx];
#else
  abort();
#endif /* HAVE_AVX2 */
}

static nsd_node_t **
add_child16(nsd_node_t **noderef, uint8_t key, nsd_node_t *child)
{
  uint8_t idx;
  nsd_node16_t *node16 = (nsd_node16_t *)*noderef;

  assert(node16 != NULL);
  assert(node16->base.type == nsd_node16);

  if (node16->base.width == 16) {
#if HAVE_AVX2
    nsd_node32_t *node32;

    if ((node32 = alloc_node(nsd_node32)) == NULL) {
      return NULL;
    }

    copy_header((nsd_node_t *)node32, (nsd_node_t *)node16);
    memcpy(node32->keys, node16->keys, sizeof(uint8_t) * 16);
    memcpy(node32->children, node16->children, sizeof(void *) * 16);
    *noderef = (nsd_node_t *)node32;
    free_node(node16);
    return add_child32(noderef, key, child);
#else
    int ishost = (node38_xlat(key) != (uint8_t)-1);

    for (idx = 0; ishost && idx < node16->base.width; idx++) {
      ishost = (node38_xlat(node16->keys[idx]) != (uint8_t)-1);
    }

    if (ishost) {
      nsd_node38_t *node38;
      if ((node38 = alloc_node(nsd_node38)) == NULL) {
        return NULL;
      }

      copy_header((nsd_node_t *)node38, (nsd_node_t *)node16);
      for (idx = 0; idx < 16; idx++) {
        node38->children[ node38_xlat(node16->keys[idx]) ]
          = node16->children[idx];
      }
      *noderef = (nsd_node_t *)node38;
      free_node(node16);
      return add_child38(noderef, key, child);
    } else {
      nsd_node48_t *node48;
      if ((node48 = alloc_node(nsd_node48)) == NULL) {
        return NULL;
      }

      copy_header((nsd_node_t *)node48, (nsd_node_t *)node);
      for (idx = 0, cnt = 0; idx < 16; idx++) {
        node48->children[cnt++] = node16->children[idx];
        node48->keys[ node16->keys[idx] ] = cnt;
      }
      *noderef = (nsd_node_t *)node48;
      free_node(node16);
      return add_child48(noderef, key, child);
    }
#endif /* HAVE_AVX2 */
  }

  assert(node16->base.width < 16);

  idx = nsd_v16_findgt_u8(key, node16->keys, node16->base.width);
  if (idx) {
    assert(idx < node16->base.width);
    memmove(
      &node16->keys[idx + 1],
      &node16->keys[idx],
      sizeof(uint8_t) * (node16->base.width - idx));
    memmove(
      &node16->children[idx + 1],
      &node16->children[idx],
      sizeof(nsd_node_t *) * (node16->base.width - idx));
  } else {
    idx = node16->base.width;
  }

  node16->keys[idx] = key;
  node16->children[idx] = child;
  node16->base.width++;

  return &node16->children[idx];
}

static nsd_node_t **
add_child4(nsd_node_t **noderef, uint8_t key, nsd_node_t *child)
{
  uint8_t idx = 0;
  nsd_node4_t *node4 = (nsd_node4_t *)*noderef;
  nsd_node16_t *node16;

  if (node4->base.width == 4) {
    if ((node16 = alloc_node(nsd_node16)) == NULL) {
      return NULL;
    }

    copy_header((nsd_node_t *)node16, (nsd_node_t *)node4);
    memcpy(node16->keys, node4->keys, sizeof(uint8_t) * 4);
    memcpy(node16->children, node4->children, sizeof(void*) * 4);
    *noderef = (nsd_node_t *)node16;
    free_node(node4);
    return add_child16(noderef, key, child);
  }

  assert(node4->base.width < 4);

  for (idx = 0; idx < node4->base.width && key > node4->keys[idx]; idx++) { }

  if (idx < node4->base.width) {
    assert(key != node4->keys[idx]);
    memmove(
      &node4->keys[idx + 1],
      &node4->keys[idx],
      sizeof(uint8_t) * (node4->base.width - idx));
    memmove(
      &node4->children[idx + 1],
      &node4->children[idx],
      sizeof(void*) * (node4->base.width - idx));
  }

  node4->keys[idx] = key;
  node4->children[idx] = child;
  node4->base.width++;

  return &node4->children[idx];
}

static nsd_node_t **
add_child(nsd_node_t **noderef, uint8_t key, nsd_node_t *child)
{
  assert(noderef != NULL);
  switch ((*noderef)->type) {
    case nsd_node4:
      return add_child4(noderef, key, child);
    case nsd_node16:
      return add_child16(noderef, key, child);
    case nsd_node32:
      return add_child32(noderef, key, child);
    case nsd_node38:
      return add_child38(noderef, key, child);
    case nsd_node48:
      return add_child48(noderef, key, child);
    case nsd_node256:
      return add_child256(noderef, key, child);
    default:
      break;
  }

  abort();
}

nsd_retcode_t
nsd_find_path(
  nsd_tree_t *tree, nsd_path_t *path, const nsd_key_t key, uint8_t key_len)
{
  uint8_t depth = 0;
  nsd_node_t **childref, **noderef;

  assert(tree != NULL);
  assert(path != NULL);
  assert(key_len != 0);

  if (path->height == 0) {
    path->levels[0].depth = depth;
    path->levels[0].noderef = &tree->root;
    path->height++;
  } else {
    assert(path->levels[0].depth == 0);
    assert(path->levels[0].noderef == &tree->root);
    depth = path->levels[path->height - 1].depth;
  }

  assert(key_len >= path->levels[path->height - 1].depth);

  while (depth < key_len) {
    noderef = path->levels[path->height - 1].noderef;
    if (nsd_is_leaf(*noderef)) {
      uint8_t cnt;
      nsd_leaf_t *leaf = nsd_leaf_raw(*noderef);

      assert(key_len >= leaf->key_len);
      cnt = compare_keys(key, key_len, leaf->key, leaf->key_len);
      assert(cnt >= depth);
      if (cnt == key_len) {
        /* keys cannot be prefixes */
        assert(key_len == leaf->key_len);
        return nsd_ok;
      } else {
        /* discard node from path */
        path->height--;
        return nsd_not_found;
      }
    } else if ((*noderef)->prefix_len != 0) {
      uint8_t cnt;

      cnt = compare_keys(
        key + depth, key_len - depth, (*noderef)->prefix, (*noderef)->prefix_len);
      if (cnt == (*noderef)->prefix_len) {
        depth += cnt;
      } else {
        /* discard node from path */
        path->height--;
        return nsd_not_found;
      }
    }

    if ((childref = find_child(*noderef, key[depth])) == NULL) {
      return nsd_not_found;
    }

    path->levels[path->height].depth = depth;
    path->levels[path->height].noderef = childref;
    path->height++;
    depth++;
  }

  return nsd_ok;
}

nsd_retcode_t
nsd_make_path(
  nsd_tree_t *tree, nsd_path_t *path, const nsd_key_t key, uint8_t key_len)
{
  uint8_t depth = 0;
  nsd_node_t **childref, **noderef;

  assert(tree != NULL);
  assert(path != NULL);
  assert(key_len != 0);

  if (path->height == 0) {
    path->levels[0].depth = depth;
    path->levels[0].noderef = &tree->root;
    path->height++;
  } else {
    assert(path->levels[0].depth == 0);
    assert(path->levels[0].noderef == &tree->root);
    depth = path->levels[path->height - 1].depth;
  }

  assert(key_len >= path->levels[path->height - 1].depth);

  while (depth < key_len) {
    noderef = path->levels[path->height - 1].noderef;
    if (nsd_is_leaf(*noderef)) {
      uint8_t cnt;
      nsd_leaf_t *leaf = nsd_leaf_raw(*noderef);

      assert(key_len >= leaf->key_len);
      cnt = compare_keys(key, key_len, leaf->key, leaf->key_len);
      assert(cnt >= depth);

      if (cnt == key_len) {
        /* match */
        /* duplicates can exist but keys cannot be prefixes */
        assert(key_len == leaf->key_len);
        return nsd_ok;
      } else {
        /* mismatch, split node. */
        uint8_t len;
        nsd_node_t **childref, *dummy, *node;
        nsd_path_t relpath;

        assert(cnt < key_len);
        assert(cnt < leaf->key_len);

        relpath.height = 0;

        /* take depth of *this* node for offset */
        depth = path->levels[path->height - 1].depth;
        while (depth < cnt) {
          if ((node = alloc_node(nsd_node4)) == NULL) {
            /* discard newly allocated nodes */
            while (relpath.height > 0) {
              free_node(*relpath.levels[--relpath.height].noderef);
            }
            return nsd_no_memory;
          }
          /* determine prefix length, exclude first octet */
          len = cnt - depth;
          if (len > NSD_MAX_PREFIX) {
            len = NSD_MAX_PREFIX;
          } else {
            len--;
          }
          if (len > 0) {
            memcpy(node->prefix, &key[1 + depth], len);
            node->prefix_len = len;
          }
          if (relpath.height == 0) {
            dummy = node;
            childref = &dummy;
          } else {
            childref = add_child(
              relpath.levels[ relpath.height - 1 ].noderef, key[depth], node);
          }
          relpath.levels[relpath.height].depth = depth;
          relpath.levels[relpath.height].noderef = childref;
          relpath.height++;
          depth += 1 + len;
        }

        assert(depth == cnt);
        /* unlink leaf */
        *noderef = *relpath.levels[0].noderef;
        /* merge paths */
        if (--relpath.height > 0) { /* skip dummy */
          assert(path->height <= NSD_MAX_HEIGHT - relpath.height);
          memcpy(&path->levels[path->height],
                 &relpath.levels[1],
                  relpath.height * sizeof(relpath.levels[1]));
          path->height += relpath.height;
        }
        /* link leaf */
        (void)add_child(
          path->levels[path->height - 1].noderef, leaf->key[depth], SET_LEAF(leaf));
      }
    } else if ((*noderef)->prefix_len != 0) {
      uint8_t cnt;
      nsd_node_t *node;

      cnt = compare_keys(
        key + depth, key_len - depth, (*noderef)->prefix, (*noderef)->prefix_len);
      assert(path->levels[path->height - 1].depth == depth - 1);

      if (cnt != (*noderef)->prefix_len) {
        /* mismatch, split node */
        assert(cnt < key_len - depth);
        assert(cnt < (*noderef)->prefix_len);

        if ((node = alloc_node(nsd_node4)) == NULL) {
          return nsd_no_memory;
        }

        node->prefix_len = cnt;
        memcpy(node->prefix, (*noderef)->prefix, cnt);
        /* link node */
        add_child(&node, (*noderef)->prefix[cnt], *noderef);
        /* determine prefix length, exclude first octet */
        (*noderef)->prefix_len = (*noderef)->prefix_len - (1 + cnt);
        if ((*noderef)->prefix_len != 0) {
          memmove((*noderef)->prefix,
                  (*noderef)->prefix + (1 + cnt),
                  (*noderef)->prefix_len);
        } else {
          memset((*noderef)->prefix, 0, sizeof((*noderef)->prefix));
        }
        /* unlink node, link inner node */
        *noderef = node;
      }
      depth += cnt;
    }

    childref = find_child(*noderef, key[depth]);
    if (childref != NULL) {
      path->levels[path->height].depth = depth;
      path->levels[path->height].noderef = childref;
      path->height++;
      depth++;
    } else {
      nsd_leaf_t *leaf;

      if ((leaf = make_leaf(key, key_len)) == NULL) {
        return nsd_no_memory;
      }
      childref = add_child(noderef, key[depth], SET_LEAF(leaf));
      if (childref == NULL) {
        free_node(SET_LEAF(leaf));
        return nsd_no_memory;
      }

      path->levels[path->height].depth = depth;
      path->levels[path->height].noderef = childref;
      path->height++;
      depth = key_len;
    }
  }

  return nsd_ok;
}
