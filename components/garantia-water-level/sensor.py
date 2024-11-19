import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ACCURACY_DECIMALS,
    CONF_DEVICE_CLASS,
    CONF_NAME,
    CONF_SENSORS,
    CONF_STATE_CLASS,
    CONF_UNIT_OF_MEASUREMENT,
    DEVICE_CLASS_MOISTURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
)

AUTO_LOAD = [
    "sensor"
]

demo_ns = cg.esphome_ns.namespace("garantia")
WaterLevel = demo_ns.class_("WaterLevel", sensor.Sensor, cg.PollingComponent)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(
            CONF_SENSORS,
            default=[
                {
                    CONF_NAME: "Water level",
                    CONF_UNIT_OF_MEASUREMENT: UNIT_PERCENT,
                    CONF_ACCURACY_DECIMALS: 0,
                    CONF_DEVICE_CLASS: DEVICE_CLASS_MOISTURE,
                    CONF_STATE_CLASS: STATE_CLASS_MEASUREMENT,
                }
            ],
        ): [
            sensor.sensor_schema(WaterLevel, accuracy_decimals=0).extend(
                cv.polling_component_schema("60s")
            )
        ]
    }
)

async def to_code(config):
    for conf in config[CONF_SENSORS]:
        var = await sensor.new_sensor(conf)
        await cg.register_component(var, conf)
