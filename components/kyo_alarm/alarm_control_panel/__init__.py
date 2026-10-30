import logging

import esphome.codegen as cg
from esphome.components import alarm_control_panel
import esphome.config_validation as cv
from esphome.const import CONF_AWAY

from .. import CONF_KYO_ALARM_ID, KyoAlarmComponent, KyoAlarmListener, kyo_alarm_ns

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["kyo_alarm"]

CONF_HOME = "home"
CONF_NIGHT = "night"
CONF_PARTIAL = "partial"
CONF_PARTIAL_NO_DELAY = "partial_no_delay"
CONF_PARTITIONS = "partitions"
CONF_REQUIRES_CODE = "requires_code"
CONF_REQUIRES_CODE_TO_ARM = "requires_code_to_arm"
CONF_TOTAL = "total"
CONF_VACATION = "vacation"

# In the order of the partition control register, and of the setters' arguments
ARMING_TYPES = (CONF_TOTAL, CONF_PARTIAL, CONF_PARTIAL_NO_DELAY)
# In the order the panel status is matched against
MODES = (CONF_AWAY, CONF_HOME, CONF_NIGHT, CONF_VACATION)

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


ARMING_TYPES_SCHEMA = cv.All(
    cv.Schema(
        {cv.Optional(arming_type): partition_mask for arming_type in ARMING_TYPES}
    ),
    cv.has_at_least_one_key(*ARMING_TYPES),
)


def arming_mode(value):
    """Partitions armed by a mode: partitions armed total, or a map of partitions by arming type."""
    if isinstance(value, dict):
        masks = ARMING_TYPES_SCHEMA(value)
    else:
        masks = {CONF_TOTAL: partition_mask(value)}
    config = {arming_type: masks.get(arming_type, 0) for arming_type in ARMING_TYPES}
    seen = 0
    for arming_type in ARMING_TYPES:
        overlap = config[arming_type] & seen
        if overlap:
            numbers = ", ".join(str(i + 1) for i in range(8) if overlap & (1 << i))
            raise cv.Invalid(
                f"Partitions {numbers} are armed with more than one arming type",
                path=[arming_type],
            )
        seen |= config[arming_type]
    return config


def validate_modes(config):
    """Warn about modes the panel status cannot tell apart."""
    seen = {}
    for mode in MODES:
        if mode not in config:
            continue
        masks = tuple(config[mode][arming_type] for arming_type in ARMING_TYPES)
        if not any(masks):
            continue
        if masks in seen:
            _LOGGER.warning(
                "'%s' arms the same partitions as '%s' with the same arming types, it will be reported as '%s'",
                mode,
                seen[masks],
                seen[masks],
            )
        else:
            seen[masks] = mode
    return config


CONFIG_SCHEMA = alarm_control_panel.alarm_control_panel_schema(
    KyoAlarmControlPanel
).extend(
    {
        cv.GenerateID(CONF_KYO_ALARM_ID): cv.use_id(KyoAlarmComponent),
        cv.Optional(CONF_PARTITIONS, default={}): cv.All(
            cv.Schema(
                {
                    cv.Optional(CONF_AWAY, default=0xFF): arming_mode,
                    cv.Optional(CONF_HOME): arming_mode,
                    cv.Optional(CONF_NIGHT): arming_mode,
                    cv.Optional(CONF_VACATION): arming_mode,
                }
            ),
            validate_modes,
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
    for mode in MODES:
        if mode in partitions:
            masks = [partitions[mode][arming_type] for arming_type in ARMING_TYPES]
            cg.add(getattr(var, f"set_{mode}_partitions")(*masks))
    cg.add(var.set_requires_code(config[CONF_REQUIRES_CODE]))
    cg.add(var.set_requires_code_to_arm(config[CONF_REQUIRES_CODE_TO_ARM]))
    cg.add(hub.register_listener(var))
