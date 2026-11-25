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

With `--report-setup-paths N`, the additional `setup_paths` array contains up to N same-clock setup paths across the design in ascending slack order. Each entry has the launch and capture clock events (`from` and `to`), the native path `delay` and `slack` in ns, and the segment list `path` in the same format as `critical_paths`. The limit counts distinct endpoint and clock-event pairs. Equal-slack endpoints can appear together. Each endpoint contributes its worst traced path for that event pair.
