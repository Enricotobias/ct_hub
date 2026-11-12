# CT-Hub Firmware

CT-Hub adalah firmware pemantau arus 16 kanal untuk ESP32 Dev Module. Firmware menjalankan pemindaian sensor CT, menyimpan snapshot dan kalibrasi, menerbitkan data melalui MQTT biasa, menerima command umum dan OTA HTTPS, serta memantau kesehatan perangkat melalui LED RGB.

## Status integrasi saat ini

`setup()` di `ct_hub.ino` menginisialisasi runtime, store, kalibrasi NVS, adapter board dan OTA, identitas perangkat, lalu membuat task `TaskHealth`, `TaskSensor`, `TaskNetwork`, dan `TaskOTA`. Sensor mulai bekerja tanpa menunggu Wi-Fi atau broker. `loop()` hanya menunggu; operasi berjalan pada task FreeRTOS dengan ownership terpisah.

Nilai Wi-Fi dan broker pada `config.cpp` masih kosong dan perlu diisi sebelum konektivitas dapat digunakan. OTA memerlukan partition table yang mendukung OTA. Tag source firmware saat ini adalah `CT_HUB_VER:1.0.0`.

## Debug Serial

Serial berjalan pada `115200` baud. Baris debug memakai format `[millis][LEVEL][COMPONENT] pesan`; komponen utama adalah `BOOT`, `SENSOR`, `CT`, `WIFI`, `NETWORK`, `MQTT`, `HEALTH`, dan `OTA`. Log menunjukkan tahap inisialisasi, status koneksi/perubahan health, hasil subscribe dan publish (topic, ukuran, retain), ringkasan 16 kanal CT setiap 10 detik, serta perpindahan fase OTA. Ringkasan CT bukan log setiap sampel ADC.

Untuk menonaktifkan log terstruktur saat dibutuhkan, set `CT_DEBUG_SERIAL_ENABLED` ke `0` sebelum `debug_log.h` digunakan. Pesan debug tidak memuat kredensial, sertifikat, isi payload MQTT, atau URL OTA. Saat mendiagnosis, cari log `ERROR`/`WARN` pertama setelah `BOOT`; contoh: `ADS1115 initialization failed`, `SSID is empty`, `broker connection failed` beserta state PubSubClient, atau `publish failed` dengan topic yang dituju.

## Alur kerja firmware

Alur yang disediakan modul firmware:

1. Bootstrap menyiapkan runtime, store, kalibrasi NVS, adapter board/OTA, lalu membentuk device ID dari MAC eFuse.
2. `TaskSensor` memilih kanal MUX, memberi waktu settling, membaca dua pasangan diferensial ADS1115, menghitung arus, lalu menyimpan snapshot terbaru untuk kanal CT01 sampai CT16.
3. `TaskNetwork` menjaga koneksi melalui adapter Wi-Fi/MQTT, memproses command yang masuk ke runtime queue, menerbitkan status/info/data sesuai jadwal, dan memublikasikan ACK serta event OTA.
4. `TaskHealth` mengevaluasi kondisi task/sensor/network dan mengendalikan output RGB melalui adapter.
5. `TaskOTA` mengambil permintaan OTA dari runtime queue. OTA meminta jeda sensor pada batas scan yang aman, meminta network maintenance, mengambil dan memverifikasi image, menyimpan rekam boot, lalu memeriksa kesehatan setelah restart.

Snapshot yang diterbitkan adalah snapshot terbaru, bukan antrean histori. Setelah koneksi pulih, scheduler menunggu scan baru sebelum menerbitkan data. Source juga memiliki interval publish data 10 detik dan info 60 detik.

## Perangkat dan pengukuran

| Fungsi | Konfigurasi source |
|---|---|
| I2C | SDA GPIO21, SCL GPIO22 |
| MUX | S0/S1/S2 GPIO25/26/27 |
| LED RGB | Red/green/blue GPIO23/19/18 |
| ADS1115 | Address `0x48`, PGA plus/minus 2.048 V, LSB 0.0625 mV |
| CT01-CT08 | MUX Y0-Y7, pasangan ADS A0-A1 |
| CT09-CT16 | MUX Y0-Y7, pasangan ADS A2-A3 |

Setiap kanal membuang 16 pembacaan awal dan mengumpulkan minimal 20 sampel valid. Perhitungan RMS memakai akumulator AC dan mengubah RMS tegangan menjadi arus dengan faktor `30 A/V` dan gain kalibrasi kanal. Detail konektor fisik tetap perlu diverifikasi pada perangkat.

## Identitas perangkat dan topic MQTT

Device ID memakai MAC eFuse dalam huruf kapital dengan pemisah titik dua, misalnya `AA:BB:CC:DD:EE:FF`. Source memakai Wi-Fi MAC sebagai fallback bila MAC eFuse tidak valid. ID ini juga digunakan sebagai MQTT client ID.

Dengan root topic `cthub` dan contoh ID tersebut, topic yang dibentuk firmware adalah:

