/*
 * main.c -- simple demonstration of adaptive radix tree
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tree.h"
#include "dname.h"

static void put_key(
  nsd_tree_t *tree,
  nsd_key_t key,
  uint8_t key_len,
  const char *name,
  const char *value)
{
  nsd_path_t path;
  path.height = 0;

  if (nsd_make_path(tree, &path, key, key_len) == nsd_ok) {
    nsd_leaf_t *leaf;
    const char fmt[] = "%s %s (height: %u, value: %s)\n";
    const char *stat;
    assert(path.height > 0);
    assert(nsd_is_leaf(*path.levels[path.height - 1].noderef));
    leaf = nsd_leaf_raw(*path.levels[path.height - 1].noderef);
    if (leaf->data != NULL) {
      stat = "existed";
    } else {
      stat = "created";
      leaf->data = (void*)value;
    }
    printf(fmt, name, stat, path.height, (const char *)leaf->data);
  } else {
    printf("%s not created\n", name);
  }
}

static void get_key(
  nsd_tree_t *tree,
  nsd_key_t key,
  uint8_t key_len,
  const char *name,
  const char *value)
{
  nsd_path_t path;
  path.height = 0;

  if (nsd_find_path(tree, &path, key, key_len) == nsd_ok) {
    nsd_leaf_t *leaf;
    const char fmt[] = "%s found (height: %u, value: %s)\n";
    assert(path.height > 0);
    assert(nsd_is_leaf(*path.levels[path.height - 1].noderef));
    leaf = nsd_leaf_raw(*path.levels[path.height - 1].noderef);
    printf(fmt, name, path.height, (const char *)leaf->data);
  } else {
    printf("%s not found\n", name);
  }
}

typedef void(*func_t)(nsd_tree_t *, nsd_key_t, uint8_t, const char *, const char *);

int main(int argc, char *argv[])
{
  nsd_tree_t tree;
  nsd_key_t key;
  uint8_t key_len;
  uint8_t name[255];
  int name_len;
  char *sep;
  const char *val;
  func_t ops[2] = { &put_key, &get_key };

  if (argc < 2) {
    fprintf(stderr, "Usage: %s domain-name=value..\n", argv[0]);
    exit(1);
  }

  tree.root = calloc(1, sizeof(nsd_node4_t));
  tree.root->type = nsd_node4;

  for (int opno = 0; opno < 2; opno++) {
    for (int argno = 1; argno < argc; argno++) {
      if ((sep = strchr(argv[argno], '=')) != NULL) {
        *sep = '\0';
        val = sep + 1;
      } else {
        val = "foobar";
      }
      name_len = dname_parse_wire(name, argv[argno]);
      if (name_len == 0) {
        printf("skipped %s\n", argv[argno]);
        goto skip;
      }
      key_len = nsd_make_key(key, name);
      if (key_len == 0) {
        printf("skipped %s\n", argv[argno]);
        goto skip;
      }
      ops[opno](&tree, key, key_len, argv[argno], val);
skip:
      if (sep != NULL) {
        *sep = '=';
      }
    }
  }

  return 0;
}
