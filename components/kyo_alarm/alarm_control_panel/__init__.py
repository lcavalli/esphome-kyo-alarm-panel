import esphome.codegen as cg
from esphome.components import alarm_control_panel
import esphome.config_validation as cv
from esphome.const import CONF_AWAY

from .. import CONF_KYO_ALARM_ID, KyoAlarmComponent, KyoAlarmListener, kyo_alarm_ns

DEPENDENCIES = ["kyo_alarm"]

CONF_HOME = "home"
CONF_NIGHT = "night"
CONF_PARTITIONS = "partitions"
CONF_REQUIRES_CODE = "requires_code"
CONF_REQUIRES_CODE_TO_ARM = "requires_code_to_arm"
CONF_VACATION = "vacation"

KyoAlarmControlPanel = kyo_alarm_ns.class_(
    "KyoAlarmControlPanel", alarm_control_panel.AlarmControlPanel, KyoAlarmListener
)


def partition_mask(value):
    """A partitions bitmask (bit 0 is partition 1) or a list of partition numbers."""
    if isinstance(value, list):
        mask = 0
        for partition in value:
            mask |= 1 << (cv.int_range(min=1, max=8)(partition) - 1)
        return mask
    return cv.hex_uint8_t(value)


CONFIG_SCHEMA = alarm_control_panel.alarm_control_panel_schema(
    KyoAlarmControlPanel
).extend(
    {
        cv.GenerateID(CONF_KYO_ALARM_ID): cv.use_id(KyoAlarmComponent),
        cv.Optional(CONF_PARTITIONS, default={}): cv.Schema(
            {
                cv.Optional(CONF_AWAY, default=0xFF): partition_mask,
                cv.Optional(CONF_HOME): partition_mask,
                cv.Optional(CONF_NIGHT): partition_mask,
                cv.Optional(CONF_VACATION): partition_mask,
            }
        ),
        cv.Optional(CONF_REQUIRES_CODE, default=True): cv.boolean,
        cv.Optional(CONF_REQUIRES_CODE_TO_ARM, default=True): cv.boolean,
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_KYO_ALARM_ID])
    var = await alarm_control_panel.new_alarm_control_panel(config)
    await cg.register_parented(var, hub)

    partitions = config[CONF_PARTITIONS]
    cg.add(var.set_away_partitions(partitions[CONF_AWAY]))
    if CONF_HOME in partitions:
        cg.add(var.set_home_partitions(partitions[CONF_HOME]))
    if CONF_NIGHT in partitions:
        cg.add(var.set_night_partitions(partitions[CONF_NIGHT]))
    if CONF_VACATION in partitions:
        cg.add(var.set_vacation_partitions(partitions[CONF_VACATION]))
    cg.add(var.set_requires_code(config[CONF_REQUIRES_CODE]))
    cg.add(var.set_requires_code_to_arm(config[CONF_REQUIRES_CODE_TO_ARM]))
    cg.add(hub.register_listener(var))
