/*
 * Bosch BMX055 driver for the Calliope mini v1.
 *
 * The BMX055 packs a BMA2x2 accelerometer, a BMG160 gyroscope and a BMM150
 * magnetometer. The microbit-dal only knows micro:bit sensors, so without this
 * driver accelerometer and compass calls end in panic 50/51.
 *
 * The MIT License (MIT)
 */
#ifndef MICROPY_INCLUDED_MICROBIT_BMX055_H
#define MICROPY_INCLUDED_MICROBIT_BMX055_H

#include "MicroBitAccelerometer.h"
#include "MicroBitCompass.h"
#include "MicroBitI2C.h"

// 8-bit I2C addresses, as MicroBitI2C expects (7-bit 0x18 and 0x10).
#define BMX055_ACC_ADDR             0x30
#define BMX055_MAG_ADDR             0x20

class BMX055Accelerometer : public MicroBitAccelerometer {
    MicroBitI2C &i2c;
    uint64_t lastRead;

public:
    BMX055Accelerometer(MicroBitI2C &i2c, CoordinateSpace &coordinateSpace);
    static int isDetected(MicroBitI2C &i2c);
    virtual int configure();
    virtual int requestUpdate();
    virtual void idleTick();
};

class BMX055Magnetometer : public MicroBitCompass {
    MicroBitI2C &i2c;
    uint64_t lastRead;

    // Factory trim values of the BMM150, used for temperature/offset compensation.
    int8_t dig_x1, dig_y1, dig_x2, dig_y2, dig_xy2;
    uint8_t dig_xy1;
    int16_t dig_z2, dig_z3, dig_z4;
    uint16_t dig_z1, dig_xyz1;

    int readTrim();

public:
    BMX055Magnetometer(MicroBitI2C &i2c, MicroBitAccelerometer &accel, CoordinateSpace &coordinateSpace);
    static int isDetected(MicroBitI2C &i2c);
    virtual int configure();
    virtual int requestUpdate();
    virtual void idleTick();
};

#endif // MICROPY_INCLUDED_MICROBIT_BMX055_H
