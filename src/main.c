#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <ctype.h>
#include "mfrc522.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Must match the nodelabel in your app.overlay (e.g., rfid0: mfrc522@0) */
static const struct device *const rfid = DEVICE_DT_GET(DT_NODELABEL(rfid001));

/* Default MIFARE Classic Key A (all 0xFF) */
static const uint8_t default_key[MFRC522_KEY_LEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* Sector 1 trailer block (covers data blocks 4, 5, and 6) */
#define TARGET_TRAILER_BLOCK 7

int main(void)
{
    uint8_t ver = 0;
    struct mfrc522_tag_data tag;
    int ret;

    if (!device_is_ready(rfid)) {
        LOG_ERR("MFRC522 device is not ready (check SPI/RST wiring and app.overlay)");
        return -ENODEV;
    }

    /* Quick hardware sanity check via VersionReg (0x37) */
    ret = mfrc522_reg_read(rfid, MFRC522_REG_VERSION, &ver);
    if (ret == 0) {
        LOG_INF("MFRC522 Ready! Firmware Version: 0x%02x", ver);
    }

    LOG_INF("Tap an RFID card or keyfob near the antenna...");

    while (1) {
        ret = mfrc522_read_sector(rfid, TARGET_TRAILER_BLOCK, default_key, &tag);

        if (ret == 0) {
            /* Clean non-printable bytes from factory-blank cards before printing */
            for (size_t i = 0; i < MFRC522_SECTOR_DATA_LEN; i++) {
                if (!isprint((unsigned char)tag.text[i])) {
                    tag.text[i] = '.';
                }
            }

            LOG_INF("Tag Detected! UID: %02X:%02X:%02X:%02X (ID: %u)",
                    tag.uid_raw[0], tag.uid_raw[1],
                    tag.uid_raw[2], tag.uid_raw[3],
                    tag.uid_num);
            LOG_INF("Sector Data : %s", tag.text);

            /* Wait 1 second after a successful read so it doesn't spam logs */
            k_sleep(K_SECONDS(1));
        } else if (ret == -EAGAIN) {
            /* No card in field — poll again shortly */
            k_sleep(K_MSEC(150));
        } else {
            LOG_WRN("Card detected, but read returned error: %d", ret);
            k_sleep(K_MSEC(500));
        }
    }

    return 0;
}