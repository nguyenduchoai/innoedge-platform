# Phát hành lên ESP Component Registry

Đây là cách dev ESP32 thật sự tìm và cài SDK: `idf.py add-dependency`, không phải
`git clone`.

## Chuẩn bị một lần

1. Đăng nhập https://components.espressif.com bằng tài khoản GitHub.
2. Namespace đang dùng: `nguyenduchoai`.
3. Tạo API token (Settings → Tokens), rồi:

```bash
export IDF_COMPONENT_API_TOKEN=<token>
```

4. Cài công cụ:

```bash
pip install -U idf-component-manager
```

## Mỗi lần phát hành

```bash
# 1. Tăng version trong components/innoedge/idf_component.yml
#    (semver — phá vỡ tương thích thì tăng major)

# 2. Test phải xanh
./tests/run.sh

# 3. Đóng gói thử, KHÔNG upload — xem đúng những file nào sẽ đi
compote component pack --name innoedge --project-dir components/innoedge

# 4. Upload — cách khuyên dùng: GitHub Actions "Publish to ESP Component Registry"
#    (workflow_dispatch hoặc tạo Release). Token nằm ở secret IDF_COMPONENT_API_TOKEN,
#    workflow phát hành cả innoedge lẫn innoedge_hw và yank được version hỏng.
#    Tay, nếu có token:
compote component upload --namespace nguyenduchoai --name innoedge \
    --project-dir components/innoedge
compote component upload --namespace nguyenduchoai --name innoedge_hw \
    --project-dir components-hw/innoedge_hw
```

## Luật

**Không bao giờ ghi đè một version đã phát hành.** Registry cho phép xoá, nhưng
project của người khác đã ghim version đó. Sai thì phát hành version vá.

**Version trong manifest là nguồn duy nhất.** Đừng để nó lệch với git tag —
gắn tag `v<version>` ngay sau khi upload.

**`compote component pack` trước khi upload.** Nó cho thấy đúng danh sách file
sẽ đi ra ngoài. Đây là chốt chặn cuối trước khi lỡ phát hành thứ không định phát.

## Người dùng cài như thế nào

```bash
idf.py add-dependency "nguyenduchoai/innoedge^0.2.0"
```

Component manager tự kéo cả `espressif/esp_websocket_client`. Người dùng chỉ cần
`REQUIRES innoedge` trong `main/CMakeLists.txt`.

## Vì sao chỉ một component

Tên component là **toàn cục** trong một project ESP-IDF. Phát hành ra registry
công khai một component tên `net` hay `config_store` là đặt mìn xung đột cho mọi
người dùng. Nên 6 component hạ tầng cũ được gộp thành một `innoedge` duy nhất:
một tên, một phiên bản, một lệnh cài.
