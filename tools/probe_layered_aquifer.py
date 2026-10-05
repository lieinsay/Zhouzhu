"""Actual-island layered groundwater scenarios, with explicit material inputs.

No production material defaults, perennial classification or channel geometry
are changed. Tables below are disclosed analogy scenarios, not measured island
properties. See docs/RIVER-MODEL-AUDIT.md for sources and applicability limits.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import sys
import time

import numpy as np

from audit_river_scale import quantiles


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--seed', type=int, default=42)
    ap.add_argument('--nodes', type=int, nargs='+', default=[6329])
    ap.add_argument('--marl-ms', type=float, nargs='+', default=[1e-9])
    ap.add_argument('--carbonate-m-day', type=float, default=.1)
    ap.add_argument('--crystalline-m-day', type=float, required=True,
        help='Explicit comparison only; generic crystalline values are not an ophiolite default')
    ap.add_argument('--max-iterations', type=int, default=50000)
    ap.add_argument('--shallow-depth-m', type=float, default=0.)
    ap.add_argument('--shallow-gabbro-ms', type=float, nargs='+')
    ap.add_argument('--shallow-serpentinite-ms', type=float)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    shallow_on = args.shallow_depth_m > 0
    if shallow_on != (args.shallow_gabbro_ms is not None and args.shallow_serpentinite_ms is not None):
        ap.error('A shallow horizon needs an explicit positive depth and both rock conductivities')
    sys.path.insert(0, str(Path('extern/skyisle-gen').resolve()))
    from skyisle_gen.cli import _ctx_from_run
    from skyisle_gen.island import generate
    from skyisle_gen import engine as E, _core
    ctx = _ctx_from_run(str(Path(f'build/river-preview/seed{args.seed}').resolve()))
    world = _core.planet_run(_core.make_config(E.planet_config(ctx.cfg)), ctx.seed, 4, 4, False)
    for i, obj in enumerate(world, 1):
        E.put_part(ctx, i, obj)
    report = dict(seed=args.seed, status='conditional layered aquifer scenario, not adopted defaults',
        core=dict(exchange_days=4, footprint_scale=2), res_m=300,
        native_sha256=hashlib.sha256(Path(_core.__file__).read_bytes()).hexdigest(), nodes={})
    for node in args.nodes:
        g = generate(ctx, node, res_m=300, steps=2, write=False, log=lambda _: None,
            sets=['island.water.core_exchange_days=4', 'island.water.core_footprint_scale=2'])
        full = g['island_id'] == 0
        rows, cols = np.where(full)
        r0, c0, r1, c1 = rows.min(), cols.min(), rows.max()+1, cols.max()+1
        sl = np.s_[r0:r1, c0:c1]
        mask = np.ascontiguousarray(full[sl])
        top, structural, base = [np.ascontiguousarray(g[k][sl], dtype=float) for k in ('height', 'strat_top', 'skel_top')]
        cond = np.ascontiguousarray(g['condense_mm'][sl]/(1000*g['year_s']), dtype=float)
        rain = np.maximum(0, g['runoff_mm'][sl].astype(float)-g['condense_mm'][sl])/(1000*g['year_s'])
        bfi = g['bfi'][sl].astype(float)
        ri, rj = g['recv_i'][sl]-r0, g['recv_j'][sl]-c0
        valid = mask & (ri >= 0) & (ri < mask.shape[0]) & (rj >= 0) & (rj < mask.shape[1])
        receiver = np.full(mask.shape, -1, dtype=np.int64)
        receiver[valid] = ri[valid]*mask.shape[1]+rj[valid]
        channel = mask & ((g['river'][sl] > 0) | (g['stream'][sl] > 0)) & ~g['lake'][sl].astype(bool)
        rr, cc = np.indices(mask.shape)
        length = np.where(valid & (ri != rr) & (rj != cc), np.sqrt(2), 1)*300
        area = g['flowacc_km2'][sl]
        report['nodes'][str(node)] = []
        for marl, shallow_gabbro in itertools.product(args.marl_ms, args.shallow_gabbro_ms or [None]):
            # Per-unit isotropy is a scenario assumption. Resolved layering
            # creates bulk anisotropy; vertical marl measurements do not prove Kh.
            km = [args.carbonate_m_day/86400, marl, args.crystalline_m_day/86400,
                  args.crystalline_m_day/86400, 0.]
            shallow_k = [km[0], km[1], shallow_gabbro, args.shallow_serpentinite_ms, 0.] if shallow_on else None
            profile = dict(shallow_depth_m=args.shallow_depth_m,
                shallow_horizontal_ms=shallow_k, shallow_vertical_ms=shallow_k) if shallow_on else {}
            model = _core.make_layered_aquifer(top, structural, base, mask, g['strat_raw'][0],
                km, km, [.01]*5, [1e-6]*5, 300., **profile)
            for source_name, recharge, quick in [('core', cond, np.zeros(mask.shape)),
                                                ('total', cond+rain*bfi, rain*(1-bfi)*300**2)]:
                started = time.perf_counter()
                solved = model.solve(np.ascontiguousarray(recharge), max_iterations=args.max_iterations)
                record = dict(source=source_name, material_horizontal_ms=km, material_vertical_ms=km,
                    shallow_profile=profile,
                    specific_yield=[.01]*5, specific_storage_per_m=[1e-6]*5,
                    storage_note='Sy=.01 and Ss=1e-6/m are explicit scenarios; neither affects a steady solution',
                    nodes=len(model.node_cells), iterations=solved['iterations'], converged=solved['converged'],
                    seconds=time.perf_counter()-started, input_m3s=solved['input_m3s'],
                    surface_m3s=float(solved['surface_m3s'].sum()), coast_m3s=float(solved['coast_m3s'].sum()),
                    absolute_residual_m3s=solved['absolute_residual_m3s'])
                if solved['converged']:
                    routed = _core.route_surface_water(np.ascontiguousarray(solved['surface_m3s']+quick), mask, receiver.ravel())
                    q = routed['river_m3s']
                    numerical_floor = max(1e-12, solved['absolute_residual_m3s'])
                    record['numerical_flow_floor_m3s'] = numerical_floor
                    record['surface_outlet_m3s'] = routed['outlet_m3s']
                    record['per_area'] = {}
                    for lo, hi in [(.3, 1), (1, 10), (10, 50), (50, np.inf)]:
                        pick = channel & (area >= lo) & (area < hi)
                        den = length[pick].sum()
                        record['per_area'][f'{lo:g}-{hi:g}'] = dict(length_km=float(den/1000),
                            supplied_share=float(length[pick & (q > numerical_floor)].sum()/den) if den else None,
                            flow_m3s=quantiles(q[pick], length[pick]) if den else None)
                report['nodes'][str(node)].append(record)
                args.out.parent.mkdir(parents=True, exist_ok=True)
                args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
                print(node, marl, source_name, record, flush=True)
                if not solved['converged']:
                    raise RuntimeError('Unconverged layered state rejected; no spring routing accepted')


if __name__ == '__main__':
    main()
