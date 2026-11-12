#include "config.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

const char* CT_WIFI_SSID = "NICO-ISEMS-4G";
const char* CT_WIFI_PASSWORD = "@Nico1234";
const char* CT_MQTT_HOST = "192.168.80.254";
const uint16_t CT_MQTT_PORT = 1883;
const char* CT_MQTT_USERNAME = "smart-led";
const char* CT_MQTT_PASSWORD = "smart-led-2026";
const char* CT_ROOT_CA = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICpjCCAiugAwIBAgIRAIchZfw0tuX7qK3Vs3BftTowCgYIKoZIzj0EAwMwTzEL
MAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2VhcmNo
IEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDIwHhcNMjYwNTEzMDAwMDAwWhcN
MzIwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzENMAsGA1UEChMESVNSRzEQMA4G
A1UEAxMHUm9vdCBZRTB2MBAGByqGSM49AgEGBSuBBAAiA2IABDwS/6vhrcVqcbBo
+wgdI3fwn9x7DNJJOY/lTOti0vkwuRN87RhEhTH17E7XyFjWsPYhIPt/wzOqxTd2
b+4ZJNy9ID04YywF9U5zasDVyGSNErVNtz8uSGh5izW87j77GaOB6zCB6DAOBgNV
HQ8BAf8EBAMCAQYwEwYDVR0lBAwwCgYIKwYBBQUHAwEwDwYDVR0TAQH/BAUwAwEB
/zAdBgNVHQ4EFgQUo8gmWo6hTNA1Y/ybI8g6rlbzT1YwHwYDVR0jBBgwFoAUfEKW
rt5LSDv6kviejM9ti6lyN5UwMgYIKwYBBQUHAQEEJjAkMCIGCCsGAQUFBzAChhZo
dHRwOi8veDIuaS5sZW5jci5vcmcvMBMGA1UdIAQMMAowCAYGZ4EMAQIBMCcGA1Ud
HwQgMB4wHKAaoBiGFmh0dHA6Ly94Mi5jLmxlbmNyLm9yZy8wCgYIKoZIzj0EAwMD
aQAwZgIxAMU19WCtmxVND8UHBZRoma49Z7jPs64Dma0eTu1OChVbB/2J7GV3nvYK
Ax54uk1G9QIxAO0miLVJu8PLNiXXXkiE/gsK3CTRTF/aeo4bMX42Zw40csRU6AC2
6hSW1/IWaas6dg==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG
A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv
b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw
MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i
YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT
aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ
jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp
xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp
1Wrjsok6Vjk4bwY8iGlbKk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdG
snUOhugZitVtbNV4FpWi6cgKOOvyJBNPc1STE4U6G7weNLWLBYy5d4ux2x8gkasJ
U26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrXgzT/LCrBbBlDSgeF59N8
9iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E
BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B
AQUFAAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOz
yj1hTdNGCbM+w6DjY1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE
38NflNUVyRRBnMRddWQVDf9VMOyGj/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymP
AbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhHhm4qxFYxldBniYUr+WymXUad
DKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveCX4XSQRjbgbME
HMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==
-----END CERTIFICATE-----
)EOF";
extern const char CT_HUB_FW_VERSION_TAG[] = "CT_HUB_VER:1.0.0";

