import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID, ENTITY_CATEGORY_CONFIG

from .. import (
    CONF_KYO_ALARM_ID,
    KyoAlarmComponent,
    KyoAlarmListener,
    get_hub_config,
    kyo_alarm_ns,
)

DEPENDENCIES = ["kyo_alarm"]

CONF_RESET_MEMORY = "reset_memory"
CONF_SYNC_CLOCK = "sync_clock"

KyoResetMemoryButton = kyo_alarm_ns.class_(
    "KyoResetMemoryButton", button.Button, KyoAlarmListener
)
KyoSyncClockButton = kyo_alarm_ns.class_(
    "KyoSyncClockButton", button.Button, KyoAlarmListener
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_KYO_ALARM_ID): cv.use_id(KyoAlarmComponent),
        cv.Optional(CONF_RESET_MEMORY): button.button_schema(
            KyoResetMemoryButton, icon="mdi:alarm-light-off"
        ),
        cv.Optional(CONF_SYNC_CLOCK): button.button_schema(
            KyoSyncClockButton,
            icon="mdi:clock-check-outline",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
    }
)


def final_validate(config):
    if CONF_SYNC_CLOCK in config and CONF_TIME_ID not in get_hub_config(config):
        raise cv.Invalid(
            f"'{CONF_SYNC_CLOCK}' requires '{CONF_TIME_ID}' on the kyo_alarm hub",
            path=[CONF_SYNC_CLOCK],
        )
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    hub = await cg.get_variable(config[CONF_KYO_ALARM_ID])
    buttons = button.sub_buttons(config, parent=hub)
    await buttons(CONF_RESET_MEMORY, hub.register_listener)
    await buttons(CONF_SYNC_CLOCK, hub.register_listener)
