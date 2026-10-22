import esphome.codegen as cg
from esphome.components import (
    alarm_control_panel,
    binary_sensor,
    button,
    switch,
    text_sensor,
    time,
    uart,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_AWAY,
    CONF_ID,
    CONF_MODEL,
    CONF_NAME,
    CONF_TIME_ID,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_TAMPER,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

CODEOWNERS = ["@lcavalli"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["alarm_control_panel", "binary_sensor", "button", "switch", "text_sensor"]
MULTI_CONF = True

CONF_ALARM_CONTROL_PANEL = "alarm_control_panel"
CONF_ALARM_MEMORY = "alarm_memory"
CONF_BYPASS = "bypass"
CONF_COMMUNICATION = "communication"
CONF_FIRMWARE_VERSION = "firmware_version"
CONF_HOME = "home"
CONF_NIGHT = "night"
CONF_PANEL_MODEL = "panel_model"
CONF_PARTITIONS = "partitions"
CONF_REQUIRES_CODE = "requires_code"
CONF_REQUIRES_CODE_TO_ARM = "requires_code_to_arm"
CONF_RESET_MEMORY = "reset_memory"
CONF_TAMPER = "tamper"
CONF_TAMPER_MEMORY = "tamper_memory"
CONF_TAMPERS = "tampers"
CONF_VACATION = "vacation"
CONF_WARNINGS = "warnings"
CONF_ZONE = "zone"
CONF_ZONES = "zones"

kyo_alarm_ns = cg.esphome_ns.namespace("kyo_alarm")
KyoAlarmComponent = kyo_alarm_ns.class_(
    "KyoAlarmComponent", cg.PollingComponent, uart.UARTDevice
)
KyoAlarmListener = kyo_alarm_ns.class_("KyoAlarmListener")
KyoStatusBinarySensor = kyo_alarm_ns.class_(
    "KyoStatusBinarySensor", binary_sensor.BinarySensor, KyoAlarmListener
)
KyoZoneBypassSwitch = kyo_alarm_ns.class_(
    "KyoZoneBypassSwitch", switch.Switch, KyoAlarmListener
)
KyoResetMemoryButton = kyo_alarm_ns.class_(
    "KyoResetMemoryButton", button.Button, KyoAlarmListener
)
KyoAlarmControlPanel = kyo_alarm_ns.class_(
    "KyoAlarmControlPanel", alarm_control_panel.AlarmControlPanel, KyoAlarmListener
)
StatusField = kyo_alarm_ns.enum("StatusField", is_class=True)

# Supported models and their number of zones
MODELS = {
    "KYO32G": 32,
}

# Warning flags: key -> (bit, default name)
WARNINGS = {
    "mains_failure": (0, "Mains failure"),
    "bpi_missing": (1, "BPI missing"),
    "fuse_fault": (2, "Fuse fault"),
    "low_battery": (3, "Low battery"),
    "phone_line_fault": (4, "Phone line fault"),
    "default_codes": (5, "Default codes"),
    "wireless_fault": (6, "Wireless fault"),
}

# Tamper flags: key -> (bit, default name)
TAMPERS = {
    "zone": (2, "Zone tamper"),
    "false_key": (3, "False key"),
    "bpi": (4, "BPI tamper"),
    "system": (5, "System tamper"),
    "rf_jam": (6, "RF jamming"),
    "wireless": (7, "Wireless tamper"),
}


def expand_entities(default_names, name_prefix=False):
    """Allow `key: true` as a shorthand for an entity with a default name.

    With name_prefix, default names are prefixed with the name of the enclosing entity (the zone).
    """

    def validator(config):
        if not isinstance(config, dict):
            return config
        config = config.copy()
        prefix = ""
        if name_prefix and config.get(CONF_NAME):
            prefix = f"{config[CONF_NAME]} "
        for key, default_name in default_names.items():
            value = config.get(key)
            if value is None:
                continue
            if value is False:
                del config[key]
                continue
            if value is True:
                value = {}
            if isinstance(value, dict) and CONF_NAME not in value:
                value = {**value, CONF_NAME: prefix + default_name}
            config[key] = value
        return config

    return validator


def partition_mask(value):
    """A partitions bitmask (bit 0 is partition 1) or a list of partition numbers."""
    if isinstance(value, list):
        mask = 0
        for partition in value:
            mask |= 1 << (cv.int_range(min=1, max=8)(partition) - 1)
        return mask
    return cv.hex_uint8_t(value)


def validate_zone_name(config):
    if not config.get(CONF_NAME):
        raise cv.Invalid("Zones require a name")
    return config


ZONE_ENTITIES = {
    CONF_ALARM_MEMORY: "alarm memory",
    CONF_TAMPER: "tamper",
    CONF_TAMPER_MEMORY: "tamper memory",
    CONF_BYPASS: "bypass",
}

ZONE_SCHEMA = cv.All(
    validate_zone_name,
    expand_entities(ZONE_ENTITIES, name_prefix=True),
    binary_sensor.binary_sensor_schema(KyoStatusBinarySensor).extend(
        {
            cv.Required(CONF_ZONE): cv.int_range(min=1, max=32),
            cv.Optional(CONF_ALARM_MEMORY): binary_sensor.binary_sensor_schema(
                KyoStatusBinarySensor, icon="mdi:alarm-light"
            ),
            cv.Optional(CONF_TAMPER): binary_sensor.binary_sensor_schema(
                KyoStatusBinarySensor, device_class=DEVICE_CLASS_TAMPER
            ),
            cv.Optional(CONF_TAMPER_MEMORY): binary_sensor.binary_sensor_schema(
                KyoStatusBinarySensor, device_class=DEVICE_CLASS_TAMPER
            ),
            cv.Optional(CONF_BYPASS): switch.switch_schema(
                KyoZoneBypassSwitch,
                icon="mdi:shield-off-outline",
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="DISABLED",
            ),
        }
    ),
)

ALARM_CONTROL_PANEL_SCHEMA = alarm_control_panel.alarm_control_panel_schema(
    KyoAlarmControlPanel
).extend(
    {
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


def flags_schema(flags, device_class):
    return cv.All(
        expand_entities({key: name for key, (_, name) in flags.items()}),
        cv.Schema(
            {
                cv.Optional(key): binary_sensor.binary_sensor_schema(
                    KyoStatusBinarySensor,
                    device_class=device_class,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                )
                for key in flags
            }
        ),
    )


def validate_zones(config):
    max_zones = MODELS[config[CONF_MODEL]]
    seen = set()
    for zone in config[CONF_ZONES]:
        number = zone[CONF_ZONE]
        if number > max_zones:
            raise cv.Invalid(
                f"Zone {number} out of range, {config[CONF_MODEL]} has {max_zones} zones"
            )
        if number in seen:
            raise cv.Invalid(f"Zone {number} is configured more than once")
        seen.add(number)
    return config


CONFIG_SCHEMA = cv.All(
    expand_entities(
        {
            CONF_ALARM_CONTROL_PANEL: "Alarm",
            CONF_PANEL_MODEL: "Panel model",
            CONF_FIRMWARE_VERSION: "Firmware version",
            CONF_COMMUNICATION: "Communication",
            CONF_RESET_MEMORY: "Reset alarm memory",
        }
    ),
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(KyoAlarmComponent),
            cv.Optional(CONF_MODEL, default="KYO32G"): cv.one_of(*MODELS, upper=True),
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_ALARM_CONTROL_PANEL): ALARM_CONTROL_PANEL_SCHEMA,
            cv.Optional(CONF_PANEL_MODEL): text_sensor.text_sensor_schema(
                icon="mdi:shield-home", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_FIRMWARE_VERSION): text_sensor.text_sensor_schema(
                icon="mdi:memory", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_COMMUNICATION): binary_sensor.binary_sensor_schema(
                KyoStatusBinarySensor,
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_RESET_MEMORY): button.button_schema(
                KyoResetMemoryButton, icon="mdi:alarm-light-off"
            ),
            cv.Optional(CONF_WARNINGS, default={}): flags_schema(
                WARNINGS, DEVICE_CLASS_PROBLEM
            ),
            cv.Optional(CONF_TAMPERS, default={}): flags_schema(
                TAMPERS, DEVICE_CLASS_TAMPER
            ),
            cv.Optional(CONF_ZONES, default=[]): cv.ensure_list(ZONE_SCHEMA),
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(uart.UART_DEVICE_SCHEMA),
    validate_zones,
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "kyo_alarm",
    baud_rate=9600,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="EVEN",
)


async def register_status_sensor(hub, config, field, bit):
    sens = await binary_sensor.new_binary_sensor(config, field, bit)
    cg.add(hub.register_listener(sens))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    model = config[CONF_MODEL]
    cg.add(var.set_model(model, MODELS[model]))

    if time_id := config.get(CONF_TIME_ID):
        cg.add(var.set_time(await cg.get_variable(time_id)))

    if acp_config := config.get(CONF_ALARM_CONTROL_PANEL):
        acp = await alarm_control_panel.new_alarm_control_panel(acp_config)
        cg.add(acp.set_parent(var))
        partitions = acp_config[CONF_PARTITIONS]
        cg.add(acp.set_away_partitions(partitions[CONF_AWAY]))
        if CONF_HOME in partitions:
            cg.add(acp.set_home_partitions(partitions[CONF_HOME]))
        if CONF_NIGHT in partitions:
            cg.add(acp.set_night_partitions(partitions[CONF_NIGHT]))
        if CONF_VACATION in partitions:
            cg.add(acp.set_vacation_partitions(partitions[CONF_VACATION]))
        cg.add(acp.set_requires_code(acp_config[CONF_REQUIRES_CODE]))
        cg.add(acp.set_requires_code_to_arm(acp_config[CONF_REQUIRES_CODE_TO_ARM]))
        cg.add(var.register_listener(acp))

    if panel_model_config := config.get(CONF_PANEL_MODEL):
        sens = await text_sensor.new_text_sensor(panel_model_config)
        cg.add(var.set_panel_model_text_sensor(sens))
    if firmware_config := config.get(CONF_FIRMWARE_VERSION):
        sens = await text_sensor.new_text_sensor(firmware_config)
        cg.add(var.set_firmware_version_text_sensor(sens))
    if communication_config := config.get(CONF_COMMUNICATION):
        await register_status_sensor(var, communication_config, StatusField.ONLINE, 0)
    if reset_config := config.get(CONF_RESET_MEMORY):
        btn = await button.new_button(reset_config)
        cg.add(btn.set_parent(var))
        cg.add(var.register_listener(btn))

    for key, (bit, _) in WARNINGS.items():
        if flag_config := config[CONF_WARNINGS].get(key):
            await register_status_sensor(var, flag_config, StatusField.WARNINGS, bit)
    for key, (bit, _) in TAMPERS.items():
        if flag_config := config[CONF_TAMPERS].get(key):
            await register_status_sensor(var, flag_config, StatusField.TAMPERS, bit)

    for zone_config in config[CONF_ZONES]:
        index = zone_config[CONF_ZONE] - 1
        await register_status_sensor(var, zone_config, StatusField.ZONE_OPEN, index)
        if alarm_memory_config := zone_config.get(CONF_ALARM_MEMORY):
            await register_status_sensor(
                var, alarm_memory_config, StatusField.ZONE_ALARM_MEMORY, index
            )
        if tamper_config := zone_config.get(CONF_TAMPER):
            await register_status_sensor(
                var, tamper_config, StatusField.ZONE_TAMPER, index
            )
        if tamper_memory_config := zone_config.get(CONF_TAMPER_MEMORY):
            await register_status_sensor(
                var, tamper_memory_config, StatusField.ZONE_TAMPER_MEMORY, index
            )
        if bypass_config := zone_config.get(CONF_BYPASS):
            sw = await switch.new_switch(bypass_config, index)
            cg.add(sw.set_parent(var))
            cg.add(var.register_listener(sw))
