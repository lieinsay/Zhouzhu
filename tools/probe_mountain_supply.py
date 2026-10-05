"""Inspect whole-mountain core capacities, without fitting channel widths.

Activity is an explicit world ability per supported mountain volume, not an
Earth measurement. Report all fixed sample islands and retain dry/wet contrasts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--activity-per-km-day', type=float, nargs='+', required=True)
    ap.add_argument('--samples', type=Path, default=Path('build/river-scale-study.json'))
    ap.add_argument('--res-m', type=float, default=300.)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if not all(np.isfinite(a) and a > 0 for a in args.activity_per_km_day):
        ap.error('Explicit positive finite world abilities required')
    sys.path.insert(0, str(Path('extern/skyisle-gen').resolve()))
    from skyisle_gen import _core, engine as E
    from skyisle_gen.cli import _ctx_from_run
    from skyisle_gen.island import generate, engine as IE
    samples = json.loads(args.samples.read_text(encoding='utf-8'))['samples']
    report = dict(status='capacity scenarios, no adopted defaults or channel-width fitting',
        native_sha256=hashlib.sha256(Path(_core.__file__).read_bytes()).hexdigest(),
        res_m=args.res_m, nodes={})
    for seed, nodes in samples.items():
        ctx = _ctx_from_run(str(Path(f'build/river-preview/seed{seed}').resolve()))
        E.clear_cache()
        IE._PLANET_OBJ.clear()
        world = _core.planet_run(_core.make_config(E.planet_config(ctx.cfg)), ctx.seed, 4, 4, False)
        for i, obj in enumerate(world, 1):
            E.put_part(ctx, i, obj)
        for item in nodes:
            node = item['node']
            baseline = generate(ctx, node, res_m=args.res_m, steps=2, write=False, log=lambda _: None,
                sets=['island.water.core_mountain_domain=false', 'island.water.core_exchange_days=0',
                      'island.water.core_footprint_scale=0'])
            mask = baseline['island_id'] == 0
            cell_km2 = baseline['res_km']**2
            conversion = cell_km2*1000/baseline['year_s']
            legacy_q = float(baseline['condense_mm'][mask].sum(dtype=float)*conversion)
            natural_q = float((baseline['runoff_mm'][mask].astype(float)-baseline['condense_mm'][mask]).sum()*conversion)
            rows = []
            report['nodes'][f'{seed}/{node}'] = rows
            for activity in args.activity_per_km_day:
                g = generate(ctx, node, res_m=args.res_m, steps=2, write=False, log=lambda _: None,
                    sets=['island.water.core_mountain_domain=true', 'island.water.core_exchange_days=0',
                          'island.water.core_footprint_scale=0', f'island.water.core_activity_per_km_day={activity}'])
                np.testing.assert_array_equal(g['island_id'], baseline['island_id'])
                np.testing.assert_array_equal(g['rain_mm'], baseline['rain_mm'])
                sources = [dict(s) for s in g['core_water_sources'] if s['island'] == 0]
                q = float(g['condense_mm'][mask].sum(dtype=float)*conversion)
                assert abs(sum(s['capacity_m3s'] for s in sources)-q) <= 1e-6*max(q, 1e-12)
                for s in sources:
                    assert abs(s['capacity_m3s']-s['condense_m3s']) <= 1e-10*max(s['capacity_m3s'], 1e-12)
                    s['latent_W_m2'] = s['capacity_m3s']*1000*2.45e6/(s['domain_area_km2']*1e6)
                    s['equivalent_column_exchange_days'] = s['domain_area_km2']/(s['mountain_volume_km3']*activity)
                row = dict(activity_per_km_day=activity, area_km2=float(mask.sum()*cell_km2),
                    natural_runoff_m3s=natural_q, legacy_core_m3s=legacy_q, core_m3s=q,
                    core_to_natural=q/natural_q if natural_q > 0 else None,
                    covered_land_fraction=float(np.mean(g['condense_mm'][mask] > 0)),
                    condensation_mm_year_p10_p50_p90=np.quantile(g['condense_mm'][mask], [.1,.5,.9]).tolist(),
                    sources=sources, ocean_evaporation_obligation_m3s=q,
                    latent_heat_MW=q*1000*2.45e6/1e6,
                    heat_note='Latent heat and equal ocean source are obligations, not a solved heat/transport balance')
                rows.append(row)
                args.out.parent.mkdir(parents=True, exist_ok=True)
                args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
                print(seed, node, activity, 'Qcore', round(q,3), 'Qrain', round(natural_q,3),
                    'latent W/m2', [round(s['latent_W_m2'],2) for s in sources], flush=True)


if __name__ == '__main__':
    main()
