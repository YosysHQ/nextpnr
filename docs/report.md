# JSON Reports

nextpnr can write a JSON report using `--report` post-place-and-route for integration with other build systems. It contains information on post-pack utilization and maximum achieved frequency for each clock domain, and is of the following format:

```
{
    "utilization": {
        <beltype>: {
            "used": <number of bels used in design>,
            "available": <total number of bels available in device>
        }, 
        ...
    },
    "fmax": {
        <clock domain>: {
            "achieved": <computed Fmax of routed design for clock in MHz>,
            "constraint": <constraint for clock in MHz>
        },
        ...
    }
}
```
