with open("delay_vs_fanout.csv", "w") as f:
    print("fanout,delay", file=f)
    for net_name, net in ctx.nets:
        if net.driver.cell is None:
            continue
        if net.driver.cell.type == "DCCA":
            continue # ignore global clocks
        for user in net.users:
            print(f"{len(net.users)},{ctx.getNetinfoRouteDelay(net, user)}", file=f)

