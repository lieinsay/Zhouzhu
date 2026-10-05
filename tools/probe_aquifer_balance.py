"""Check the conservative aquifer solver on real generated island geometry.

Uniform K values are sensitivity scenarios, NOT assigned world lithologies or
calibrated spring parameters. A single aquifer above the floating skeleton is
an explicit simplification; perched layers/karst must be assessed separately.
No classification, preview, game data or default parameters are changed.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import sys
import time

import numpy as np


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run', type=Path, default=Path('build/river-preview/seed42'))
    ap.add_argument('--nodes', type=int, nargs='+', default=[6329, 6327, 1165, 6582])
    ap.add_argument('--res-m', type=float, default=300.)
    ap.add_argument('--k-m-day', type=float, nargs='+', default=[.01, .1, 1.])
    ap.add_argument('--out', type=Path, default=Path('build/aquifer-balance-study.json'))
    args = ap.parse_args()
    sys.path.insert(0, str(Path('extern/skyisle-gen').resolve()))
    from skyisle_gen.cli import _ctx_from_run
    from skyisle_gen.island import generate
    from skyisle_gen import _core
    ctx = _ctx_from_run(str(args.run.resolve()))
    result = dict(res_m=args.res_m, run=str(args.run.resolve()),
                  config_sha256=hashlib.sha256((args.run/'config.resolved.toml').read_bytes()).hexdigest(),
                  native_sha256=hashlib.sha256(Path(_core.__file__).read_bytes()).hexdigest(),
                  status='single-layer numerical/material sensitivity; not a validated spring map', nodes={})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    for node in args.nodes:
        g = generate(ctx, node, res_m=args.res_m, steps=2, write=False, log=lambda _: None)
        main = g['island_id'] == 0
        rows, cols = np.where(main)
        sl = np.s_[rows.min():rows.max()+1, cols.min():cols.max()+1]
        mask = np.ascontiguousarray(main[sl])
        # Use existing surface and skeleton; do not invent a shallow bed to
        # increase inland springs. Resolve perched lithological layers later.
        top = np.ascontiguousarray(g['height'][sl], dtype=float)
        bottom = np.ascontiguousarray(g['skel_top'][sl], dtype=float)
        if np.any(bottom[mask] > top[mask]):
            raise ValueError('Skeleton above terrain: inspect geometry, do not silently clip')
        records = []
        for kday in args.k_m_day:
            for source in ['recharge_mm', 'condense_mm']:
                recharge = np.ascontiguousarray(g[source][sl]/(1000*g['year_s']), dtype=float)
                k = np.full(top.shape, kday/86400.)
                t = time.perf_counter()
                r = _core.solve_aquifer_balance(top, bottom, k, recharge, mask, args.res_m)
                volume = r['recharge_m3s']
                gradients = []
                for axis in [0, 1]:
                    a, b = (mask[:-1, :], mask[1:, :]) if axis == 0 else (mask[:, :-1], mask[:, 1:])
                    gradients.extend((np.abs(np.diff(r['head_m'], axis=axis))[a & b]/args.res_m).tolist())
                record = dict(k_m_day=kday, source=source, converged=r['converged'],
                    iterations=r['iterations'], seconds=time.perf_counter()-t, input_m3s=volume,
                    surface_m3s=float(r['surface_m3s'].sum()), coast_m3s=float(r['coast_m3s'].sum()),
                    residual_abs_m3s=float(np.abs(r['residual_m3s']).sum()),
                    exposed_cell_share=float(np.count_nonzero(r['surface_m3s'] > 1e-12)/mask.sum()),
                    head_slope_p50_p90=np.quantile(gradients, [.5, .9]).tolist(),
                    head_slope_above_point1_fraction=float(np.mean(np.asarray(gradients) > .1)),
                    note='Annual steady state; core-only run is not by itself proof of observed year-round flow.')
                records.append(record)
                print(node, kday, source, 'converged', r['converged'], 'surface/coast',
                      record['surface_m3s'], record['coast_m3s'], 'seconds', record['seconds'], flush=True)
                result['nodes'][str(node)] = records
                args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
                del r
        del g
        gc.collect()


if __name__ == '__main__':
    main()
