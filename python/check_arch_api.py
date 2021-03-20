""" Script to do Arch API sanity checking.

This python script can be used to do some sanity checking of either wire to
wire connectivity or bel pin wire connectivity.

Wire to wire connectivity is tested by supplying a source and destination wire
and verifing that a pip exists that connects those wires.

Bel pin wire connectivity is tested by supplying a bel and pin name and the
connected wire.

Invoke in a working directory that contains a file name "test_data.yaml":
  ${NEXTPNR} --run ${NEXTPNR_SRC}/check_arch_api.py

"test_data.yaml" should contain the test vectors for the wire to wire or bel
pin connectivity tests. Example test_data.yaml:

pip_test:
    - src_wire: CLBLM_R_X11Y93/CLBLM_L_D3
      dst_wire: SLICE_X15Y93.SLICEL/D3
pip_chain_test:
    - wires:
        - $CONSTANTS_X0Y0.$CONSTANTS/$GND_SOURCE
        - $CONSTANTS_X0Y0/$GND_NODE
        - TIEOFF_X3Y145.TIEOFF/$GND_SITE_WIRE
bel_pin_test:
    - bel: SLICE_X15Y93.SLICEL/D6LUT
      pin: A3
      wire: SLICE_X15Y93.SLICEL/D3

"""
import yaml
import sys




def check_arch_api(ctx):
    success = True
    pips_tested = 0
    pips_failed = 0

    def test_pip(src_wire_name, dst_wire_name):
        nonlocal success
        nonlocal pips_tested
        nonlocal pips_failed

        pip = None
        for pip_name in ctx.getPipsDownhill(src_wire_name):
            if ctx.getPipDstWire(pip_name) == dst_wire_name:
                pip = pip_name
                src_wire = ctx.getPipSrcWire(pip_name)
                assert src_wire == src_wire_name, (
                        src_wire, src_wire_name)


        if pip is None:
            success = False
            pips_failed += 1
            print('Pip from {} to {} failed'.format(src_wire_name, dst_wire_name))
        else:
            pips_tested += 1
    bel_pins_tested = 0
    with open('test_data.yaml', 'r') as f:
        test_data = yaml.safe_load(f.read())
        if 'pip_test' in test_data:
            for pip_test in test_data['pip_test']:
                test_pip(pip_test['src_wire'], pip_test['dst_wire'])

        if 'pip_chain_test' in test_data:
            for chain_test in test_data['pip_chain_test']:
                wires = chain_test['wires']
                for src_wire, dst_wire in zip(wires, wires[1:]):
                    test_pip(src_wire, dst_wire)

        if 'bel_pin_test' in test_data:
            for bel_pin_test in test_data['bel_pin_test']:
                wire_name = ctx.getBelPinWire(bel_pin_test['bel'], bel_pin_test['pin'])
                assert bel_pin_test['wire'] == wire_name, (bel_pin_test['wire'], wire_name)

                if 'type' in bel_pin_test:
                    pin_type = ctx.getBelPinType(bel_pin_test['bel'], bel_pin_test['pin'])
                    assert bel_pin_test['type'] == pin_type.name, (bel_pin_test['type'], pin_type)

                bel_pins_tested += 1

    print('Tested {} pips and {} bel pins'.format(pips_tested, bel_pins_tested))

    if not success:
        print('{} pips failed'.format(pips_failed))
        sys.exit(-1)


check_arch_api(ctx)
