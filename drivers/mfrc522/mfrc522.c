#define DT_DRV_COMPAT nxp_mfrc522

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "mfrc522.h"


LOG_MODULE_REGISTER(mfrc522, CONFIG_MFRC522_LOG_LEVEL);
#define MFRC522_SPI_OP (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)


// Hardware config pulled from devicetree


struct mfrc522_config{
    struct spi_dt_spec spi;
    struct gpio_dt_spec rst_gpio;
};


struct mfrc522_data{
    uint8_t chip_version;
};

int reg_write(const  struct device *dev , uint8_t reg,  uint8_t val)
{
    const struct mfrc522_config *cfg = dev->config;

    uint8_t tx_buf_data[2] = { (reg << 1) & 0x7E ,val};

    struct spi_buf tx_buf  = {.buf = tx_buf_data, .len = sizeof(tx_buf_data)};
    struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};

    return spi_write_dt(&cfg->spi,&tx_set);
}

int reg_read(const struct device *dev , uint8_t reg , uint8_t *val)
{
    const struct mfrc522_config *cfg = dev->config;
    uint8_t tx_buf_data[2] = { ((reg<<1) & 0x7E) | 0x80,0x00};
    uint8_t rx_buf_data[2] = {0};

    struct spi_buf tx_buf = { .buf = tx_buf_data, .len = sizeof(tx_buf_data) };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

    struct spi_buf rx_buf = { .buf = rx_buf_data, .len = sizeof(rx_buf_data) };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    int ret = spi_transceive_dt(&cfg->spi,&tx_set,&rx_set);
    if (ret == 0){
        *val = rx_buf_data[1];
    }

    return ret;

}

static int Reset(const struct device *dev){
    int ret = reg_write(dev,CommandReg,PCD_RESETPHASE);

    if (ret < 0){
        return ret;
    }

    k_msleep(50);

    return 0;

}
static int clearBitMask(const struct device *dev,uint8_t reg,uint8_t mask){
    uint8_t temp = 0;
    int ret;

    ret = reg_read(dev,reg,&temp);

    if (ret < 0){
        return ret;
    }

    ret = reg_write(dev,reg,temp & (~mask));

    if (ret < 0){
        return ret;
    }

    return 0;
}
static int setBitMask(const struct device *dev,uint8_t reg , uint8_t mask){
    uint8_t temp = 0;
    int ret ;

    ret = reg_read(dev,reg,&temp);

    if (ret < 0){
        return 0;
    }

    ret = reg_write(dev,reg,temp|mask);

    if (ret < 0 ){
        return 0;
    }

    return 0;
}



static int mfrc522_stop_crypto1(const struct device *dev)
{
    uint8_t status2 = 0;
    int ret = mfrc522_reg_read(dev, MFRC522_REG_STATUS2, &status2);
    if (ret < 0) {
        return ret;
    }

    return mfrc522_reg_write(dev, MFRC522_REG_STATUS2, status2 & ~MFRC522_BIT_CRYPTO1_ON);
}

static int AntennaOn(const struct device *dev){
    uint8_t temp = 0;
    int ret;
    ret = reg_read(dev,TxControlReg,&temp);
    
    if (ret < 0){
        return ret;
    }

    if ((temp & 0x03 ) != 0x03){
        ret = setBitMask(dev,TxControlReg,0x03);

        if(ret < 0){
            return ret;
        }
    }

    return 0;
}


static bool mfrc522_is_valid_trailer(uint8_t trailer_block){
    return (trailer_block < 64 ) && (((trailer_block + 1) % 4) == 0);
}

static uint32_t mfrc522_uid_to_num(const uint8_t uid[5])
{
    uint32_t num = 0;
    for (int  i = 0;i<4;i++){
        num = (num << 8 | uid[i]);
    }

    return num;
}

