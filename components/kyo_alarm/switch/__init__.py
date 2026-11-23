import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG

from .. import (
    CONF_KYO_ALARM_ID,
    CONF_PASSTHROUGH,
    CONF_ZONE,
    CONF_ZONES,
    KyoAlarmComponent,
    KyoAlarmListener,
    final_validate_zones,
    get_hub_config,
    kyo_alarm_ns,
    zones_schema,
)

DEPENDENCIES = ["kyo_alarm"]

CONF_BYPASS = "bypass"

KyoZoneBypassSwitch = kyo_alarm_ns.class_(
    "KyoZoneBypassSwitch", switch.Switch, KyoAlarmListener
)
KyoPassthroughSwitch = kyo_alarm_ns.class_(
    "KyoPassthroughSwitch", switch.Switch, KyoAlarmListener
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_KYO_ALARM_ID): cv.use_id(KyoAlarmComponent),
        # The state comes from the hub, the passthrough is always off at boot
        cv.Optional(CONF_PASSTHROUGH): switch.switch_schema(
            KyoPassthroughSwitch,
            icon="mdi:swap-horizontal",
            entity_category=ENTITY_CATEGORY_CONFIG,
            default_restore_mode="DISABLED",
        ),
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


def final_validate(config):
    if CONF_PASSTHROUGH in config and CONF_PASSTHROUGH not in get_hub_config(config):
        raise cv.Invalid(
            f"'{CONF_PASSTHROUGH}' requires '{CONF_PASSTHROUGH}' on the kyo_alarm hub",
            path=[CONF_PASSTHROUGH],
        )
    return final_validate_zones(config)


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    hub = await cg.get_variable(config[CONF_KYO_ALARM_ID])
    await switch.sub_switches(config, parent=hub)(
        CONF_PASSTHROUGH, hub.register_listener
    )
    for zone_config in config.get(CONF_ZONES, []):
        switches = switch.sub_switches(zone_config, parent=hub)
        await switches(CONF_BYPASS, hub.register_listener, zone_config[CONF_ZONE] - 1)
