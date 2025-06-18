from loguru import logger

logger.remove()
logger.add(
    sink=lambda msg: print(msg, end=''),  # or use sys.stdout
    format="<green>{time:DD/MM/YYYY HH:mm:ss}</green> | <cyan>{name}</cyan> | <level>{level: <8}</level> | <level>{message}</level>",
    colorize=True
)


class BashGenerator:
    def __init__(self, workflow):
        self.workflow = workflow
        logger.info(f"Initializing BashGenerator with workflow {self.workflow['workflow-name']}")

    def _generate_master_script(self, generated_scripts):
        waitpids = []
        filename = "/tmp/submit-all.sh"
        with open(filename, "w") as f:
            f.write("#!/bin/bash\n\n")
            for i, script in enumerate(generated_scripts):
                f.write(f"bash {script} & PID{i}=$!\n")
                waitpids.append(f"$PID{i}")

            f.write(f"\n\nwait {' '.join(waitpids)}\n")
        return filename

    def generate(self,
                 jobs_to_submit,
                 capio_dir,
                 server_path,
                 server_args,
                 server_config_path,
                 intercept_path
                 ):
        generated_scripts = []

        for sub_location, sub_steps in jobs_to_submit.items():
            filename = f"/tmp/{self.workflow['workflow-name']}-{sub_location}.sh"
            logger.info(f"Generating bash script {filename}")
            generated_scripts.append(filename)
            with open(filename, "w") as f:
                f.write("#!/bin/bash\n\n")
                f.write(
                    f"CAPIO_DIR={capio_dir}  \\\n"
                    f"{server_path} {server_args} {server_config_path} & SERVERPID=$!\n\n")
                waitpids = []
                for step in sub_steps:
                    logger.info(f"Generating command for step {step['stepname']}")
                    f.write(
                        f"{step['env']} \\\nLD_PRELOAD={intercept_path} \\\n{step['runnable']} PID{step['stepname']}=$!\n\n")
                    waitpids.append(f"$PID{step['stepname']}")
                f.write(f"\nwait {' '.join(waitpids)}\n")
                f.write("killall $SERVERPID\n")

        master_script_path = self._generate_master_script(generated_scripts)
        logger.info("Generated master script")

        return master_script_path
