# **`capiorun` – Simplified Launch of CAPIO Applications**

`capiorun` is a lightweight Python utility designed to streamline the execution of workflow steps with **CAPIO**. It automates the setup of required environment variables and manages the lifecycle of the `capio_server` instance, reducing the manual configuration needed by users.

If a `capio_server` instance is not already running for a given workflow, `capiorun` will automatically start and manage it.

---

## 📌 **Parameters**

| Flag | Description |
|------|-------------|
| `--capio-dir` *(required)* | Specifies the CAPIO virtual mount point. |
| `--capiocl` | Path to the CAPIO-CL configuration file. |
| `--app-name` *(required)* | The name of the application step being launched. Must match an entry in the CAPIO-CL configuration file (`--capiocl`). |
| `--workflow-name` | The name of the workflow to which the application step (`--app-name`) belongs. |
| `--capio-log-level` *(optional)* | Sets the log level if CAPIO is executed in debug mode. |
| `args` | Remaining parameters that represent the executable and its arguments to be run with CAPIO. |

---

## ⚙️ **Optional Overrides**

You can also explicitly specify the locations of both `libcapio_posix.so` and the `capio_server` binary:

| Flag | Description |
|------|-------------|
| `--libcapio` | Path to the `libcapio_posix.so` shared library. |
| `--server` | Path to the `capio_server` executable. |