static int mfrc522_to_card(const struct device *dev,
                            uint8_t command,
                            const uint8_t *send_data ,
                            size_t send_len,
                            uint8_t *back_data,
                            size_t *back_len,
                            uint8_t *back_bits)
{
    uint8_t irq_en = 0x00;
    uint8_t wait_irq = 0x00;
    uint8_t irq_val = 0x00;
    uint8_t err_val = 0x00;
    int ret;

    if (command == MFRC522_CMD_MF_AUTHENT){
        irq_en = 0x12;
        wait_irq = 0x10;
    }  else if(command == CMDTransceive){
        irq_en = 0x77;
        wait_irq = 0x30;
    }


    reg_write(dev,RegCommIen,irq_en | 0x80);
    clearBitMask(dev,RegCommIrq,0x80);
    setBitMask(dev,RegFifoLevel,0x80);

    reg_write(dev,CommandReg,MFRC522_CMD_IDLE);

    for(size_t i = 0;i<send_len;i++){
        ret = reg_write(dev,RegFifoData,send_data[i]);
        if (ret < 0){
            return ret;
        }
    }

    ret = reg_write(dev,CommandReg,command);

    if (ret < 0){
        return ret;
    }

    if (command == CMDTransceive){
        setBitMask(dev,RegBitFram,0x80);
    }

    int64_t deadline = k_uptime_get() + 40;
    bool timed_out = true;
    while (k_uptime_get() < deadline){
        ret = reg_read(dev,RegCommIrq,&irq_val);
        if( ret < 0){
            clearBitMask(dev,RegBitFram,0x80);
            return ret;
        }

        if(irq_val & wait_irq){
            timed_out = false;
            break;
        }

        if(irq_val & 0x01){
            break;
        }

        k_sleep(K_MSEC(1));
    }

    clearBitMask(dev,RegBitFram,0x80);

    if(timed_out){
        return -ETIMEDOUT;
    }

    ret = reg_read(dev,RegError,&err_val);


    if(ret < 0 ){
        return ret;
    }

    if((err_val & 0x1B) != 0x00){
        return -EIO;
    }

    if (irq_val & irq_en & 0x01){
        return -ETIMEDOUT;
    }

    if (command == CMDTransceive && back_data != NULL && back_len != NULL)
    {
        uint8_t fifo_level = 0;
        uint8_t last_bits = 0;
        size_t max_buf = *back_len;

        reg_read(dev, RegFifoLevel, &fifo_level);
        reg_read(dev, RegControl, &last_bits);
        last_bits &= 0x07;

        if (back_bits != NULL) {
            if (last_bits != 0 && fifo_level > 0) {
                *back_bits = (fifo_level - 1) * 8 + last_bits;
            } else {
                *back_bits = fifo_level * 8;
            }
        }

        size_t to_read = fifo_level;
        if (to_read == 0) {
            to_read = 1;
        }
        if (to_read > MFRC522_MAX_LEN) {
            to_read = MFRC522_MAX_LEN;
        }
        if (to_read > max_buf) {
            to_read = max_buf;
        }

        for (size_t i = 0; i < to_read; i++) {
            ret = reg_read(dev, RegFifoData, &back_data[i]);
            if (ret < 0) {
                return ret;
            }
        }
        *back_len = to_read;
    }

    return 0;
}
    
static int mfrc522_request(const struct device *dev,uint8_t req_mode ,uint8_t tag_type[2]){

    uint8_t back_data[2] = {0};
    size_t back_len = sizeof(back_data);
    uint8_t back_bits = 0;
    int ret;

    ret = reg_write(dev,RegBitFram,0x07);
    
    if(ret < 0){
        return ret;
    }

    ret = mfrc522_to_card(dev,
                        CMDTransceive,
                        &req_mode , 1,
                        back_data,&back_len,
                        &back_bits
                        );
    
    if (ret < 0 ){
        return ret;
    }

    if(back_bits != 0x10){
        return -EPROTO;
    }

    if (tag_type != NULL){
        tag_type[0] = back_data[0];
        tag_type[1] = back_data[1];
    }
    return 0;
}

