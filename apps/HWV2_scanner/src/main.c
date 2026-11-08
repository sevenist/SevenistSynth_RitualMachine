// Key matrix and encoder scanner for the GRV-mini PCB (U14, CH32V006E8R6). It scans the 5x4 matrix and the 2 encoders, and is an I2C slave on the
// bus shared with the ESP32: a read returns the state below. The board has no interrupt line, so the ESP32 polls.
//
// A read returns kFrameSize bytes:
//   0..2  the inputs, little endian. Bit row * 4 + col = a key (row 0..3 = r1..r4, the 4x4; row 4 = r5, the top row; col 0..3 = c1..c4, left to
//         right), bit 20 / 21 = the push of encoder 1 (SW24) / 2 (SW25). 1 = pressed, debounced.
//   3, 4  encoder 1 / 2: a free running count of quarter steps, 4 a click. The reader takes the difference as an int8_t, so a read that fails loses nothing.
//   5     flags (kFlagResetPin)
//   6     a free running scan count: it changes while the scanner runs
//   7     check: 0xA5 xor bytes 0..6
#include "debug.h"

#define I2C_ADDRESS 0x42                // not the TCA8418's 0x34 (IC1, the footprint for a fallback) nor the ES8388's 0x10

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} Pin;

// By the PCB's pads. A row drives a column through its diode: drive one row high, a pressed key reads high on its column.
static const Pin kRows[5] = {{GPIOC, GPIO_Pin_0}, {GPIOD, GPIO_Pin_3}, {GPIOD, GPIO_Pin_2}, {GPIOC, GPIO_Pin_4}, {GPIOD, GPIO_Pin_4}};
static const Pin kCols[4] = {{GPIOD, GPIO_Pin_5}, {GPIOD, GPIO_Pin_6}, {GPIOD, GPIO_Pin_7}, {GPIOA, GPIO_Pin_0}};
static const Pin kEncoders[2][2] = {{{GPIOA, GPIO_Pin_2}, {GPIOA, GPIO_Pin_3}}, {{GPIOA, GPIO_Pin_4}, {GPIOA, GPIO_Pin_5}}};   // A, B
static const Pin kPushes[2] = {{GPIOB, GPIO_Pin_0}, {GPIOB, GPIO_Pin_1}};

enum {
    kRowCount = 5,
    kColCount = 4,
    kKeyCount = kRowCount * kColCount,
    kInputCount = kKeyCount + 2,
    kSettleUs = 20,                     // a column's pull-down needs a moment after its row moves
    kRowRestUs = 180,                   // 200 us a row: the matrix every 1 ms, the encoders every 200 us
    kDebounceScans = 5,                 // an input changes after this many scans (ms) of disagreeing with its state
    kFrameSize = 8,
    kFlagResetPin = 0x01,               // PD7 (column 3) is still the reset pin: that column can't be read
};

// A transition (previous AB, current AB) -> a quarter step; 0 = no move, or a bounce that skipped a state.
static const int8_t kQuadrature[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};

// Written by the main loop, read by the I2C interrupt: each is a single aligned word or byte.
static volatile uint32_t g_inputs = 0;
static volatile uint8_t g_counts[2] = {0, 0};
static volatile uint8_t g_scans = 0;
static uint8_t g_flags = 0;

static uint8_t g_ages[kInputCount];
static uint8_t g_encoder_ab[2];

static uint8_t g_frame[kFrameSize];
static volatile uint8_t g_sent = 0;
static volatile uint8_t g_sending = 0;

void I2C1_EV_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void I2C1_ER_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

static void build_frame(void) {
    const uint32_t inputs = g_inputs;
    g_frame[0] = (uint8_t)inputs;
    g_frame[1] = (uint8_t)(inputs >> 8);
    g_frame[2] = (uint8_t)(inputs >> 16);
    g_frame[3] = g_counts[0];
    g_frame[4] = g_counts[1];
    g_frame[5] = g_flags;
    g_frame[6] = g_scans;
    uint8_t check = 0xA5;
    for (int i = 0; i < kFrameSize - 1; i++) check ^= g_frame[i];
    g_frame[kFrameSize - 1] = check;
}

void I2C1_EV_IRQHandler(void) {
    const uint16_t status = I2C1->STAR1;
    if (status & I2C_STAR1_ADDR) {
        const uint16_t status2 = I2C1->STAR2;   // reading STAR1 then STAR2 clears ADDR
        g_sending = (status2 & I2C_STAR2_TRA) != 0;
        if (g_sending) {
            build_frame();
            g_sent = 0;
        }
        I2C_ITConfig(I2C1, I2C_IT_BUF, ENABLE);
    }
    if (g_sending && (status & I2C_STAR1_TXE)) {
        I2C1->DATAR = g_sent < kFrameSize ? g_frame[g_sent++] : 0xFF;
    }
    if (status & I2C_STAR1_RXNE) (void)I2C1->DATAR;   // nothing can be written to the scanner: taken and ignored
    if (status & I2C_STAR1_STOPF) I2C1->CTLR1 |= I2C_CTLR1_PE;   // reading STAR1 then writing CTLR1 clears STOPF
}

// The master's NACK ends every read. TXE then stays set until the stop: without this the interrupt would fire until it comes.
void I2C1_ER_IRQHandler(void) {
    const uint16_t status = I2C1->STAR1;
    if (status & I2C_STAR1_AF) {
        g_sending = 0;
        I2C_ITConfig(I2C1, I2C_IT_BUF, DISABLE);
    }
    I2C1->STAR1 = (uint16_t)~(I2C_STAR1_AF | I2C_STAR1_BERR | I2C_STAR1_ARLO);
}

