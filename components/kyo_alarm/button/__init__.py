import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import CONF_KYO_ALARM_ID, KyoAlarmComponent, KyoAlarmListener, kyo_alarm_ns

DEPENDENCIES = ["kyo_alarm"]

CONF_RESET_MEMORY = "reset_memory"

KyoResetMemoryButton = kyo_alarm_ns.class_(
    "KyoResetMemoryButton", button.Button, KyoAlarmListener
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_KYO_ALARM_ID): cv.use_id(KyoAlarmComponent),
        cv.Optional(CONF_RESET_MEMORY): button.button_schema(
            KyoResetMemoryButton, icon="mdi:alarm-light-off"
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_KYO_ALARM_ID])
    buttons = button.sub_buttons(config, parent=hub)
    await buttons(CONF_RESET_MEMORY, hub.register_listener)
