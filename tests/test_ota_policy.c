// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// Luật OTA: digest hex, semver nghiêm ngặt, chống hạ cấp. Sai ở đây = máy cài
// bản cũ có lỗ hổng, hoặc tải lại mãi một bản (OTA loop).
#include "ie_ota_policy.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint8_t digest[32];
    assert(ie_ota_digest("0123456789abcdef" "0123456789ABCDEF"
                           "0123456789abcdef" "0123456789ABCDEF", digest));
    assert(digest[0] == 0x01 && digest[31] == 0xef);
    assert(!ie_ota_digest(NULL, digest));
    assert(!ie_ota_digest("bad", digest));
    assert(!ie_ota_digest("z123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef01", digest));
    assert(ie_ota_version_allowed("1.4.0", "1.3.5", false));
    assert(!ie_ota_version_allowed("1.3.4", "1.3.5", false));
    assert(ie_ota_version_allowed("1.3.4", "1.3.5", true));
    assert(!ie_ota_version_allowed("1.3.5", "1.3.5", true));
    assert(!ie_ota_version_allowed("1.3.5bad", "1.3.4", true));
    assert(!ie_ota_version_allowed("1.3", "1.3.4", true));
    assert(!ie_ota_version_allowed("999999999999.1.1", "1.3.4", true));
    puts("PASS — luật OTA (digest, semver, chống hạ cấp)");
    return 0;
}
