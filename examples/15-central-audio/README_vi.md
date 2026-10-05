# 15 — Centralized Multi-Zone IP Audio: Quản Lý Âm Thanh & Loa Thông Báo Tập Trung

[English](README.md) | [Tiếng Việt](README_vi.md)


Ứng dụng quản lý hệ thống âm thanh thông báo công cộng (Public Address - PA) và phát nhạc nền (BGM) đa vùng kết nối Cloud InnoEdge cho chuỗi cửa hàng, siêu thị, nhà xưởng, trường học, tòa nhà.

---

## 💡 Điểm Độc Đáo Của InnoEdge IP Audio

1. **Phân Vùng Đa Điểm (Multi-Zone Management):**
   * Quản lý phân quyền theo Zone: Zone 1 (Tầng 1 / Quầy thu ngân), Zone 2 (Khu vực trưng bày), Zone ALL (Toàn bộ tòa nhà).
   * Phát thông báo riêng lẻ cho từng vùng mà không làm gián đoạn các vùng khác.
2. **Ngắt Ưu Tiên Thông Báo (Priority Paging Override):**
   * Khi nhận lệnh `audio_announce`, hệ thống tự động:
     - Giảm hoặc ngắt tiếng nhạc nền BGM.
     - Phát chuông báo "Ding-Dong".
     - Tăng âm lượng lên mức thông báo rõ ràng (85%).
     - Phát nội dung thông báo thoại.
     - Tự động hạ âm lượng và tiếp tục phát nhạc nền du dương khi hết giờ!
3. **Còi Báo Cháy Khẩn Cấp (Emergency Siren):**
   * Lệnh `audio_emergency` lập tức ghi đè toàn bộ hệ thống, đẩy âm lượng lên 100% để phát âm thanh sơ tán hỏa hoạn.
4. **Dịch Vụ Âm Nhạc Theo Yêu Cầu (Jukebox On-Demand):**
   * Khách tại quán cafe / nhà hàng quét mã VietQR (10.000 đ) để order bài hát yêu thích phát lên loa quán!

---

## 🛠️ Phần Cứng Đề Xuất

* **Vi điều khiển:** ESP32-S3 kết nối mạch khuếch đại âm thanh I2S (MAX98357A hoặc codec ES8311).
* **Hoặc Linux SBC:** Raspberry Pi hoặc Banana Pi cắm trực tiếp ra amply/loa qua cổng 3.5mm AUX, HDMI hoặc card âm thanh USB.
* **Loa:** Loa thông báo gắn trần hoặc loa hộp trở kháng cao/thấp.

---

## 🚀 Hướng Dẫn Chạy Thử Nghiệm

### Bước 1: Khởi động Mock Cloud
```bash
cd tools/mock-cloud
go run .
```

### Bước 2: Nạp Firmware & Giám Sát Log
```bash
cd examples/15-central-audio
idf.py set-target esp32s3
idf.py menuconfig   # Cấu hình WiFi và IP mock-cloud
idf.py build flash monitor
```

---

## 🎮 Thao Tác Kiểm Chứng

1. Hệ thống tự động phát nhạc nền BGM ở mức âm lượng 45%:
   ```
   I audio-pa: 🔊 [ZONE 1] Chế độ: BGM (Nhạc nền) | Âm lượng: 45%
   I audio-pa:    🎵 Đang phát nhạc nền: "Acoustic Cafe Chill Playlist"
   ```
2. Thử nghiệm phát thông báo ưu tiên từ Cloud:
   Gửi lệnh từ Dashboard hoặc Mock Cloud:
   ```json
   {
     "action": "audio_announce",
     "params": {
       "message": "Kính mời quý khách tới quầy thu ngân số 2 thanh toán",
       "seconds": 5,
       "zone": 1
     }
   }
   ```
   Hệ thống lập tức ngắt nhạc nền, chuyển sang chế độ Paging và tăng âm lượng lên 85%:
   ```
   W audio-pa: 🔔 [DING-DONG] NGẮT NHẠC NỀN -> PHÁT THÔNG BÁO ƯU TIÊN!
   I audio-pa: 🔊 [ZONE 1] Chế độ: PAGING (Thông báo thoại) | Âm lượng: 85%
   I audio-pa:    🗣️ Đang phát thông báo: "Kính mời quý khách tới quầy thu ngân số 2..."
   ... (5 giây sau)
   I audio-pa: Thông báo kết thúc -> Tự động khôi phục phát nhạc nền BGM.
   I audio-pa: 🔊 [ZONE 1] Chế độ: BGM (Nhạc nền) | Âm lượng: 45%
   ```
3. Thử nghiệm tính năng Jukebox nạp tiền chọn bài hát (Bấm nút **BOOT**):
   ```
   I audio-pa: Khách ấn nút chọn bài hát Jukebox (10.000 đ) -> Tạo VietQR...
   I audio-pa: Mã VietQR Chọn Bài Hát Theo Yêu Cầu (Jukebox): 10000 đ | Ref: GTMOCKD...
   ```
   Quét thanh toán giả lập -> Loa lập tức chuyển sang bài hát của khách yêu cầu:
   ```
   I audio-pa: ✓ XÁC NHẬN TIỀN VỀ: 10000 đ -> XẾP BÀI HÁT VÀO HÀNG ĐỢI PHÁT!
   I audio-pa:    🎵 Đang phát nhạc nền: "Yêu Cầu #...: Bài Ca Tình Yêu"
   ```