static int mfrc522_anticoll(const struct device *dev,
                            uint8_t uid[5])
{
    uint8_t cmd[2] = { PICC_ANTICOLL, 0x20 };
    uint8_t back_data[5] = { 0 };
    size_t back_len = sizeof(back_data);
    uint8_t back_bits = 0;
    uint8_t bcc_check = 0;
    int ret;

    if (uid == NULL){
        return -EINVAL;
    }

    ret = reg_write(dev,RegBitFram,0x00);
    
    if(ret < 0){
        return ret;
    }

    ret = mfrc522_to_card(dev,
                        CMDTransceive,
                        cmd,
                        sizeof(cmd),
                        back_data,&back_len,
                        &back_bits);

    if (ret < 0){
        return ret;
    }


    if(back_len != 5 || back_bits != 40){
        return -EPROTO;
    }


    for (int i = 0;i<4;i++){
        bcc_check ^= back_data[i];
    }

    if(bcc_check != back_data[4]){
        return -EBADMSG;
    }

    memcpy(uid,back_data,5);
    
    return 0;
}

/* Internal helper: equivalent to self.CalulateCRC(pIndata) */
static int mfrc522_calculate_crc(const struct device *dev,
                                 const uint8_t *in_data, size_t len,
                                 uint8_t out_crc[2])
{
    uint8_t irq_val = 0;
    int ret;

    /* 1. Clear CRCIRq flag (bit 2 = 0x04) in DivIrqReg and flush FIFO */
    clearBitMask(dev, RegDivIrq, 0x04);
    setBitMask(dev, RegFifoLevel, 0x80);
    reg_write(dev, CommandReg, MFRC522_CMD_IDLE);

    /* 2. Write input bytes into FIFO */
    for (size_t i = 0; i < len; i++) {
        ret = reg_write(dev, RegFifoData, in_data[i]);
        if (ret < 0) {
            return ret;
        }
    }

    /* 3. Start CRC calculation */
    ret =  reg_write(dev, CommandReg, CmdCalcCrc);
    if (ret < 0) {
        return ret;
    }

    /* 4. Poll DivIrqReg until CRCIRq (0x04) sets or 90ms timeout expires */
    int64_t deadline = k_uptime_get() + 90;
    bool done = false;

    while (k_uptime_get() < deadline) {
        ret = reg_read(dev, RegDivIrq, &irq_val);
        if (ret < 0) {
            return ret;
        }
        if (irq_val & 0x04) {
            done = true;
            break;
        }
        k_sleep(K_MSEC(1));
    }

    reg_write(dev, CommandReg, MFRC522_CMD_IDLE);

    if (!done) {
        return -ETIMEDOUT;
    }

    /* 5. Read 16-bit CRC result (Low byte first, High byte second) */
    reg_read(dev,RegCrcResultL, &out_crc[0]);
    reg_read(dev, RegCrcResultH, &out_crc[1]);

    return 0;
}

int mfrc522_select_tag(const struct device *dev, const uint8_t uid[5], uint8_t *sak)
{
    uint8_t buf[9];
    uint8_t back_data[3] = { 0 };
    size_t back_len = sizeof(back_data);
    uint8_t back_bits = 0;
    int ret;

    if (uid == NULL) {
        return -EINVAL;
    }

    /* 1. Build the 7-byte header: [0x93, 0x70, uid[0..4]] */
    buf[0] = PICC_SELECTTAG;
    buf[1] = 0x70;
    memcpy(&buf[2], uid, 5);

    /* 2. Append 2-byte CRC_A into buf[7] and buf[8] */
    ret = mfrc522_calculate_crc(dev, buf, 7, &buf[7]);
    if (ret < 0) {
        return ret;
    }

    /* 3. Transceive all 9 bytes and expect 24 bits (0x18 = 1 SAK byte + 2 CRC bytes) */
    ret = mfrc522_to_card(dev,
                          CMDTransceive,
                          buf, sizeof(buf),
                          back_data, &back_len,
                          &back_bits);
    if (ret < 0) {
        return ret;
    }

    if (back_bits != 0x18 || back_len < 1) {
        return -EPROTO;
    }

    LOG_DBG("Card SAK: 0x%02x", back_data[0]);

    /* Pass the SAK byte back to caller if requested (e.g., 0x08 = MIFARE Classic 1K) */
    if (sak != NULL) {
        *sak = back_data[0];
    }

    return 0;
}