namespace {

constexpr CtChannelConfig kInvalidCtChannel = {
    0,
    false,
    "",
    0,
    AdsPair::A0_A1,
    1.0f,
};

constexpr CtChannelConfig kCtChannels[CT_CHANNEL_COUNT] = {
    {1, true, "CT1", 0, AdsPair::A0_A1, 1.0f},
    {2, true, "CT2", 1, AdsPair::A0_A1, 1.0f},
    {3, true, "CT3", 2, AdsPair::A0_A1, 1.0f},
    {4, true, "CT4", 3, AdsPair::A0_A1, 1.0f},
    {5, true, "CT5", 4, AdsPair::A0_A1, 1.0f},
    {6, true, "CT6", 5, AdsPair::A0_A1, 1.0f},
    {7, true, "CT7", 6, AdsPair::A0_A1, 1.0f},
    {8, true, "CT8", 7, AdsPair::A0_A1, 1.0f},
    {9, true, "CT9", 0, AdsPair::A2_A3, 1.0f},
    {10, true, "CT10", 1, AdsPair::A2_A3, 1.0f},
    {11, true, "CT11", 2, AdsPair::A2_A3, 1.0f},
    {12, true, "CT12", 3, AdsPair::A2_A3, 1.0f},
    {13, true, "CT13", 4, AdsPair::A2_A3, 1.0f},
    {14, true, "CT14", 5, AdsPair::A2_A3, 1.0f},
    {15, true, "CT15", 6, AdsPair::A2_A3, 1.0f},
    {16, true, "CT16", 7, AdsPair::A2_A3, 1.0f},
};

constexpr uint32_t kCalibrationCommitMarker = 0x43544231U;
constexpr char kCalibrationSlotAKey[] = "cal_a";
constexpr char kCalibrationSlotBKey[] = "cal_b";
constexpr char kActiveChannelCountKey[] = "active_channels";
portMUX_TYPE g_calibrationMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t g_activeChannelCount = CT_DEFAULT_ACTIVE_CHANNEL_COUNT;
float g_calibrationGains[CT_CHANNEL_COUNT] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
};

void publishCalibrationGains(const float* gains) {
  portENTER_CRITICAL(&g_calibrationMux);
  for (std::size_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    g_calibrationGains[index] = gains[index];
  }
  portEXIT_CRITICAL(&g_calibrationMux);
}

struct CalibrationBlob {
  uint32_t commitMarker;
  uint16_t schemaVersion;
  uint16_t reserved;
  uint32_t revision;
  float gains[CT_CHANNEL_COUNT];
  uint32_t checksum;
};

void setCalibrationDefaults(CalibrationPersistence& persistence) {
  persistence.schemaVersion = CT_CALIBRATION_SCHEMA_VERSION;
  persistence.revision = 0;
  for (std::size_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    persistence.gains[index] = 1.0f;
  }
}

bool isValidCalibrationGain(float gain) {
  return std::isfinite(gain) && gain >= CT_CALIBRATION_GAIN_MIN && gain <= CT_CALIBRATION_GAIN_MAX;
}

bool areValidCalibrationGains(const float* gains, std::size_t gainCount) {
  if (gains == nullptr || gainCount != CT_CHANNEL_COUNT) {
    return false;
  }
  for (std::size_t index = 0; index < gainCount; ++index) {
    if (!isValidCalibrationGain(gains[index])) {
      return false;
    }
  }
  return true;
}

uint32_t calculateCalibrationChecksum(const CalibrationBlob& blob) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&blob);
  uint32_t checksum = 2166136261U;
  for (std::size_t index = 0; index < offsetof(CalibrationBlob, checksum); ++index) {
    checksum ^= bytes[index];
    checksum *= 16777619U;
  }
  return checksum;
}

bool isCommittedCalibrationBlob(const CalibrationBlob& blob) {
  return blob.commitMarker == kCalibrationCommitMarker &&
         blob.schemaVersion == CT_CALIBRATION_SCHEMA_VERSION &&
         blob.checksum == calculateCalibrationChecksum(blob) &&
         areValidCalibrationGains(blob.gains, CT_CHANNEL_COUNT);
}

bool readCalibrationSlot(Preferences& preferences, const char* key, CalibrationBlob& blob) {
  if (preferences.getBytesLength(key) != sizeof(CalibrationBlob)) {
    return false;
  }
  if (preferences.getBytes(key, &blob, sizeof(blob)) != sizeof(blob)) {
    return false;
  }
  return isCommittedCalibrationBlob(blob);
}

bool isNewerRevision(uint32_t candidate, uint32_t current) {
  return static_cast<int32_t>(candidate - current) > 0;
}

