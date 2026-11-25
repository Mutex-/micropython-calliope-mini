/*
 * Bosch BMX055 driver for the Calliope mini v1 (accelerometer + magnetometer).
 *
 * The MIT License (MIT)
 */

#include "mbed.h"
#include "MicroBitConfig.h"
#include "MicroBitFiber.h"
#include "MicroBitSystemTimer.h"
#include "microbit/bmx055.h"

// --- Board orientation -------------------------------------------------------
//
// Maps the sensor's raw axes onto the board frame that MicroPython reports
// (same convention as the micro:bit):
//   x < 0 when the A-button side points down     ('left')
//   y > 0 when the top (USB) edge points up      ('up')
//   z < 0 when the board lies face up            ('face up')
// Each row is {raw axis index, sign}. Both tables were measured on a Calliope
// mini v1 (gesture poses for the accelerometer, a reference compass for the
// magnetometer).
//
// Accelerometer: raw +x points to the bottom edge, raw +y to the B-button side,
// raw +z out of the display.
static const int8_t BMX055_ACC_AXIS_MAP[3][2] = {
    {1, -1},
    {0, -1},
    {2, -1},
};

// Magnetometer: inside the BMX055 its x/y axes are turned 90 degrees against the
// accelerometer's, so it needs its own table. Sharing the accelerometer's put
// compass.heading() about 90 degrees off.
static const int8_t BMX055_MAG_AXIS_MAP[3][2] = {
    {0, -1},
    {1, 1},
    {2, -1},
};

// Convert raw axes into the ENU form the DAL stores. The DAL's
// CoordinateSpace(SIMPLE_CARTESIAN, upsidedown=true, ROTATED_0) then turns it
// back into exactly the board frame above.
static void board_to_enu(const int8_t map[3][2], const int raw[3], Sample3D &enu) {
    int f[3];
    for (int i = 0; i < 3; ++i) {
        f[i] = map[i][1] * raw[map[i][0]];
    }
    enu.x = f[1];
    enu.y = -f[0];
    enu.z = f[2];
}

// --- BMA2x2 accelerometer ----------------------------------------------------

#define BMA_CHIPID          0x00
#define BMA_CHIPID_VAL      0xFA
#define BMA_ACCD_X_LSB      0x02
#define BMA_PMU_RANGE       0x0F
#define BMA_PMU_BW          0x10
#define BMA_PMU_LPW         0x11
#define BMA_BGW_SOFTRESET   0x14
#define BMA_SOFTRESET_CMD   0xB6
#define BMA_BW_62HZ5        0x0B

BMX055Accelerometer::BMX055Accelerometer(MicroBitI2C &_i2c, CoordinateSpace &coordinateSpace)
    : MicroBitAccelerometer(coordinateSpace, MICROBIT_ID_ACCELEROMETER), i2c(_i2c), lastRead(0) {
    status = 0;
    i2c.writeRegister(BMX055_ACC_ADDR, BMA_BGW_SOFTRESET, BMA_SOFTRESET_CMD);
    wait_ms(3);
    configure();
}

int BMX055Accelerometer::isDetected(MicroBitI2C &i2c) {
    return i2c.readRegister(BMX055_ACC_ADDR, BMA_CHIPID) == BMA_CHIPID_VAL;
}

int BMX055Accelerometer::configure() {
    uint8_t range_bits;
    if (sampleRange <= 2) {
        sampleRange = 2; range_bits = 0x03;
    } else if (sampleRange <= 4) {
        sampleRange = 4; range_bits = 0x05;
    } else if (sampleRange <= 8) {
        sampleRange = 8; range_bits = 0x08;
    } else {
        sampleRange = 16; range_bits = 0x0C;
    }
    if (i2c.writeRegister(BMX055_ACC_ADDR, BMA_PMU_LPW, 0x00) != 0      // normal power mode
        || i2c.writeRegister(BMX055_ACC_ADDR, BMA_PMU_RANGE, range_bits) != 0
        || i2c.writeRegister(BMX055_ACC_ADDR, BMA_PMU_BW, BMA_BW_62HZ5) != 0) {
        return MICROBIT_I2C_ERROR;
    }
    return MICROBIT_OK;
}

int BMX055Accelerometer::requestUpdate() {
    if (!(status & MICROBIT_ACCEL_ADDED_TO_IDLE)) {
        fiber_add_idle_component(this);
        status |= MICROBIT_ACCEL_ADDED_TO_IDLE;
    }

    uint64_t now = system_timer_current_time();
    if (lastRead != 0 && now - lastRead < samplePeriod) {
        return MICROBIT_OK;
    }

    uint8_t d[6];
    if (i2c.readRegister(BMX055_ACC_ADDR, BMA_ACCD_X_LSB, d, 6) != 0) {
        return MICROBIT_I2C_ERROR;
    }
    lastRead = now;

    // 12-bit two's complement, left-aligned in LSB/MSB pairs. At +-2g that is
    // 1024 counts per g; scale so every range reports ~1024 per g, like the micro:bit.
    int raw[3];
    for (int i = 0; i < 3; ++i) {
        int16_t v = (int16_t)((d[2 * i + 1] << 8) | (d[2 * i] & 0xF0));
        raw[i] = (v >> 4) * sampleRange / 2;
    }
    board_to_enu(BMX055_ACC_AXIS_MAP, raw, sampleENU);
    update();
    return MICROBIT_OK;
}

