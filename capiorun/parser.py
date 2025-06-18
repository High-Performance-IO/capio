
from loguru import logger
import json
from pathlib import Path

from jsonschema import validate, ValidationError, SchemaError

logger.remove()
logger.add(
    sink=lambda msg: print(msg, end=''),  # or use sys.stdout
    format="<green>{time:DD/MM/YYYY HH:mm:ss}</green> | <cyan>{name}</cyan> | <level>{level: <8}</level> | <level>{message}</level>",
    colorize=True
)


def validate_json(json_file_path, schema_file_path):
    try:
        # Load the JSON data
        with open(json_file_path, 'r') as json_file:
            data = json.load(json_file)

        # Load the JSON schema
        with open(schema_file_path, 'r') as schema_file:
            schema = json.load(schema_file)

        # Validate the data against the schema
        validate(instance=data, schema=schema)
        return True

    except ValidationError as ve:
        logger.critical(f"Validation error: {ve.message}")
    except SchemaError as se:
        logger.critical(f"Schema error: {se.message}")
    except json.JSONDecodeError as je:
        logger.critical(f"JSON decode error: {je}")
    except Exception as e:
        logger.critical(f"An unexpected error occurred: {e}")

    return False


def parse_json(workflow):

    logger.info(f"Parsing workflow {workflow}")

    workflow = json.load(open(workflow))
    current_file_path = Path(__file__).resolve()
    schema_file_path = current_file_path.parent.parent / "schema/capio-cl.json"

    if not validate_json(workflow, schema_file_path):
        logger.critical("Invalid JSON")
        #exit(-1)

    return workflow
