/*
 * test_ui.c — Tests for the graph visualization UI module.
 *
 * Covers: config persistence, embedded asset lookup, layout engine.
 */
#include "../src/foundation/compat.h"
#include "../src/foundation/compat_fs.h"
#include "test_framework.h"
#include "test_helpers.h"
#include "ui/config.h"
#include "ui/embedded_assets.h"
#include "ui/layout3d.h"
#include "store/store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* ── Config tests ─────────────────────────────────────────────── */

TEST(config_load_defaults) {
    /* Loading with no config file should give defaults */
    cbm_ui_config_t cfg;
    cfg.ui_enabled = true; /* set non-default to verify load overwrites */
    cfg.ui_port = 1234;

    /* Use a temp HOME to avoid touching real config */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_test_config_XXXXXX");
    char *td = cbm_mkdtemp(tmpdir);
    ASSERT_NOT_NULL(td);

    char *old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    cbm_setenv("HOME", td, 1);

    cbm_ui_config_load(&cfg);

    ASSERT_FALSE(cfg.ui_enabled);
    ASSERT_EQ(cfg.ui_port, 9749);

    /* Restore HOME */
    if (old_home) {
        cbm_setenv("HOME", old_home, 1);
        free(old_home);
    }

    PASS();
}

TEST(config_save_and_reload) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_test_config_XXXXXX");
    char *td = cbm_mkdtemp(tmpdir);
    ASSERT_NOT_NULL(td);

    char *old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    cbm_setenv("HOME", td, 1);

    /* Save */
    cbm_ui_config_t cfg = {.ui_enabled = true, .ui_port = 8080};
    cbm_ui_config_save(&cfg);

    /* Reload */
    cbm_ui_config_t loaded;
    cbm_ui_config_load(&loaded);

    ASSERT_TRUE(loaded.ui_enabled);
    ASSERT_EQ(loaded.ui_port, 8080);

    if (old_home) {
        cbm_setenv("HOME", old_home, 1);
        free(old_home);
    }

    PASS();
}

TEST(config_overwrite) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_test_config_XXXXXX");
    char *td = cbm_mkdtemp(tmpdir);
    ASSERT_NOT_NULL(td);

    char *old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    cbm_setenv("HOME", td, 1);

    /* Save with ui_enabled=true */
    cbm_ui_config_t cfg1 = {.ui_enabled = true, .ui_port = 9749};
    cbm_ui_config_save(&cfg1);

    /* Overwrite with ui_enabled=false */
    cbm_ui_config_t cfg2 = {.ui_enabled = false, .ui_port = 9749};
    cbm_ui_config_save(&cfg2);

    /* Reload should show false */
    cbm_ui_config_t loaded;
    cbm_ui_config_load(&loaded);
    ASSERT_FALSE(loaded.ui_enabled);

    if (old_home) {
        cbm_setenv("HOME", old_home, 1);
        free(old_home);
    }

    PASS();
}

TEST(config_corrupt_file) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_test_config_XXXXXX");
    char *td = cbm_mkdtemp(tmpdir);
    ASSERT_NOT_NULL(td);

    char *old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    cbm_setenv("HOME", td, 1);

    /* Write garbage to config path */
    char path[1024];
    cbm_ui_config_path(path, (int)sizeof(path));

    /* Ensure directory exists (portable — no system("mkdir -p")) */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/.cache/codebase-memory-mcp", td);
    cbm_mkdir_p(dir, 0755);

    FILE *f = fopen(path, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "this is not json!!!");
    fclose(f);

    /* Should load defaults, not crash */
    cbm_ui_config_t cfg;
    cbm_ui_config_load(&cfg);
    ASSERT_FALSE(cfg.ui_enabled);
    ASSERT_EQ(cfg.ui_port, 9749);

    if (old_home) {
        cbm_setenv("HOME", old_home, 1);
        free(old_home);
    }

    PASS();
}

TEST(config_missing_fields) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_test_config_XXXXXX");
    char *td = cbm_mkdtemp(tmpdir);
    ASSERT_NOT_NULL(td);

    char *old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    cbm_setenv("HOME", td, 1);

    /* Write JSON with only ui_port */
    char path[1024];
    cbm_ui_config_path(path, (int)sizeof(path));

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/.cache/codebase-memory-mcp", td);
    cbm_mkdir_p(dir, 0755);

    FILE *f = fopen(path, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "{\"ui_port\": 5555}");
    fclose(f);

    cbm_ui_config_t cfg;
    cbm_ui_config_load(&cfg);
    ASSERT_FALSE(cfg.ui_enabled); /* defaults for missing field */
    ASSERT_EQ(cfg.ui_port, 5555); /* present field loaded */

    if (old_home) {
        cbm_setenv("HOME", old_home, 1);
        free(old_home);
    }

    PASS();
}