// PD7 is also the chip's reset pin, and this option byte ships with the reset on, which takes the pin from column 3. Turn it off, once: it is kept
// in flash, and the chip restarts to load it. If that fails the scanner runs on and says so (kFlagResetPin).
// Seen on the board: the SDK's call erases the option bytes but its rewrite doesn't take, so all but the read protection word read 0xFF afterwards.
// That gives what is wanted (USER 0xFF: reset off; no protection, no option error), but the other options are not kept as the call's arguments suggest.
static void free_reset_pin(void) {
    const uint16_t user = OB->USER;
    if ((user & OB_RST_NoEN) != OB_RST_NoEN) {
        FLASH_Unlock();
        const FLASH_Status status = FLASH_UserOptionByteConfig(user & OB_IWDG_SW, user & OB_STDBY_NoRST, OB_RST_NoEN, user & OB_PowerON_Start_Mode_BOOT);
        FLASH_Lock();
        if (status == FLASH_COMPLETE && (OB->USER & OB_RST_NoEN) == OB_RST_NoEN) NVIC_SystemReset();
    }
    if ((FLASH_GetUserOptionByte() & OB_RST_NoEN) != OB_RST_NoEN) g_flags |= kFlagResetPin;
}

static void pin_mode(const Pin *pin, GPIOMode_TypeDef mode) {
    GPIO_InitTypeDef init = {0};
    init.GPIO_Pin = pin->pin;
    init.GPIO_Mode = mode;
    init.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(pin->port, &init);
}

static int pin_read(const Pin *pin) {
    return GPIO_ReadInputDataBit(pin->port, pin->pin) == Bit_SET;
}

static void pins_init(void) {
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOA | RCC_PB2Periph_GPIOB | RCC_PB2Periph_GPIOC | RCC_PB2Periph_GPIOD, ENABLE);
    for (int r = 0; r < kRowCount; r++) {
        GPIO_ResetBits(kRows[r].port, kRows[r].pin);
        pin_mode(&kRows[r], GPIO_Mode_Out_PP);
    }
    for (int c = 0; c < kColCount; c++) pin_mode(&kCols[c], GPIO_Mode_IPD);
    // The encoders have 10k pull-downs on the board; the internal ones keep them low on a hand-soldered board that doesn't have those yet.
    for (int e = 0; e < 2; e++) {
        pin_mode(&kEncoders[e][0], GPIO_Mode_IPD);
        pin_mode(&kEncoders[e][1], GPIO_Mode_IPD);
        pin_mode(&kPushes[e], GPIO_Mode_IPD);
    }
}

static void i2c_init(void) {
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_AFIO, ENABLE);
    RCC_PB1PeriphClockCmd(RCC_PB1Periph_I2C1, ENABLE);
    const Pin scl = {GPIOC, GPIO_Pin_2};
    const Pin sda = {GPIOC, GPIO_Pin_1};
    pin_mode(&scl, GPIO_Mode_AF_OD);
    pin_mode(&sda, GPIO_Mode_AF_OD);
    I2C_InitTypeDef init = {0};
    init.I2C_ClockSpeed = 400000;
    init.I2C_Mode = I2C_Mode_I2C;
    init.I2C_DutyCycle = I2C_DutyCycle_2;
    init.I2C_OwnAddress1 = I2C_ADDRESS << 1;
    init.I2C_Ack = I2C_Ack_Enable;
    init.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &init);
    I2C_ITConfig(I2C1, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, ENABLE);
    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);
    I2C_Cmd(I2C1, ENABLE);
    I2C_AcknowledgeConfig(I2C1, ENABLE);   // the ACK bit only holds with the peripheral on
}

static uint8_t encoder_ab(int e) {
    return (uint8_t)(pin_read(&kEncoders[e][0]) << 1 | pin_read(&kEncoders[e][1]));
}

static void read_encoders(void) {
    for (int e = 0; e < 2; e++) {
        const uint8_t ab = encoder_ab(e);
        g_counts[e] += kQuadrature[g_encoder_ab[e] << 2 | ab];
        g_encoder_ab[e] = ab;
    }
}

static void debounce(uint32_t raw) {
    uint32_t inputs = g_inputs;
    for (int i = 0; i < kInputCount; i++) {
        const uint32_t mask = (uint32_t)1 << i;
        if ((raw & mask) == (inputs & mask)) {
            g_ages[i] = 0;
        } else if (++g_ages[i] >= kDebounceScans) {
            inputs ^= mask;
            g_ages[i] = 0;
        }
    }
    g_inputs = inputs;
}

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    free_reset_pin();
    pins_init();
    for (int e = 0; e < 2; e++) g_encoder_ab[e] = encoder_ab(e);
    i2c_init();
    while (1) {
        uint32_t raw = 0;
        for (int r = 0; r < kRowCount; r++) {
            GPIO_SetBits(kRows[r].port, kRows[r].pin);
            Delay_Us(kSettleUs);
            for (int c = 0; c < kColCount; c++) {
                if (pin_read(&kCols[c])) raw |= (uint32_t)1 << (r * kColCount + c);
            }
            GPIO_ResetBits(kRows[r].port, kRows[r].pin);
            read_encoders();
            Delay_Us(kRowRestUs);
        }
        for (int e = 0; e < 2; e++) {
            if (pin_read(&kPushes[e])) raw |= (uint32_t)1 << (kKeyCount + e);
        }
        debounce(raw);
        g_scans++;
    }
}
