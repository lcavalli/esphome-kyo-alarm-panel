import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_KYO_ALARM_ID, KyoAlarmComponent, enable_event_log

DEPENDENCIES = ["kyo_alarm"]

CONF_FIRMWARE_VERSION = "firmware_version"
CONF_LAST_EVENT = "last_event"
CONF_PANEL_MODEL = "panel_model"
CONF_PARTITION_STATUS = "partition_status"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_KYO_ALARM_ID): cv.use_id(KyoAlarmComponent),
        cv.Optional(CONF_PANEL_MODEL): text_sensor.text_sensor_schema(
            icon="mdi:shield-home", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_FIRMWARE_VERSION): text_sensor.text_sensor_schema(
            icon="mdi:memory", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_PARTITION_STATUS): text_sensor.text_sensor_schema(
            icon="mdi:shield-lock-outline", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_LAST_EVENT): text_sensor.text_sensor_schema(
            icon="mdi:history"
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_KYO_ALARM_ID])
    text_sensors = text_sensor.sub_text_sensors(config)
    await text_sensors(CONF_PANEL_MODEL, hub.set_panel_model_text_sensor)
    await text_sensors(CONF_FIRMWARE_VERSION, hub.set_firmware_version_text_sensor)
    await text_sensors(CONF_PARTITION_STATUS, hub.set_partition_status_text_sensor)
    if CONF_LAST_EVENT in config:
        enable_event_log()
        await text_sensors(CONF_LAST_EVENT, hub.set_last_event_text_sensor)