| Arah | Topic | Retained | Isi |
|---|---|---:|---|
| Publish | `cthub/AA:BB:CC:DD:EE:FF/data` | Tidak | Snapshot 16 kanal |
| Publish | `cthub/AA:BB:CC:DD:EE:FF/status` | Ya | Mode perangkat |
| Publish | `cthub/AA:BB:CC:DD:EE:FF/info` | Ya | Informasi runtime, memori, task, dan OTA |
| Publish | `cthub/AA:BB:CC:DD:EE:FF/ack` | Tidak | ACK command dan event OTA |
| Subscribe `TaskNetwork` | `cthub/command/AA:BB:CC:DD:EE:FF/update` | n/a | Permintaan OTA |
| Subscribe `TaskNetwork` | `cthub/AA:BB:CC:DD:EE:FF/command` | n/a | Command `info`, `config`, `calibration` (alias lama), dan `reboot` |

Data dijadwalkan setiap 10 detik dan info setiap 60 detik. Status diterbitkan saat koneksi baru terbentuk. Status dan info retained, sedangkan data dan ACK tidak retained.

### Payload data

Payload data memuat `schema_version`, `device_id`, `boot_id`, `sequence`, `monotonic_ms`, `timestamp`, `scan_duration_ms`, `paused`, dan `channels.ct01` sampai `channels.ct16`. `timestamp` berisi epoch UTC jika waktu tersinkronisasi, selain itu `null`. Setiap kanal berisi `current_a`, `valid`, `quality`, dan `age_ms`.

```json
{
  "schema_version": 1,
  "device_id": "AA:BB:CC:DD:EE:FF",
  "boot_id": "boot-id",
  "sequence": 42,
  "monotonic_ms": 84000,
  "timestamp": null,
  "scan_duration_ms": 200,
  "paused": false,
  "channels": {
    "ct01": {"current_a": 1.1, "valid": true, "quality": "ok", "age_ms": 0},
    "ct02": {"current_a": null, "valid": false, "quality": "sensor_error", "age_ms": null}
  }
}
```

Contoh hanya menampilkan dua kanal; firmware membentuk seluruh 16 kanal. Pembacaan nol yang valid dikirim sebagai `0`. Pembacaan yang tidak valid dikirim sebagai `null`. Nilai `quality` yang dikenal source adalah `ok`, `degraded`, `sensor_error`, `stale`, `disabled`, dan `paused`.

### Payload status, info, dan ACK

Status memiliki field `status` dengan nilai `booting`, `online`, `degraded`, `paused`, atau `offline`, serta `reason`.

Info memuat `schema_version`, `network_connected`, `sensor_sequence`, `calibration_revision`, `boot_count`, `crash_count`, `memory.free_heap`, high-water stack task sensor/network, dan `ota.phase` serta `ota.progress_bytes`.

ACK memiliki bentuk berikut:

```json
{"request_id":"req-001","command":"calibration","status":"completed","detail":"APPLIED"}
```

Untuk request yang tidak bisa dikaitkan dengan ID, `request_id` dapat bernilai `null`. ACK dikirim tanpa retain.

## Command yang dikenali source

### Command umum

Parser command umum mengenali `info`, `config`, `calibration` (alias kompatibilitas), dan `reboot`. Payload dibatasi maksimum 2048 byte dan membutuhkan `request_id`.

| Command | Field | Validasi dan hasil |
|---|---|---|
| `info` | `request_id`, `command` (opsional) | Memublikasikan payload info lalu ACK `completed/INFO_PUBLISHED`. Tanpa `command`, payload umum yang bukan kalibrasi atau reboot dikenali sebagai `info`. |
| `config` jumlah kanal | `request_id`, `command`, `active_channels` | Nilai 1-16 mengaktifkan kanal berurutan CT01 sampai CTxx dan disimpan pada NVS. Nilai awal sebelum konfigurasi adalah 16. ACK detail `ACTIVE_CHANNELS_UPDATED`. |
| `config` gain | `request_id`, `command`, `channel`, `gain` | Kanal 1-16 dan gain finite antara 0.1-10.0. Gain disimpan lewat penyimpanan kalibrasi yang ada. ACK detail `CALIBRATION_UPDATED`. |
| `calibration` | `request_id`, `channel`, `gain` | Alias lama tetap diterima; kanal 1-16 dan gain 0.1-10.0, dengan ACK command `calibration`. |
| `reboot` | `request_id`, `command`, `delay_seconds` (opsional) | Delay default 3 detik. Nilai dibatasi ke rentang 1-60 detik; reboot tidak memerlukan sinkronisasi waktu. |

Setiap request `config` hanya boleh membawa satu operasi. Jangan mengirim `active_channels` bersama `channel`/`gain`.

Atur dua kanal aktif:

```json
{"request_id":"cfg-001","command":"config","active_channels":2}
```

Atur gain CT01:

```json
{"request_id":"cfg-002","command":"config","channel":1,"gain":1.05}
```

Kanal nonaktif tetap ada dalam payload data dengan `valid:false`, `current_a:null`, dan `quality:"disabled"`; kondisi ini bukan error sensor.