/* ── Embedded asset tests ─────────────────────────────────────── */

TEST(embedded_lookup_not_found) {
    /* With stub, everything should return NULL */
    const cbm_embedded_file_t *f = cbm_embedded_lookup("/nonexistent");
    ASSERT_NULL(f);
    PASS();
}

TEST(embedded_stub_count) {
    /* Stub should have 0 files */
    ASSERT_EQ(CBM_EMBEDDED_FILE_COUNT, 0);
    PASS();
}

/* ── Layout tests ─────────────────────────────────────────────── */

TEST(layout_empty_graph) {
    cbm_store_t *store = cbm_store_open_memory();
    ASSERT_NOT_NULL(store);

    /* No nodes in store → empty result */
    cbm_layout_result_t *r =
        cbm_layout_compute(store, "test-project", CBM_LAYOUT_OVERVIEW, NULL, 0, 100, NULL);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->node_count, 0);
    ASSERT_EQ(r->edge_count, 0);

    cbm_layout_free(r);
    cbm_store_close(store);
    PASS();
}

TEST(layout_single_node) {
    cbm_store_t *store = cbm_store_open_memory();
    ASSERT_NOT_NULL(store);

    cbm_store_upsert_project(store, "test", "/tmp/test");
    cbm_node_t node = {
        .project = "test",
        .label = "Function",
        .name = "main",
        .qualified_name = "test::main",
        .file_path = "main.c",
        .start_line = 1,
        .end_line = 10,
    };
    int64_t id = cbm_store_upsert_node(store, &node);
    ASSERT_GT(id, 0);

    cbm_layout_result_t *r =
        cbm_layout_compute(store, "test", CBM_LAYOUT_OVERVIEW, NULL, 0, 100, NULL);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->node_count, 1);
    ASSERT_STR_EQ(r->nodes[0].name, "main");
    ASSERT_EQ(r->total_nodes, 1);

    cbm_layout_free(r);
    cbm_store_close(store);
    PASS();
}

TEST(layout_two_connected) {
    cbm_store_t *store = cbm_store_open_memory();
    ASSERT_NOT_NULL(store);

    cbm_store_upsert_project(store, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "foo",
                     .qualified_name = "test::foo",
                     .file_path = "a.c",
                     .start_line = 1,
                     .end_line = 5};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "bar",
                     .qualified_name = "test::bar",
                     .file_path = "b.c",
                     .start_line = 1,
                     .end_line = 5};
    int64_t id1 = cbm_store_upsert_node(store, &n1);
    int64_t id2 = cbm_store_upsert_node(store, &n2);

    cbm_edge_t edge = {.project = "test", .source_id = id1, .target_id = id2, .type = "CALLS"};
    cbm_store_insert_edge(store, &edge);

    cbm_layout_result_t *r =
        cbm_layout_compute(store, "test", CBM_LAYOUT_OVERVIEW, NULL, 0, 100, NULL);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->node_count, 2);

    /* Nodes should be positioned apart (not at same point) */
    float dx = r->nodes[0].x - r->nodes[1].x;
    float dy = r->nodes[0].y - r->nodes[1].y;
    float dz = r->nodes[0].z - r->nodes[1].z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    ASSERT_GT((long long)(dist * 100), 0);

    ASSERT_EQ(r->edge_count, 1);

    cbm_layout_free(r);
    cbm_store_close(store);
    PASS();
}

TEST(layout_respects_max_nodes) {
    cbm_store_t *store = cbm_store_open_memory();
    ASSERT_NOT_NULL(store);

    cbm_store_upsert_project(store, "test", "/tmp/test");

    /* Insert 20 nodes */
    for (int i = 0; i < 20; i++) {
        char name[32], qn[64];
        snprintf(name, sizeof(name), "fn%d", i);
        snprintf(qn, sizeof(qn), "test::fn%d", i);
        cbm_node_t n = {.project = "test",
                        .label = "Function",
                        .name = name,
                        .qualified_name = qn,
                        .file_path = "a.c",
                        .start_line = i,
                        .end_line = i + 1};
        cbm_store_upsert_node(store, &n);
    }

    /* max_nodes=5 should return at most 5 */
    cbm_layout_result_t *r =
        cbm_layout_compute(store, "test", CBM_LAYOUT_OVERVIEW, NULL, 0, 5, NULL);
    ASSERT_NOT_NULL(r);
    ASSERT_LTE(r->node_count, 5);
    ASSERT_EQ(r->total_nodes, 20);

    cbm_layout_free(r);
    cbm_store_close(store);
    PASS();
}