int mfrc522_authenticate(const struct device *dev,
                         uint8_t auth_mode,
                         uint8_t block_addr,
                         const uint8_t sector_key[6],
                         const uint8_t uid[5])
{
    uint8_t buf[12];
    uint8_t status2 = 0;
    int ret;

    if (sector_key == NULL || uid == NULL) {
        return -EINVAL;
    }

    /* 1. Build the 12-byte authentication frame: [mode, block, key(6), uid(4)] */
    buf[0] = auth_mode;
    buf[1] = block_addr;
    memcpy(&buf[2], sector_key, 6);
    memcpy(&buf[8], uid, 4);

    /* 2. Execute PCD_AUTHENT (no response bytes returned from FIFO) */
    ret = mfrc522_to_card(dev,
                          MFRC522_CMD_MF_AUTHENT,
                          buf, sizeof(buf),
                          NULL, NULL, NULL);
    if (ret < 0) {
        LOG_ERR("AUTH ERROR (to_card failed: %d)", ret);
        return ret;
    }

    /* 3. Verify MFCrypto1On (bit 3 = 0x08) is latched in Status2Reg */
    ret = reg_read(dev, MFRC522_REG_STATUS2, &status2);
    if (ret < 0) {
        return ret;
    }

    if ((status2 & MFRC522_BIT_CRYPTO1_ON) == 0) {
        LOG_ERR("AUTH ERROR (Status2Reg: 0x%02x, Crypto1On not set)", status2);
        return -EACCES;
    }

    return 0;
}

int mfrc522_read_block(const struct device *dev, uint8_t block_addr, uint8_t block_buf[16])
{
    uint8_t cmd[4];
    uint8_t back_data[18] = { 0 };
    size_t back_len = sizeof(back_data);
    uint8_t back_bits = 0;
    int ret;

    if (block_buf == NULL) {
        return -EINVAL;
    }

    /* 1. Build 4-byte READ command: [0x30, block_addr, CRC_L, CRC_H] */
    cmd[0] = PICC_READ;
    cmd[1] = block_addr;

    ret = mfrc522_calculate_crc(dev, cmd, 2, &cmd[2]);
    if (ret < 0) {
        return ret;
    }

    /* 2. Transceive and expect 18 bytes (16 data + 2 CRC = 144 bits / 0x90) */
    ret = mfrc522_to_card(dev,
                          CMDTransceive,
                          cmd, sizeof(cmd),
                          back_data, &back_len,
                          &back_bits);
    if (ret < 0) {
        return ret;
    }

    /* 3. Verify full 144-bit response arrived */
    if (back_bits != 0x90 || back_len < 16) {
        LOG_ERR("Read block %u failed (back_bits: 0x%02x, len: %zu)",
                block_addr, back_bits, back_len);
        return -EPROTO;
    }

    /* 4. Copy the 16 payload bytes (excluding the 2 trailing CRC bytes) */
    memcpy(block_buf, back_data, 16);

    return 0;
}
int read_sector(const struct device *dev,
                        uint8_t trailer_block,
                        const uint8_t key[MFRC522_KEY_LEN],
                        struct mfrc522_tag_data *out)
{
    uint8_t tag_type[2] = {0};
    uint8_t block_buf[MFRC522_BLOCK_SIZE];

    int ret;

    if(out == NULL || key == NULL || !mfrc522_is_valid_trailer(trailer_block)){
        return -EINVAL;
    }

    memset(out,0,sizeof(*out));


    ret = mfrc522_request(dev,PICC_REQIDL,tag_type);
    if (ret < 0){
        return -EAGAIN;
    }

    ret = mfrc522_anticoll(dev,out->uid_raw);
    if (ret < 0){
        return ret;
    }

    out->uid_num = mfrc522_uid_to_num(out->uid_raw);
    if(ret < 0 ){
        return ret;
    }


    ret = mfrc522_anticoll(dev,out->uid_raw);
    if (ret < 0){
        return ret;
    }

