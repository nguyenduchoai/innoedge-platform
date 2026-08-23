#pragma once
// Stub tối thiểu: test registry/dedupe không cần parser thật.
typedef struct cJSON cJSON;
static inline cJSON *cJSON_Parse(const char *s) { (void)s; return 0; }
static inline void cJSON_Delete(cJSON *j) { (void)j; }