TEST(layout_deterministic) {
    cbm_store_t *store = cbm_store_open_memory();
    ASSERT_NOT_NULL(store);

    cbm_store_upsert_project(store, "test", "/tmp/test");

    cbm_node_t n1 = {.project = "test",
                     .label = "Function",
                     .name = "alpha",
                     .qualified_name = "test::alpha",
                     .file_path = "a.c",
                     .start_line = 1,
                     .end_line = 5};
    cbm_node_t n2 = {.project = "test",
                     .label = "Function",
                     .name = "beta",
                     .qualified_name = "test::beta",
                     .file_path = "b.c",
                     .start_line = 1,
                     .end_line = 5};
    cbm_store_upsert_node(store, &n1);
    cbm_store_upsert_node(store, &n2);

    /* Run twice, check positions match */
    cbm_layout_result_t *r1 =
        cbm_layout_compute(store, "test", CBM_LAYOUT_OVERVIEW, NULL, 0, 100, NULL);
    cbm_layout_result_t *r2 =
        cbm_layout_compute(store, "test", CBM_LAYOUT_OVERVIEW, NULL, 0, 100, NULL);
    ASSERT_NOT_NULL(r1);
    ASSERT_NOT_NULL(r2);
    ASSERT_EQ(r1->node_count, r2->node_count);

    for (int i = 0; i < r1->node_count; i++) {
        ASSERT_FLOAT_EQ(r1->nodes[i].x, r2->nodes[i].x, 0.001);
        ASSERT_FLOAT_EQ(r1->nodes[i].y, r2->nodes[i].y, 0.001);
        ASSERT_FLOAT_EQ(r1->nodes[i].z, r2->nodes[i].z, 0.001);
    }

    cbm_layout_free(r1);
    cbm_layout_free(r2);
    cbm_store_close(store);
    PASS();
}

TEST(layout_to_json) {
    cbm_store_t *store = cbm_store_open_memory();
    ASSERT_NOT_NULL(store);

    cbm_store_upsert_project(store, "test", "/tmp/test");

    cbm_node_t n = {.project = "test",
                    .label = "Function",
                    .name = "hello",
                    .qualified_name = "test::hello",
                    .file_path = "a.c",
                    .start_line = 1,
                    .end_line = 5};
    cbm_store_upsert_node(store, &n);

    cbm_layout_result_t *r =
        cbm_layout_compute(store, "test", CBM_LAYOUT_OVERVIEW, NULL, 0, 100, NULL);
    ASSERT_NOT_NULL(r);

    char *json = cbm_layout_to_json(r);
    ASSERT_NOT_NULL(json);

    /* Should contain key fields */
    ASSERT(strstr(json, "\"nodes\"") != NULL);
    ASSERT(strstr(json, "\"edges\"") != NULL);
    ASSERT(strstr(json, "\"total_nodes\"") != NULL);
    ASSERT(strstr(json, "\"hello\"") != NULL);
    ASSERT(strstr(json, "\"Function\"") != NULL);

    free(json);
    cbm_layout_free(r);
    cbm_store_close(store);
    PASS();
}

TEST(layout_null_inputs) {
    /* NULL store → NULL result */
    cbm_layout_result_t *r =
        cbm_layout_compute(NULL, "test", CBM_LAYOUT_OVERVIEW, NULL, 0, 100, NULL);
    ASSERT_NULL(r);

    /* NULL project → NULL result */
    cbm_store_t *store = cbm_store_open_memory();
    r = cbm_layout_compute(store, NULL, CBM_LAYOUT_OVERVIEW, NULL, 0, 100, NULL);
    ASSERT_NULL(r);

    /* cbm_layout_free(NULL) should not crash */
    cbm_layout_free(NULL);

    /* cbm_layout_to_json(NULL) should return NULL */
    char *json = cbm_layout_to_json(NULL);
    ASSERT_NULL(json);

    cbm_store_close(store);
    PASS();
}

