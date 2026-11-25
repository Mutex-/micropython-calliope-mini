/*
 * calliopemini.gyroscope: rotation rates from the Calliope mini v1's BMX055.
 *
 * The MIT License (MIT)
 */

#include "microbit/microbitdal.h"
#include "microbit/bmx055.h"

extern "C" {

#include "py/runtime.h"
#include "microbit/modmicrobit.h"

typedef struct _microbit_gyroscope_obj_t {
    mp_obj_base_t base;
} microbit_gyroscope_obj_t;

STATIC void gyroscope_read(int out[3]) {
    if (ubit_gyroscope == NULL) {
        mp_raise_msg(&mp_type_OSError, "no gyroscope");
    }
    if (ubit_gyroscope->read(out) != MICROBIT_OK) {
        mp_raise_msg(&mp_type_OSError, "gyroscope read failed");
    }
}

STATIC mp_obj_t microbit_gyroscope_get_x(mp_obj_t self_in) {
    (void)self_in;
    int v[3];
    gyroscope_read(v);
    return mp_obj_new_int(v[0]);
}
MP_DEFINE_CONST_FUN_OBJ_1(microbit_gyroscope_get_x_obj, microbit_gyroscope_get_x);

STATIC mp_obj_t microbit_gyroscope_get_y(mp_obj_t self_in) {
    (void)self_in;
    int v[3];
    gyroscope_read(v);
    return mp_obj_new_int(v[1]);
}
MP_DEFINE_CONST_FUN_OBJ_1(microbit_gyroscope_get_y_obj, microbit_gyroscope_get_y);

STATIC mp_obj_t microbit_gyroscope_get_z(mp_obj_t self_in) {
    (void)self_in;
    int v[3];
    gyroscope_read(v);
    return mp_obj_new_int(v[2]);
}
MP_DEFINE_CONST_FUN_OBJ_1(microbit_gyroscope_get_z_obj, microbit_gyroscope_get_z);

STATIC mp_obj_t microbit_gyroscope_get_values(mp_obj_t self_in) {
    (void)self_in;
    int v[3];
    gyroscope_read(v);
    mp_obj_t t[3] = { mp_obj_new_int(v[0]), mp_obj_new_int(v[1]), mp_obj_new_int(v[2]) };
    return mp_obj_new_tuple(3, t);
}
MP_DEFINE_CONST_FUN_OBJ_1(microbit_gyroscope_get_values_obj, microbit_gyroscope_get_values);

STATIC const mp_map_elem_t microbit_gyroscope_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_x), (mp_obj_t)&microbit_gyroscope_get_x_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_y), (mp_obj_t)&microbit_gyroscope_get_y_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_z), (mp_obj_t)&microbit_gyroscope_get_z_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_values), (mp_obj_t)&microbit_gyroscope_get_values_obj },
};

STATIC MP_DEFINE_CONST_DICT(microbit_gyroscope_locals_dict, microbit_gyroscope_locals_dict_table);

const mp_obj_type_t microbit_gyroscope_type = {
    { &mp_type_type },
    .name = MP_QSTR_MicroBitGyroscope,
    .print = NULL,
    .make_new = NULL,
    .call = NULL,
    .unary_op = NULL,
    .binary_op = NULL,
    .attr = NULL,
    .subscr = NULL,
    .getiter = NULL,
    .iternext = NULL,
    .buffer_p = {NULL},
    .protocol = NULL,
    .parent = NULL,
    .locals_dict = (mp_obj_dict_t*)&microbit_gyroscope_locals_dict,
};

const microbit_gyroscope_obj_t microbit_gyroscope_obj = {
    {&microbit_gyroscope_type},
};

}
