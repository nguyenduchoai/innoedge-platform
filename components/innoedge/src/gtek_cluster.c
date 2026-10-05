// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_cluster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static innoedge_cluster_role_t s_role = INNOEDGE_CLUSTER_STANDALONE;

esp_err_t gtek_cluster_init(innoedge_cluster_role_t role)
{
    s_role = role;
    return ESP_OK;
}

innoedge_cluster_role_t gtek_cluster_get_role(void)
{
    return s_role;
}

esp_err_t gtek_cluster_pack_payment(const char *subnode_id, int kind, int count,
                                    int64_t amount_vnd, char *out, size_t out_len)
{
    if (!subnode_id || !out || out_len < 64) return ESP_ERR_INVALID_ARG;

    int written = snprintf(out, out_len,
                           "{\"type\":\"cluster_pay\",\"subnode\":\"%s\",\"kind\":%d,"
                           "\"count\":%d,\"amount\":%lld}",
                           subnode_id, kind, count, (long long)amount_vnd);
    if (written < 0 || (size_t)written >= out_len) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static const char *find_json_value(const char *json, const char *key, char *out, size_t max_len)
{
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return NULL;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;

    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < max_len) {
            out[i++] = *p++;
        }
        out[i] = '\0';
        return out;
    } else {
        size_t i = 0;
        while (*p && *p != ',' && *p != '}' && *p != ' ' && i + 1 < max_len) {
            out[i++] = *p++;
        }
        out[i] = '\0';
        return out;
    }
}

esp_err_t gtek_cluster_unpack_command(const char *json_str, char *target_subnode,
                                      size_t subnode_len, long long *cmd_id,
                                      char *action, size_t act_len,
                                      char *params_json, size_t params_len)
{
    if (!json_str || !target_subnode || !cmd_id || !action) return ESP_ERR_INVALID_ARG;

    char buf[128];
    if (!find_json_value(json_str, "subnode", target_subnode, subnode_len)) {
        return ESP_ERR_NOT_FOUND;
    }

    if (find_json_value(json_str, "commandId", buf, sizeof(buf))) {
        *cmd_id = (long long)atoll(buf);
    } else {
        *cmd_id = 0;
    }

    if (!find_json_value(json_str, "action", action, act_len)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (params_json && params_len > 0) {
        const char *p = strstr(json_str, "\"params\":");
        if (p) {
            p += 9;
            while (*p == ' ' || *p == '\t') p++;
            snprintf(params_json, params_len, "%s", p);
            // Cắt bớt phần sau nếu là đối tượng kết thúc
            char *end = strrchr(params_json, '}');
            if (end && end != params_json) {
                *(end + 1) = '\0';
            }
        } else {
            params_json[0] = '\0';
        }
    }

    return ESP_OK;
}

esp_err_t gtek_cluster_pack_command_ack(const char *subnode_id, long long cmd_id,
                                        const char *status, const char *msg,
                                        char *out, size_t out_len)
{
    if (!subnode_id || !status || !out || out_len < 64) return ESP_ERR_INVALID_ARG;

    int written = snprintf(out, out_len,
                           "{\"type\":\"cluster_ack\",\"subnode\":\"%s\",\"commandId\":%lld,"
                           "\"status\":\"%s\",\"message\":\"%s\"}",
                           subnode_id, cmd_id, status, msg ? msg : "");
    if (written < 0 || (size_t)written >= out_len) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