/* ── Hierarchy expansion (drill-down explorer) ────────────────── */

TEST(expand_tree_groups_and_aggregates) {
    cbm_store_t *store = cbm_store_open_memory();
    ASSERT_NOT_NULL(store);
    cbm_store_upsert_project(store, "test", "/tmp/test");

    /* Hierarchy under "test": two folders with children + a leaf module.
     * QNs use '.' separators, matching the real indexer's scheme. */
    cbm_node_t nodes[] = {
        {.project = "test", .label = "Folder", .name = "alpha", .qualified_name = "test.alpha"},
        {.project = "test", .label = "Method", .name = "x", .qualified_name = "test.alpha.x"},
        {.project = "test", .label = "Method", .name = "y", .qualified_name = "test.alpha.y"},
        {.project = "test", .label = "Folder", .name = "beta", .qualified_name = "test.beta"},
        {.project = "test", .label = "Method", .name = "z", .qualified_name = "test.beta.z"},
        {.project = "test", .label = "Module", .name = "gamma", .qualified_name = "test.gamma"},
    };
    int64_t ids[6];
    for (int i = 0; i < 6; i++) {
        ids[i] = cbm_store_upsert_node(store, &nodes[i]);
    }
    /* Cross-folder call alpha.x -> beta.z should aggregate to a super-edge. */
    cbm_edge_t e = {.project = "test", .source_id = ids[1], .target_id = ids[4], .type = "CALLS"};
    cbm_store_insert_edge(store, &e);

    cbm_tree_view_t v;
    ASSERT_EQ(cbm_store_expand_tree(store, "test", "test", 500, 2000, &v), CBM_STORE_OK);

    /* Three top-level groups: alpha (3 nodes), beta (2), gamma (1, leaf). */
    ASSERT_EQ(v.child_count, 3);
    int ai = -1, bi = -1, gi = -1;
    for (int i = 0; i < v.child_count; i++) {
        if (strcmp(v.children[i].name, "alpha") == 0)
            ai = i;
        else if (strcmp(v.children[i].name, "beta") == 0)
            bi = i;
        else if (strcmp(v.children[i].name, "gamma") == 0)
            gi = i;
    }
    ASSERT_GTE(ai, 0);
    ASSERT_GTE(bi, 0);
    ASSERT_GTE(gi, 0);
    ASSERT_EQ(v.children[ai].count, 3);
    ASSERT_TRUE(v.children[ai].expandable);
    ASSERT_EQ(v.children[bi].count, 2);
    ASSERT_EQ(v.children[gi].count, 1);
    ASSERT_FALSE(v.children[gi].expandable); /* single leaf, nothing to drill into */

    /* Exactly one aggregated super-edge: alpha -> beta, weight 1. */
    ASSERT_EQ(v.edge_count, 1);
    ASSERT_EQ(v.edges[0].src, ai);
    ASSERT_EQ(v.edges[0].dst, bi);
    ASSERT_EQ(v.edges[0].weight, 1);

    cbm_tree_view_free(&v);
    cbm_store_close(store);
    PASS();
}

TEST(expand_tree_null_inputs) {
    cbm_tree_view_t v;
    ASSERT_EQ(cbm_store_expand_tree(NULL, "test", "test", 0, 0, &v), CBM_STORE_ERR);
    cbm_tree_view_free(NULL); /* must not crash */
    PASS();
}

/* ── Suite ────────────────────────────────────────────────────── */

SUITE(ui) {
    /* Config */
    RUN_TEST(config_load_defaults);
    RUN_TEST(config_save_and_reload);
    RUN_TEST(config_overwrite);
    RUN_TEST(config_corrupt_file);
    RUN_TEST(config_missing_fields);

    /* Embedded assets (stub) */
    RUN_TEST(embedded_lookup_not_found);
    RUN_TEST(embedded_stub_count);

    /* Layout engine */
    RUN_TEST(layout_empty_graph);
    RUN_TEST(layout_single_node);
    RUN_TEST(layout_two_connected);
    RUN_TEST(layout_respects_max_nodes);
    RUN_TEST(layout_deterministic);
    RUN_TEST(layout_to_json);
    RUN_TEST(layout_null_inputs);

    /* Hierarchy expansion */
    RUN_TEST(expand_tree_groups_and_aggregates);
    RUN_TEST(expand_tree_null_inputs);
}
