import esphome.codegen as cg
from esphome.components import time, uart
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MODEL, CONF_TIME_ID
import esphome.final_validate as fv

CODEOWNERS = ["@lcavalli"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

CONF_CLOCK_SYNC_INTERVAL = "clock_sync_interval"
CONF_KYO_ALARM_ID = "kyo_alarm_id"
CONF_ZONE = "zone"
CONF_ZONES = "zones"

kyo_alarm_ns = cg.esphome_ns.namespace("kyo_alarm")
KyoAlarmComponent = kyo_alarm_ns.class_(
    "KyoAlarmComponent", cg.PollingComponent, uart.UARTDevice
)
KyoAlarmListener = kyo_alarm_ns.class_("KyoAlarmListener")
StatusField = kyo_alarm_ns.enum("StatusField", is_class=True)

# Supported models and their number of zones
MODELS = {
    "KYO32G": 32,
}
MAX_ZONES = max(MODELS.values())


def validate_unique_zones(zones):
    seen = set()
    for zone in zones:
        number = zone[CONF_ZONE]
        if number in seen:
            raise cv.Invalid(f"Zone {number} is configured more than once")
        seen.add(number)
    return zones


def zones_schema(entities):
    """A list of zones, each with the optional `entities` (key -> entity schema)."""
    return cv.All(
        cv.ensure_list(
            cv.Schema(
                {
                    cv.Required(CONF_ZONE): cv.int_range(min=1, max=MAX_ZONES),
                    **{cv.Optional(key): schema for key, schema in entities.items()},
                }
            )
        ),
        validate_unique_zones,
    )


def final_validate_zones(config):
    """Check the zone numbers of a platform against the model of its hub."""
    full_config = fv.full_config.get()
    hub_path = full_config.get_path_for_id(config[CONF_KYO_ALARM_ID])[:-1]
    model = full_config.get_config_for_path(hub_path)[CONF_MODEL]
    for index, zone in enumerate(config.get(CONF_ZONES, [])):
        if (number := zone[CONF_ZONE]) > MODELS[model]:
            raise cv.Invalid(
                f"Zone {number} out of range, {model} has {MODELS[model]} zones",
                path=[CONF_ZONES, index, CONF_ZONE],
            )
    return config


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(KyoAlarmComponent),
            cv.Optional(CONF_MODEL, default="KYO32G"): cv.one_of(*MODELS, upper=True),
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_CLOCK_SYNC_INTERVAL, default="24h"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(days=7)),
            ),
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "kyo_alarm",
    baud_rate=9600,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="EVEN",
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    model = config[CONF_MODEL]
    cg.add(var.set_model(model, MODELS[model]))

    if time_id := config.get(CONF_TIME_ID):
        cg.add(var.set_time(await cg.get_variable(time_id)))
        cg.add(var.set_clock_sync_interval(config[CONF_CLOCK_SYNC_INTERVAL]))