void BMX055Accelerometer::idleTick() {
    requestUpdate();
}

// --- BMM150 magnetometer -----------------------------------------------------

#define BMM_CHIPID          0x40
#define BMM_CHIPID_VAL      0x32
#define BMM_DATA_X_LSB      0x42
#define BMM_POWER_CTRL      0x4B
#define BMM_OP_MODE         0x4C
#define BMM_REP_XY          0x51
#define BMM_REP_Z           0x52
#define BMM_TRIM_START      0x5D
#define BMM_TRIM_LEN        21
#define BMM_ODR_20HZ_NORMAL (0x05 << 3)

BMX055Magnetometer::BMX055Magnetometer(MicroBitI2C &_i2c, MicroBitAccelerometer &accel, CoordinateSpace &coordinateSpace)
    : MicroBitCompass(accel, coordinateSpace, MICROBIT_ID_COMPASS), i2c(_i2c), lastRead(0) {
    dig_xyz1 = 0;
    configure();
}

int BMX055Magnetometer::isDetected(MicroBitI2C &i2c) {
    // The chip ID only reads back once the part is out of suspend mode.
    i2c.writeRegister(BMX055_MAG_ADDR, BMM_POWER_CTRL, 0x01);
    wait_ms(3);
    return i2c.readRegister(BMX055_MAG_ADDR, BMM_CHIPID) == BMM_CHIPID_VAL;
}

int BMX055Magnetometer::readTrim() {
    uint8_t b[BMM_TRIM_LEN];
    if (i2c.readRegister(BMX055_MAG_ADDR, BMM_TRIM_START, b, BMM_TRIM_LEN) != 0) {
        return MICROBIT_I2C_ERROR;
    }
    // Offsets are register - 0x5D.
    dig_x1 = (int8_t)b[0];                                   // 0x5D
    dig_y1 = (int8_t)b[1];                                   // 0x5E
    dig_z4 = (int16_t)(b[5] | (b[6] << 8));                  // 0x62/0x63
    dig_x2 = (int8_t)b[7];                                   // 0x64
    dig_y2 = (int8_t)b[8];                                   // 0x65
    dig_z2 = (int16_t)(b[11] | (b[12] << 8));                // 0x68/0x69
    dig_z1 = (uint16_t)(b[13] | (b[14] << 8));               // 0x6A/0x6B
    dig_xyz1 = (uint16_t)(b[15] | ((b[16] & 0x7F) << 8));    // 0x6C/0x6D
    dig_z3 = (int16_t)(b[17] | (b[18] << 8));                // 0x6E/0x6F
    dig_xy2 = (int8_t)b[19];                                 // 0x70
    dig_xy1 = b[20];                                         // 0x71
    return MICROBIT_OK;
}

int BMX055Magnetometer::configure() {
    if (i2c.writeRegister(BMX055_MAG_ADDR, BMM_POWER_CTRL, 0x01) != 0) {
        return MICROBIT_I2C_ERROR;
    }
    wait_ms(3);
    if (readTrim() != MICROBIT_OK
        || i2c.writeRegister(BMX055_MAG_ADDR, BMM_REP_XY, 0x04) != 0    // regular preset: 9 reps
        || i2c.writeRegister(BMX055_MAG_ADDR, BMM_REP_Z, 0x0E) != 0     // regular preset: 15 reps
        || i2c.writeRegister(BMX055_MAG_ADDR, BMM_OP_MODE, BMM_ODR_20HZ_NORMAL) != 0) {
        return MICROBIT_I2C_ERROR;
    }
    return MICROBIT_OK;
}

