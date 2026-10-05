"""Audit actual surviving strata and core footprints; no hydraulic K is fitted."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--seed', type=int, default=42)
    ap.add_argument('--nodes', type=int, nargs='+', default=[6329, 6327, 1165, 6582])
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    sys.path.insert(0, str(Path('extern/skyisle-gen').resolve()))
    from skyisle_gen.cli import _ctx_from_run
    from skyisle_gen.island import generate, engine as IE
    from skyisle_gen import engine as E, _core
    run = Path(f'build/river-preview/seed{args.seed}').resolve()
    ctx = _ctx_from_run(str(run))
    world = _core.planet_run(_core.make_config(E.planet_config(ctx.cfg)), ctx.seed, 4, 4, False)
    for i, obj in enumerate(world, 1):
        E.put_part(ctx, i, obj)
    report = dict(seed=args.seed, status='stratigraphic geometry and candidate footprint, not a spring map',
        candidate=dict(exchange_days=4., footprint_scale=2., res_m=300.),
        native_sha256=hashlib.sha256(Path(_core.__file__).read_bytes()).hexdigest(), nodes={})
    for node in args.nodes:
        g = generate(ctx, node, res_m=300, steps=2, write=False, log=lambda _: None,
            sets=['island.water.core_exchange_days=4', 'island.water.core_footprint_scale=2'])
        mask = g['island_id'] == 0
        has_marl = np.zeros(mask.shape, dtype=bool)
        depth, thickness, lime_above = [], [], []
        for i, j in zip(*np.where(mask)):
            surface, base = g['height'][i, j], g['skel_top'][i, j]
            p = _core.strat_column(float(surface), float(g['strat_top'][i, j]), float(base), g['strat_raw'][0])
            assert abs(sum(t-b for b, t, _ in p)-(surface-base)) < 1e-6
            for k in range(len(p)-1, -1, -1):
                b, t, lith = p[k]
                if lith == 2:
                    has_marl[i, j] = True
                    depth.append(surface-t)
                    thickness.append(t-b)
                    lime_above.append(sum(hi-lo for lo, hi, li in p[k+1:] if li == 1))
                    break
        core = g['condense_mm'] > 0
        channels = ((g['river'] > 0) | (g['stream'] > 0)) & mask & ~g['lake'].astype(bool)
        rr, cc = np.indices(mask.shape)
        length = np.where((g['recv_i'] != rr) & (g['recv_j'] != cc), np.sqrt(2), 1)*300
        by_area = {}
        for lo, hi in [(.3, 1), (1, 10), (10, 50), (50, np.inf)]:
            pick = channels & (g['flowacc_km2'] >= lo) & (g['flowacc_km2'] < hi)
            den = length[pick].sum()
            by_area[f'{lo:g}-{hi:g}'] = dict(length_km=float(den/1000),
                direct_core_coverage=float(length[pick & core].sum()/den) if den else None,
                remaining_marl_below=float(length[pick & has_marl].sum()/den) if den else None)
        report['nodes'][str(node)] = dict(main_area_km2=float(mask.sum()*.09),
            core_land_coverage=float(np.mean(core[mask])), marl_below_land_fraction=float(np.mean(has_marl[mask])),
            overlying_rock_depth_p10_p50_p90_m=np.quantile(depth, [.1, .5, .9]).tolist() if depth else None,
            first_marl_remaining_thickness_p10_p50_p90_m=np.quantile(thickness, [.1, .5, .9]).tolist() if depth else None,
            overlying_limestone_p10_p50_p90_m=np.quantile(lime_above, [.1, .5, .9]).tolist() if depth else None,
            core_input_over_marl_share=float(g['condense_mm'][has_marl].sum()/g['condense_mm'][mask].sum()),
            channel_by_area=by_area)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
        print(node, report['nodes'][str(node)], flush=True)


if __name__ == '__main__':
    main()
