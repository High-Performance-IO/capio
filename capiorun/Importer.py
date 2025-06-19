from loguru import logger

logger.remove()
logger.add(
    sink=lambda msg: print(msg, end=''),  # or use sys.stdout
    format="<green>{time:DD/MM/YYYY HH:mm:ss}</green> | <cyan>{name}</cyan> | <level>{level: <8}</level> | <level>{message}</level>",
    colorize=True
)


class Importer:
    def __init__(self, steps, capio_dir, workflow_name):
        self.steps = steps
        self.capio_dir = capio_dir
        self.workflow_name = workflow_name
        logger.info(f"Loaded {len(self.steps)} steps for the workflow {self.workflow_name}")

    def import_steps(self):
        jobs_to_submit = {}
        for name, step_options in self.steps.items():
            logger.info(f"Generating step executor for step {name}")

            location = step_options["location"]

            if location not in jobs_to_submit:
                jobs_to_submit[location] = []

            jobs_to_submit[location].append({
                "env": f"CAPIO_DIR={self.capio_dir} \\\n"
                       f"CAPIO_APP_NAME={name} \\\n"
                       f"CAPIO_WORKFLOW_NAME={self.workflow_name} ",
                "runnable": f"{step_options['command']}  &",
                "stepname": name,
            })

        return jobs_to_submit