int BMX055Magnetometer::requestUpdate() {
    if (!(status & MICROBIT_COMPASS_STATUS_ADDED_TO_IDLE)) {
        fiber_add_idle_component(this);
        status |= MICROBIT_COMPASS_STATUS_ADDED_TO_IDLE;
    }

    uint64_t now = system_timer_current_time();
    uint64_t period = samplePeriod < 50 ? 50 : samplePeriod;   // sensor runs at 20 Hz
    if (lastRead != 0 && now - lastRead < period) {
        return MICROBIT_OK;
    }

    uint8_t d[8];
    if (i2c.readRegister(BMX055_MAG_ADDR, BMM_DATA_X_LSB, d, 8) != 0) {
        return MICROBIT_I2C_ERROR;
    }
    lastRead = now;

    int16_t rx = (int16_t)((d[1] << 8) | d[0]) >> 3;            // 13 bit
    int16_t ry = (int16_t)((d[3] << 8) | d[2]) >> 3;            // 13 bit
    int16_t rz = (int16_t)((d[5] << 8) | d[4]) >> 1;            // 15 bit
    uint16_t rhall = (uint16_t)((d[7] << 8) | d[6]) >> 2;       // 14 bit

    if (rx == -4096 || ry == -4096 || rz == -16384 || rhall == 0
        || dig_xyz1 == 0 || dig_z1 == 0 || dig_z2 == 0) {
        return MICROBIT_OK;  // overflow or no data yet: keep the previous sample
    }

    // Bosch BMM150 floating-point compensation, result in micro-tesla.
    float h = (float)dig_xyz1 * 16384.0f / rhall - 16384.0f;
    float xy_common = (float)dig_xy2 * (h * h / 268435456.0f) + h * (float)dig_xy1 / 16384.0f + 256.0f;
    float ux = ((rx * xy_common * ((float)dig_x2 + 160.0f)) / 8192.0f + (float)dig_x1 * 8.0f) / 16.0f;
    float uy = ((ry * xy_common * ((float)dig_y2 + 160.0f)) / 8192.0f + (float)dig_y1 * 8.0f) / 16.0f;
    float z0 = (float)rz - (float)dig_z4;
    float z1 = (float)rhall - (float)dig_xyz1;
    float z4 = (float)dig_z2 + (float)dig_z1 * (float)rhall / 32768.0f;
    float uz = ((z0 * 131072.0f - (float)dig_z3 * z1) / (z4 * 4.0f)) / 16.0f;

    // The DAL works in nano-tesla.
    int raw[3] = { (int)(ux * 1000.0f), (int)(uy * 1000.0f), (int)(uz * 1000.0f) };
    board_to_enu(BMX055_MAG_AXIS_MAP, raw, sampleENU);
    update();
    return MICROBIT_OK;
}

void BMX055Magnetometer::idleTick() {
    requestUpdate();
}

// --- BMG160 gyroscope --------------------------------------------------------

#define BMG_CHIPID          0x00
#define BMG_CHIPID_VAL      0x0F
#define BMG_RATE_X_LSB      0x02
#define BMG_RANGE           0x0F
#define BMG_BW              0x10
#define BMG_LPM1            0x11
#define BMG_SOFTRESET       0x14
#define BMG_SOFTRESET_CMD   0xB6
#define BMG_RANGE_2000DPS   0x00
#define BMG_BW_100HZ        0x07   // 100 Hz output rate, 32 Hz filter
#define BMG_FULL_SCALE_DPS  2000

BMX055Gyroscope::BMX055Gyroscope(MicroBitI2C &_i2c) : i2c(_i2c) {
    i2c.writeRegister(BMX055_GYRO_ADDR, BMG_SOFTRESET, BMG_SOFTRESET_CMD);
    wait_ms(30);
    configure();
}

int BMX055Gyroscope::isDetected(MicroBitI2C &i2c) {
    return i2c.readRegister(BMX055_GYRO_ADDR, BMG_CHIPID) == BMG_CHIPID_VAL;
}

int BMX055Gyroscope::configure() {
    if (i2c.writeRegister(BMX055_GYRO_ADDR, BMG_LPM1, 0x00) != 0           // normal power mode
        || i2c.writeRegister(BMX055_GYRO_ADDR, BMG_RANGE, BMG_RANGE_2000DPS) != 0
        || i2c.writeRegister(BMX055_GYRO_ADDR, BMG_BW, BMG_BW_100HZ) != 0) {
        return MICROBIT_I2C_ERROR;
    }
    return MICROBIT_OK;
}

int BMX055Gyroscope::read(int out[3]) {
    uint8_t d[6];
    if (i2c.readRegister(BMX055_GYRO_ADDR, BMG_RATE_X_LSB, d, 6) != 0) {
        return MICROBIT_I2C_ERROR;
    }
    int raw[3];
    for (int i = 0; i < 3; ++i) {
        int16_t v = (int16_t)((d[2 * i + 1] << 8) | d[2 * i]);
        raw[i] = (int)v * BMG_FULL_SCALE_DPS / 32768;
    }
    // The gyroscope shares the accelerometer's axes, so rates are reported around
    // the same board axes that accelerometer.get_x/y/z() use.
    for (int i = 0; i < 3; ++i) {
        out[i] = BMX055_ACC_AXIS_MAP[i][1] * raw[BMX055_ACC_AXIS_MAP[i][0]];
    }
    return MICROBIT_OK;
}