bool preferencesLoad(
    void*,
    uint16_t& schemaVersion,
    uint32_t& revision,
    float* gains,
    std::size_t gainCount
) {
  if (gains == nullptr || gainCount != CT_CHANNEL_COUNT) {
    return false;
  }

  Preferences preferences;
  if (!preferences.begin("cthub", true)) {
    return false;
  }
  CalibrationBlob slotA{};
  CalibrationBlob slotB{};
  const bool slotAValid = readCalibrationSlot(preferences, kCalibrationSlotAKey, slotA);
  const bool slotBValid = readCalibrationSlot(preferences, kCalibrationSlotBKey, slotB);
  preferences.end();

  if (!slotAValid && !slotBValid) {
    return false;
  }
  const CalibrationBlob& selected =
      !slotAValid ? slotB : (!slotBValid || isNewerRevision(slotA.revision, slotB.revision)) ? slotA : slotB;
  schemaVersion = selected.schemaVersion;
  revision = selected.revision;
  for (std::size_t index = 0; index < gainCount; ++index) {
    gains[index] = selected.gains[index];
  }
  return true;
}

bool preferencesSave(
    void*,
    uint16_t schemaVersion,
    uint32_t revision,
    const float* gains,
    std::size_t gainCount
) {
  if (!areValidCalibrationGains(gains, gainCount)) {
    return false;
  }

  CalibrationBlob candidate{};
  candidate.commitMarker = kCalibrationCommitMarker;
  candidate.schemaVersion = schemaVersion;
  candidate.revision = revision;
  for (std::size_t index = 0; index < gainCount; ++index) {
    candidate.gains[index] = gains[index];
  }
  candidate.checksum = calculateCalibrationChecksum(candidate);

  Preferences preferences;
  if (!preferences.begin("cthub", false)) {
    return false;
  }
  CalibrationBlob slotA{};
  CalibrationBlob slotB{};
  const bool slotAValid = readCalibrationSlot(preferences, kCalibrationSlotAKey, slotA);
  const bool slotBValid = readCalibrationSlot(preferences, kCalibrationSlotBKey, slotB);
  const char* targetKey = kCalibrationSlotAKey;
  if (slotAValid && (!slotBValid || isNewerRevision(slotA.revision, slotB.revision))) {
    targetKey = kCalibrationSlotBKey;
  } else if (slotBValid) {
    targetKey = kCalibrationSlotAKey;
  }
  const bool saved = preferences.putBytes(targetKey, &candidate, sizeof(candidate)) == sizeof(candidate);
  preferences.end();
  return saved;
}
bool loadCalibrationRecord(
    CalibrationPersistence& persistence,
    uint16_t& schemaVersion,
    uint32_t& revision,
    float* gains
) {
  if (persistence.load != nullptr) {
    return persistence.load(persistence.context, schemaVersion, revision, gains, CT_CHANNEL_COUNT);
  }
  return preferencesLoad(nullptr, schemaVersion, revision, gains, CT_CHANNEL_COUNT);
}

bool saveCalibrationRecord(const CalibrationPersistence& persistence, uint32_t revision, const float* gains) {
  if (persistence.save != nullptr) {
    return persistence.save(
        persistence.context,
        CT_CALIBRATION_SCHEMA_VERSION,
        revision,
        gains,
        CT_CHANNEL_COUNT
    );
  }
  return preferencesSave(nullptr, CT_CALIBRATION_SCHEMA_VERSION, revision, gains, CT_CHANNEL_COUNT);
}

}  // namespace

CtChannelConfig ctGetChannelConfig(uint8_t channelIndex) {
  if (channelIndex >= CT_CHANNEL_COUNT) {
    return kInvalidCtChannel;
  }
  CtChannelConfig channel = kCtChannels[channelIndex];
  portENTER_CRITICAL(&g_calibrationMux);
  channel.calibrationGain = g_calibrationGains[channelIndex];
  channel.enabled = channel.enabled && channelIndex < g_activeChannelCount;
  portEXIT_CRITICAL(&g_calibrationMux);
  return channel;
}

