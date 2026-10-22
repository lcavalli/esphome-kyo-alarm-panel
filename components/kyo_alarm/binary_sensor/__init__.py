import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_TAMPER,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from .. import (
    CONF_KYO_ALARM_ID,
    CONF_ZONE,
    CONF_ZONES,
    KyoAlarmComponent,
    KyoAlarmListener,
    StatusField,
    final_validate_zones,
    kyo_alarm_ns,
    zones_schema,
)

DEPENDENCIES = ["kyo_alarm"]

CONF_ALARM_MEMORY = "alarm_memory"
CONF_COMMUNICATION = "communication"
CONF_OPEN = "open"
CONF_TAMPER = "tamper"
CONF_TAMPER_MEMORY = "tamper_memory"

KyoStatusBinarySensor = kyo_alarm_ns.class_(
    "KyoStatusBinarySensor", binary_sensor.BinarySensor, KyoAlarmListener
)

# Warning flags: key -> bit
WARNINGS = {
    "mains_failure": 0,
    "bpi_missing": 1,
    "fuse_fault": 2,
    "low_battery": 3,
    "phone_line_fault": 4,
    "default_codes": 5,
    "wireless_fault": 6,
}

# Tamper flags: key -> bit
TAMPERS = {
    "tamper_zone": 2,
    "tamper_false_key": 3,
    "tamper_bpi": 4,
    "tamper_system": 5,
    "tamper_rf_jam": 6,
    "tamper_wireless": 7,
}

# Zone entities: key -> status field
ZONE_FIELDS = {
    CONF_OPEN: StatusField.ZONE_OPEN,
    CONF_TAMPER: StatusField.ZONE_TAMPER,
    CONF_ALARM_MEMORY: StatusField.ZONE_ALARM_MEMORY,
    CONF_TAMPER_MEMORY: StatusField.ZONE_TAMPER_MEMORY,
}


def flag_schema(device_class):
    return binary_sensor.binary_sensor_schema(
        KyoStatusBinarySensor,
        device_class=device_class,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_KYO_ALARM_ID): cv.use_id(KyoAlarmComponent),
        cv.Optional(CONF_COMMUNICATION): flag_schema(DEVICE_CLASS_CONNECTIVITY),
        **{cv.Optional(key): flag_schema(DEVICE_CLASS_PROBLEM) for key in WARNINGS},
        **{cv.Optional(key): flag_schema(DEVICE_CLASS_TAMPER) for key in TAMPERS},
        cv.Optional(CONF_ZONES): zones_schema(
            {
                CONF_OPEN: binary_sensor.binary_sensor_schema(KyoStatusBinarySensor),
                CONF_TAMPER: binary_sensor.binary_sensor_schema(
                    KyoStatusBinarySensor, device_class=DEVICE_CLASS_TAMPER
                ),
                CONF_ALARM_MEMORY: binary_sensor.binary_sensor_schema(
                    KyoStatusBinarySensor, icon="mdi:alarm-light"
                ),
                CONF_TAMPER_MEMORY: binary_sensor.binary_sensor_schema(
                    KyoStatusBinarySensor, device_class=DEVICE_CLASS_TAMPER
                ),
            }
        ),
    }
)

FINAL_VALIDATE_SCHEMA = final_validate_zones


async def to_code(config):
    hub = await cg.get_variable(config[CONF_KYO_ALARM_ID])

    binary_sensors = binary_sensor.sub_binary_sensors(config)
    await binary_sensors(
        CONF_COMMUNICATION, hub.register_listener, StatusField.ONLINE, 0
    )
    for key, bit in WARNINGS.items():
        await binary_sensors(key, hub.register_listener, StatusField.WARNINGS, bit)
    for key, bit in TAMPERS.items():
        await binary_sensors(key, hub.register_listener, StatusField.TAMPERS, bit)

    for zone_config in config.get(CONF_ZONES, []):
        index = zone_config[CONF_ZONE] - 1
        zone_sensors = binary_sensor.sub_binary_sensors(zone_config)
        for key, field in ZONE_FIELDS.items():
            await zone_sensors(key, hub.register_listener, field, index)