    out->uid_num = mfrc522_uid_to_num(out->uid_raw);

    /* 4. Select the RFID tag: SelectTag(uid) */
    ret = mfrc522_select_tag(dev, out->uid_raw,NULL);
    if (ret < 0) {
        return ret;
    }

    ret = mfrc522_authenticate(dev,PICC_AUTHEN1A,trailer_block,key,out->uid_raw);

    if (ret < 0){
        ret = -EACCES;
        goto out_stop_crypto;
    }

    for (int i = 0; i < 3; i++) {
        uint8_t block_addr = (trailer_block - 3) + i;

        ret = mfrc522_read_block(dev, block_addr, block_buf);
        if (ret < 0) {
            goto out_stop_crypto;
        }

       
        memcpy(&out->text[i * MFRC522_BLOCK_SIZE], block_buf, MFRC522_BLOCK_SIZE);
    }

    out->text[MFRC522_SECTOR_DATA_LEN] = '\0';
    ret = 0;

out_stop_crypto:
    mfrc522_stop_crypto1(dev);
    return ret;

}   


static int mfrc522_init(const struct device *dev)
{
    const struct mfrc522_config *cfg = dev->config;
    struct mfrc522_data *data = dev->data;

    int ret;

    if (!gpio_is_ready_dt(&cfg->rst_gpio)){
        LOG_ERR("Reset GPIO device not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&cfg->rst_gpio,GPIO_OUTPUT_INACTIVE);
    if (ret < 0 ){
        return ret;
    }

    k_msleep(10);
    gpio_pin_set_dt(&cfg->rst_gpio,0);
    k_msleep(50);

    if(!spi_is_ready_dt(&cfg->spi)){
        LOG_ERR("SPI bus not ready");
        return -ENODEV;
    }

    ret = reg_read(dev, MFRC522_REG_VERSION, &data->chip_version);
    if (ret < 0) {
        LOG_ERR("Failed to read VersionReg (%d)", ret);
        return ret;
    }

    if (data->chip_version != 0x91 && data->chip_version != 0x92) {
        LOG_WRN("Unexpected MFRC522 VersionReg: 0x%02x", data->chip_version);
    } else {
        LOG_INF("MFRC522 initialized (Version: 0x%02x)", data->chip_version);
    }

    Reset(dev);

    //set timer mode and prescaler
    reg_write(dev,TModeReg,0x0D);
    reg_write(dev,TPrescalerReg,0x3E);
    reg_write(dev,TReloadRegL,30);
    reg_write(dev,TReloadRegH,0);
    LOG_INF("MFRC522 Timer mode and Precaler Done");

    //Enable the auto timer for transmisson and set the mode
    reg_write(dev,TxAutoReg,0x40);
    reg_write(dev,ModeReg,0x3D);
    LOG_INF("Enable the auto timer for transmission");


    ret = AntennaOn(dev);

    if (ret < 0){
        LOG_ERR("Antenna on Failed somethinks is wrong");
        return ret;
    }
    
    LOG_INF("Antenna ON succesfully now read the Rfid Data");

    return 0;
}
static const struct mfrc522_driver_api mfrc522_api = {
    .read = reg_read,
    .write = reg_write,
    .read_sector = read_sector,
};
#define MFRC522_DEFINE(inst)                                                \
	static struct mfrc522_data mfrc522_data_##inst;                     \
	static const struct mfrc522_config mfrc522_config_##inst = {        \
		.spi = SPI_DT_SPEC_INST_GET(inst, MFRC522_SPI_OP),       \
		.rst_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),       \
	};                                                                  \
	DEVICE_DT_INST_DEFINE(inst,                                         \
			      mfrc522_init,                                 \
			      NULL,                                         \
			      &mfrc522_data_##inst,                         \
			      &mfrc522_config_##inst,                       \
			      POST_KERNEL,                                  \
			      CONFIG_MFRC522_INIT_PRIORITY,             \
			      &mfrc522_api);

DT_INST_FOREACH_STATUS_OKAY(MFRC522_DEFINE)