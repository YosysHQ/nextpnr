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
bel_pin_test:
    - bel: SLICE_X15Y93.SLICEL/D6LUT
      pin: A3
      wire: SLICE_X15Y93.SLICEL/D3

"""
import yaml


def check_arch_api(ctx):
    pips_tested = 0
    bel_pins_tested = 0
    with open('test_data.yaml', 'r') as f:
        test_data = yaml.safe_load(f.read())
        if 'pip_test' in test_data:
            for pip_test in test_data['pip_test']:
                pip = None
                for pip_name in ctx.getPipsDownhill(pip_test['src_wire']):
                    if ctx.getPipDstWire(pip_name) == pip_test['dst_wire']:
                        pip = pip_name
                        src_wire = ctx.getPipSrcWire(pip_name)
                        assert src_wire == pip_test['src_wire'], (
                                src_wire, pip_test['src_wire'])

                assert pip is not None
                pips_tested += 1

        if 'bel_pin_test' in test_data:
            for bel_pin_test in test_data['bel_pin_test']:
                wire_name = ctx.getBelPinWire(bel_pin_test['bel'], bel_pin_test['pin'])
                assert bel_pin_test['wire'] == wire_name, (bel_pin_test['wire'], wire_name)

                bel_pins_tested += 1

    print('Tested {} pips and {} bel pins'.format(pips_tested, bel_pins_tested))

check_arch_api(ctx)
