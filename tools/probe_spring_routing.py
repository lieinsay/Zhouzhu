"""Trace constant-core equilibrium springs through actual island D8 networks.

This is a conditional lower-supply scenario, NOT a completed perennial map:
core supply is constant as in the existing annual-core model, one aquifer uses
explicit uniform K, there are no downstream channel losses, and initial heads
are equilibrated to that supply. No preview or game data is written.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

from audit_river_scale import quantiles


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run', type=Path, default=Path('build/river-preview/seed42'))
    ap.add_argument('--nodes', type=int, nargs='+', default=[6329, 6327, 1165, 6582])
    ap.add_argument('--res-m', type=float, default=300.)
    ap.add_argument('--k-m-day', type=float, nargs='+', default=[.01, .1, 1.])
    ap.add_argument('--exchange-days', type=float, default=0.)
    ap.add_argument('--footprint-scale', type=float, default=0.)
    ap.add_argument('--out', type=Path, default=Path('build/spring-routing-study.json'))
    args = ap.parse_args()
    sys.path.insert(0, str(Path('extern/skyisle-gen').resolve()))
    from skyisle_gen.cli import _ctx_from_run
    from skyisle_gen.island import generate, engine as IE
    from skyisle_gen import _core, engine as E
    ctx = _ctx_from_run(str(args.run.resolve()))
    if args.exchange_days:
        E.clear_cache()
        IE._PLANET_OBJ.clear()
        world = _core.planet_run(_core.make_config(E.planet_config(ctx.cfg)), ctx.seed, 4, 4, False)
        for i, obj in enumerate(world, 1):
            E.put_part(ctx, i, obj)
    report = dict(status='conditional constant-core equilibrium; no final classification or geometry',
        res_m=args.res_m, run=str(args.run.resolve()), exchange_days=args.exchange_days,
        footprint_scale=args.footprint_scale,
        config_sha256=hashlib.sha256((args.run/'config.resolved.toml').read_bytes()).hexdigest(),
        native_sha256=hashlib.sha256(Path(_core.__file__).read_bytes()).hexdigest(), nodes={})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    for node in args.nodes:
        g = generate(ctx, node, res_m=args.res_m, steps=2, write=False, log=lambda _: None,
                     sets=[f'island.water.core_exchange_days={args.exchange_days}',
                           f'island.water.core_footprint_scale={args.footprint_scale}'])
        main = g['island_id'] == 0
        rows, cols = np.where(main)
        r0, c0, r1, c1 = rows.min(), cols.min(), rows.max()+1, cols.max()+1
        sl = np.s_[r0:r1, c0:c1]
        mask = np.ascontiguousarray(main[sl])
        shape = mask.shape
        top = np.ascontiguousarray(g['height'][sl], dtype=float)
        base = np.ascontiguousarray(g['skel_top'][sl], dtype=float)
        cond = np.ascontiguousarray(g['condense_mm'][sl]/(1000*g['year_s']), dtype=float)
        ri, rj = g['recv_i'][sl]-r0, g['recv_j'][sl]-c0
        valid = mask & (ri >= 0) & (ri < shape[0]) & (rj >= 0) & (rj < shape[1])
        recv = np.full(shape, -1, dtype=np.int64)
        recv[valid] = ri[valid]*shape[1]+rj[valid]
        if np.any(~mask.ravel()[recv[valid]]):
            raise ValueError('surface routing crosses the main-island mask')
        area = g['flowacc_km2'][sl]
        old_perennial = g['river'][sl] > 0
        channel = mask & ((g['river'][sl] > 0) | (g['stream'][sl] > 0)) & ~g['lake'][sl].astype(bool)
        rr, cc = np.indices(shape)
        length = np.where(valid & (ri != rr) & (rj != cc), np.sqrt(2), 1)*args.res_m
        records = []
        for kday in args.k_m_day:
            km = np.full(shape, kday/86400.)
            steady = _core.solve_aquifer_balance(top, base, km, cond, mask, args.res_m)
            if not steady['converged']:
                raise RuntimeError(f'{node}/{kday}: equilibrium did not converge')
            # Sy cannot change an equilibrium result. Explicit value is a
            # fixture for checking the steady -> time-step interface, not a
            # material assignment or evidence about seasonal storage capacity.
            routed = _core.step_spring_routing(top, base, km, np.zeros(shape), cond,
                np.zeros(shape), mask, recv.ravel(), steady['head_m'], np.full(shape, .1),
                args.res_m, 86400.)
            a, q = routed['aquifer'], routed['river_m3s']
            error = abs((routed['core_input_m3s']-routed['surface_outlet_m3s']-
                         a['coast_m3s'].sum())*86400-a['total_storage_change_m3'])
            if error > max(1e-12, routed['core_input_m3s']*1e-7)*86400*1.01:
                raise RuntimeError('coupled groundwater/surface volume failed')
            # Numerical cutoff only; not a visual-width or river-size target.
            supported = q > 1e-10
            groups = {}
            for lo, hi in [(.3, 1), (1, 10), (10, 50), (50, np.inf)]:
                sel = channel & (area >= lo) & (area < hi)
                den = length[sel].sum()
                groups[f'{lo:g}-{hi:g}'] = dict(length_km=float(den/1000),
                    old_perennial_share=float(length[sel & old_perennial].sum()/den) if den else None,
                    core_supported_share=float(length[sel & supported].sum()/den) if den else None,
                    core_flow_m3s=quantiles(q[sel], length[sel]) if den else None)
            record = dict(k_m_day=kday, core_input_m3s=routed['core_input_m3s'],
                paired_ocean_evaporation_m3s=routed['core_input_m3s'],
                latent_heat_mw=routed['core_input_m3s']*1000*2.45,
                peak_latent_heat_w_m2=float(cond[mask].max()*1000*2.45e6),
                core_sources=[s for s in g.get('core_water_sources', []) if s['island'] == 0],
                surface_outlet_m3s=routed['surface_outlet_m3s'],
                coast_m3s=float(a['coast_m3s'].sum()), storage_change_m3=a['total_storage_change_m3'],
                relative_balance_error=error/max(routed['core_input_m3s']*86400, 1e-12),
                per_area=groups)
            records.append(record)
            print(node, kday, 'core-supported length shares',
                  {b: round(v['core_supported_share'], 3) if v['core_supported_share'] is not None else None
                   for b, v in groups.items()}, flush=True)
            report['nodes'][str(node)] = records
            args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
        del g
        gc.collect()


if __name__ == '__main__':
    main()
