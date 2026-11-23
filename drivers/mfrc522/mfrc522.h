#ifndef MFRC522_H_
#define MFRC522_H_


#include <zephyr/device.h>
#include <stdint.h>
#include <errno.h>


// Proximity Coupling Device 
#define PCD_RESETPHASE 0x0F

// MRRC522 Registers 
#define CommandReg 0x01
#define CmdCalcCrc 0x03
#define TModeReg 0x2A
#define TPrescalerReg 0x2B
#define TReloadRegL 0x2D
#define TReloadRegH 0x2C
#define TxAutoReg 0x15
#define ModeReg 0x11
#define TxControlReg 0x14
#define RegBitFram 0x0D
#define CMDTransceive 0x0C
#define RegCommIen 0x02
#define RegCommIrq 0x04
#define RegError 0x06
#define RegFifoData 0x09
#define RegFifoLevel 0x0A
#define RegControl 0x0C
#define RegDivIrq 0x05
#define RegCrcResultH 0x21
#define RegCrcResultL 0x22


#define PICC_REQIDL  0x26
#define PICC_REQALL 0x52
#define PICC_ANTICOLL 0X93
#define PICC_SELECTTAG 0x93
#define PICC_AUTHEN1A 0X60
#define PICC_AUTHEN1B 0x061
#define PICC_READ 0X30


#define MFRC522_REG_VERSION 0x37
#define MFRC522_REG_STATUS2     0x08
#define MFRC522_BIT_CRYPTO1_ON  0x08  /* Bit 3 in Status2Reg */

// Commands 

#define MFRC522_CMD_IDLE 0X00
#define MFRC522_CMD_MF_AUTHENT 0X0E
#define MFRC522_CMD_SOFT_RESET 0x0F

#define MFRC522_BLOCK_SIZE      16
#define MFRC522_SECTOR_DATA_LEN (MFRC522_BLOCK_SIZE * 3) /* 48 bytes per sector */
#define MFRC522_KEY_LEN         6

#define MFRC522_MAX_LEN 18
struct mfrc522_tag_data {
    uint32_t uid_num;                           /* Numeric UID (from 4-byte UID) */
    uint8_t  uid_raw[5];                        /* 4 UID bytes + 1 BCC byte */
    char     text[MFRC522_SECTOR_DATA_LEN + 1]; /* 48 chars + '\0' terminator */
};


// Public Driver Function

/* mfrc522.h */

/* 1. Function pointer types */
typedef int (*mfrc522_api_read_t)(const struct device *dev, uint8_t reg, uint8_t *val);
typedef int (*mfrc522_api_write_t)(const struct device *dev, uint8_t reg, uint8_t val);
typedef int (*mfrc522_api_read_sector_t)(const struct device *dev,
    uint8_t trailer_block,
    const uint8_t key[MFRC522_KEY_LEN],
    struct mfrc522_tag_data *out);

/* 2. Driver API Structure */
struct mfrc522_driver_api {
    mfrc522_api_read_t read;
    mfrc522_api_write_t write;
    mfrc522_api_read_sector_t read_sector;
};



/* mfrc522.h (தொடர்ச்சி) */

static inline int mfrc522_reg_read(const struct device *dev, uint8_t reg, uint8_t *val)
{
    const struct mfrc522_driver_api *api = (const struct mfrc522_driver_api *)dev->api;

    if (api == NULL || api->read == NULL) {
        return -ENOSYS;
    }
    return api->read(dev, reg, val);
}

static inline int mfrc522_reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
    const struct mfrc522_driver_api *api = (const struct mfrc522_driver_api *)dev->api;

    if (api == NULL || api->write == NULL) {
        return -ENOSYS;
    }
    return api->write(dev, reg, val);
}

static inline int mfrc522_read_sector(const struct device *dev,
                        uint8_t trailer_block,
                        const uint8_t key[MFRC522_KEY_LEN],
                        struct mfrc522_tag_data *out)
    {
        const struct mfrc522_driver_api *api = (const struct mfrc522_driver_api *)dev->api;

        if (api == NULL || api->read_sector ==NULL){
            return -ENOSYS;
        }

        return api->read_sector(dev,trailer_block,key,out);
    }
#endif