bool ctGetChannelConfigs(CtChannelConfig* output, std::size_t channelCount) {
  if (output == nullptr || channelCount != CT_CHANNEL_COUNT) return false;
  portENTER_CRITICAL(&g_calibrationMux);
  for (std::size_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    output[index] = kCtChannels[index];
    output[index].calibrationGain = g_calibrationGains[index];
    output[index].enabled = output[index].enabled && index < g_activeChannelCount;
  }
  portEXIT_CRITICAL(&g_calibrationMux);
  return true;
}

bool ctIsValidExternalChannel(uint8_t channelNumber) {
  return channelNumber >= 1 && channelNumber <= CT_CHANNEL_COUNT;
}

bool ctLoadActiveChannelCount(uint8_t& output) {
  output = CT_DEFAULT_ACTIVE_CHANNEL_COUNT;
  Preferences preferences;
  if (!preferences.begin("cthub", true)) return false;
  const bool hasValue = preferences.isKey(kActiveChannelCountKey);
  const uint8_t storedValue = hasValue
                                  ? preferences.getUChar(kActiveChannelCountKey,
                                                         CT_DEFAULT_ACTIVE_CHANNEL_COUNT)
                                  : CT_DEFAULT_ACTIVE_CHANNEL_COUNT;
  preferences.end();
  if (storedValue == 0 || storedValue > CT_CHANNEL_COUNT) return false;
  output = storedValue;
  portENTER_CRITICAL(&g_calibrationMux);
  g_activeChannelCount = output;
  portEXIT_CRITICAL(&g_calibrationMux);
  return true;
}

bool ctSetActiveChannelCount(uint8_t channelCount) {
  if (channelCount == 0 || channelCount > CT_CHANNEL_COUNT) return false;
  Preferences preferences;
  if (!preferences.begin("cthub", false)) return false;
  const bool saved = preferences.putUChar(kActiveChannelCountKey, channelCount) == sizeof(uint8_t);
  preferences.end();
  if (!saved) return false;
  portENTER_CRITICAL(&g_calibrationMux);
  g_activeChannelCount = channelCount;
  portEXIT_CRITICAL(&g_calibrationMux);
  return true;
}

bool ctLoadCalibration(CalibrationPersistence& persistence) {
  uint16_t schemaVersion = 0;
  uint32_t revision = 0;
  float gains[CT_CHANNEL_COUNT]{};
  const bool loaded = loadCalibrationRecord(persistence, schemaVersion, revision, gains);
  if (!loaded || schemaVersion != CT_CALIBRATION_SCHEMA_VERSION ||
      !areValidCalibrationGains(gains, CT_CHANNEL_COUNT)) {
    setCalibrationDefaults(persistence);
    publishCalibrationGains(persistence.gains);
    return true;
  }

  persistence.schemaVersion = schemaVersion;
  persistence.revision = revision;
  for (std::size_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    persistence.gains[index] = gains[index];
  }
  publishCalibrationGains(persistence.gains);
  return true;
}

bool ctSetCalibrationGain(uint8_t channelIndex, float gain, CalibrationPersistence& persistence) {
  if (channelIndex >= CT_CHANNEL_COUNT || !isValidCalibrationGain(gain) ||
      !areValidCalibrationGains(persistence.gains, CT_CHANNEL_COUNT)) {
    return false;
  }

  float candidateGains[CT_CHANNEL_COUNT]{};
  for (std::size_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    candidateGains[index] = persistence.gains[index];
  }
  candidateGains[channelIndex] = gain;
  const uint32_t candidateRevision = persistence.revision == std::numeric_limits<uint32_t>::max()
                                         ? 1U
                                         : persistence.revision + 1U;
  if (!saveCalibrationRecord(persistence, candidateRevision, candidateGains)) {
    return false;
  }

  persistence.schemaVersion = CT_CALIBRATION_SCHEMA_VERSION;
  persistence.revision = candidateRevision;
  for (std::size_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    persistence.gains[index] = candidateGains[index];
  }
  publishCalibrationGains(persistence.gains);
  return true;
}