Contoh alias kalibrasi lama:

```json
{"request_id":"cal-001","command":"calibration","channel":1,"gain":1.05}
```

Contoh reboot tanpa delay (default 3 detik):

```json
{"request_id":"reboot-001","command":"reboot"}
```

Delay dapat ditentukan secara opsional:

```json
{"request_id":"reboot-002","command":"reboot","delay_seconds":5}
```

Nilai di bawah 1 menjadi 1 detik dan nilai di atas 60 menjadi 60 detik. `expires_at` bukan field reboot dan tidak diperlukan pada request OTA CT-Hub.

Command umum diterima pada topic `/command`, dimasukkan ke runtime queue, lalu diproses oleh `networkProcessOneCommand()`. `config` dan `reboot` dapat menyebut command secara eksplisit. Untuk kompatibilitas, tanpa field `command` firmware tetap mengenali kalibrasi lama dari `channel`+`gain`, reboot dari `delay_seconds`, dan info dari payload umum lainnya.

### OTA

OTA diterima pada `cthub/command/{device_id}/update`. Kirim link manifest pada field `url` atau `manifest_url`; `action` tidak wajib karena default-nya `manifest`. `request_id` opsional dan hanya diperlukan jika ACK perlu dikorelasikan dengan request. OTA tidak memerlukan `expires_at` atau sinkronisasi waktu; `expires_at` yang masih dikirim oleh client lama diabaikan.

Contoh request minimum:

```json
{"url":"https://updates.example/device.json"}
```

Dengan ID untuk mencocokkan ACK:

```json
{"request_id":"ota-001","url":"https://updates.example/device.json"}
```

Manifest harus berisi `version`, `url` image, `md5`, `product_id`, dan `board_id`; `size_bytes` bersifat opsional. Identitas produk dan board harus cocok dengan `CT_HUB_16` dan `ESP32_DEVKITC_V4`. Versi target harus lebih baru dari versi yang berjalan.

Untuk `direct`, URL image dapat diberikan melalui `binary_url`, `download_url`, atau `url`, disertai `md5`:

```json
{"request_id":"ota-002","action":"direct","binary_url":"https://updates.example/ct-hub.bin","md5":"0123456789abcdef0123456789abcdef"}
```

URL harus HTTPS. Jika beberapa alias URL diberikan, nilainya harus sama. MD5 harus terdiri dari 32 karakter heksadesimal. Untuk `rollback`, kirim `confirm:true`; rollback hanya tersedia jika catatan partisi dan dukungan platform memungkinkan:

```json
{"request_id":"ota-003","action":"rollback","confirm":true}
```

ACK awal OTA berstatus `accepted` jika request masuk antrean, atau status gagal/busy dengan detail penyebab. Tanpa `request_id`, field ACK tersebut bernilai `null`; dengan ID, nilai yang sama diteruskan ke ACK progress dan hasil. Progress dan hasil akhir dikirim pada topic ACK. Source memverifikasi image, identitas, versi, ukuran bila disediakan, serta MD5; validasi TLS memakai `CT_ROOT_CA`, bundle ROOT CA yang sama dengan Gree. Redirect HTTP tidak diikuti oleh adapter platform.

## Konfigurasi

Isi variabel konfigurasi di bagian atas `config.cpp` sesuai jaringan dan server CT-Hub:

- `CT_WIFI_SSID` dan `CT_WIFI_PASSWORD`
- `CT_MQTT_HOST`, `CT_MQTT_PORT`, `CT_MQTT_USERNAME`, dan `CT_MQTT_PASSWORD`
- `CT_ROOT_CA` berisi bundle ROOT CA untuk koneksi HTTPS OTA
- `CT_HUB_FW_VERSION_TAG` saat ini `CT_HUB_VER:1.0.0`; ubah tag saat merilis versi berikutnya.

Koneksi MQTT memakai TCP biasa sesuai endpoint `mqtt://` pada port 1883; username dan password tidak dienkripsi pada transport ini, jadi gunakan hanya pada jaringan lokal yang dipercaya. HTTPS OTA tetap memvalidasi server memakai `CT_ROOT_CA`. Kredensial Wi-Fi/MQTT akan tertanam pada firmware hasil compile. Jangan masukkan kredensial privat ke repository bersama. OTA memerlukan partition table dengan OTA data partition dan setidaknya dua slot aplikasi.

## Kalibrasi

Gunakan beban referensi yang stabil. Hitung gain baru dengan rumus:

```text
newGain = oldGain * referenceCurrent / measuredCurrent
```

Gain harus tetap dalam rentang 0.1-10.0. Source menyimpan kalibrasi melalui adapter persistence; verifikasi penyimpanan dan hasil ukur pada board setelah integrasi.

## Batas verifikasi

Firmware memakai API Arduino-ESP32 dan FreeRTOS secara langsung untuk target ESP32 Dev Module. Operasi sensor, Wi-Fi/MQTT, indikator health, command, dan OTA kini dihubungkan melalui task dan adapter board yang sesuai.
