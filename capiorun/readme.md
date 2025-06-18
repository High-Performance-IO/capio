# CAPIORUN: autmated submission of CAPIO workflows
To execute workflows, capiorun requires a json file containing the description 
of the workflow steps to execute.

```json
{
  "capiocl-file" : "capio_cl config path name",
  "capio-dir" : "dir",
  "steps" : {
    "workflow_step_name": {
      "exec": "bin path",
      "args": "args",
      "location": "logic location. used to tell which steps should go in the same submission script"
    },
    ...
  }
}
```