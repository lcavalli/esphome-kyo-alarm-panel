import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG

from .. import (
    CONF_KYO_ALARM_ID,
    CONF_ZONE,
    CONF_ZONES,
    KyoAlarmComponent,
    KyoAlarmListener,
    final_validate_zones,
    kyo_alarm_ns,
    zones_schema,
)

DEPENDENCIES = ["kyo_alarm"]

CONF_BYPASS = "bypass"

KyoZoneBypassSwitch = kyo_alarm_ns.class_(
    "KyoZoneBypassSwitch", switch.Switch, KyoAlarmListener
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_KYO_ALARM_ID): cv.use_id(KyoAlarmComponent),
        cv.Optional(CONF_ZONES): zones_schema(
            {
                CONF_BYPASS: switch.switch_schema(
                    KyoZoneBypassSwitch,
                    icon="mdi:shield-off-outline",
                    entity_category=ENTITY_CATEGORY_CONFIG,
                    default_restore_mode="DISABLED",
                ),
            }
        ),
    }
)

FINAL_VALIDATE_SCHEMA = final_validate_zones


async def to_code(config):
    hub = await cg.get_variable(config[CONF_KYO_ALARM_ID])
    for zone_config in config.get(CONF_ZONES, []):
        switches = switch.sub_switches(zone_config, parent=hub)
        await switches(CONF_BYPASS, hub.register_listener, zone_config[CONF_ZONE] - 1)
