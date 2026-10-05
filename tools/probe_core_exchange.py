"""Isolated balanced-core candidate; no settings or preview/game packs written.

Exchange times are disclosed world-setting sensitivities (not calibrated from
river widths). All scenarios use the same weather, terrain algorithm, river
eligibility and geometry formula. This isolates supply; it does not validate
the inherited geometry or groundwater exchange model.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

from audit_river_scale import audit, quantiles, topology, wetted_width


def daily_scales(doc):
    p, down, basin, island, weights, order, _ = topology(doc)
    bs = {b['id']: b for b in doc['basins']}
    width, depth = np.zeros(len(p)), np.zeros(len(p))
    for bid in np.unique(basin):
        for regime, key in [(0, 'event_index'), (1, 'index')]:
            ids = np.flatnonzero((basin == bid) & (p[:, 20] == regime))
            vals = np.sort(bs[int(bid)][key])  # annual median includes dry days
            lo, hi = vals[(len(vals)-1)//2], vals[len(vals)//2]
            wl, dl = wetted_width(p[ids], lo, doc['width_exponent'], doc['depth_exponent'])
            wh, dh = wetted_width(p[ids], hi, doc['width_exponent'], doc['depth_exponent'])
            width[ids], depth[ids] = (wl+wh)/2, (dl+dh)/2
    return p, weights, order, width, depth


def compare(base, candidate):
    p, weights, order, bw, bd = base
    np_, nw, no, cw, cd = candidate
    # Supply-only experiment must not move sources/junctions or add perennials.
    np.testing.assert_array_equal(np_[:, [0, 1, 2, 3, 20]], p[:, [0, 1, 2, 3, 20]])
    np.testing.assert_array_equal(no, order)
    np.testing.assert_array_equal(nw, weights)
    def group(mask):
        den = weights[mask].sum()
        both = mask & (cw > bw+1e-4) & (cd > bd+1e-4)
        ratio_mask = mask & (bw > 0) & (bd > 0)
        return dict(length_km=float(den/1000),
                    both_larger_length_share=float(weights[both].sum()/den) if den else None,
                    width_ratio=quantiles(cw[ratio_mask]/bw[ratio_mask], weights[ratio_mask]),
                    depth_ratio=quantiles(cd[ratio_mask]/bd[ratio_mask], weights[ratio_mask]))
    return dict(all_perennial=group(p[:, 20] == 1), temporary=group(p[:, 20] == 0),
                perennial_by_area={f'{lo:g}-{hi:g}': group((p[:, 20] == 1) & (p[:, 3] >= lo) & (p[:, 3] < hi))
                                   for lo, hi in [(.3, 1), (1, 10), (10, 50), (50, np.inf)]},
                perennial_by_order={str(s): group((p[:, 20] == 1) & (order == s)) for s in np.unique(order)})


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runs', type=Path, default=Path('build/river-preview'))
    ap.add_argument('--samples', type=Path, default=Path('build/river-scale-study.json'))
    ap.add_argument('--out', type=Path, default=Path('build/river-core-exchange.json'))
    ap.add_argument('--times', type=float, nargs='+', default=[8., 4., 2.])
    ap.add_argument('--footprint-scale', type=float, default=0.)
    ap.add_argument('--res-m', type=float)
    args = ap.parse_args()
    if not all(t > 0 and np.isfinite(t) for t in args.times):
        ap.error('exchange times must be positive and finite')
    sys.path.insert(0, str(Path('extern/skyisle-gen').resolve()))
    from skyisle_gen.cli import _ctx_from_run
    from skyisle_gen import engine as E, _core
    from skyisle_gen.island import generate
    from skyisle_gen.island import engine as IE
    from skyisle_gen.island.rivernet import rivers_doc
    samples = json.loads(args.samples.read_text(encoding='utf-8'))['samples']
    result = dict(status='isolated candidate, NOT adopted production model',
                  exchange_times_days=args.times,
                  footprint_scale=args.footprint_scale, res_m=args.res_m,
                  inherited_terms=('annual core input, old river eligibility and hydraulic geometry' if args.footprint_scale else
                      'core volume/height footprint, annual rain ceiling, annual core input, river eligibility and hydraulic geometry'),
                  source='Fresh seasonal model column; paired ocean evaporation is a prescribed boundary, not a solved ocean circulation.',
                  native_sha256=hashlib.sha256(Path(_core.__file__).read_bytes()).hexdigest(), seeds={}, nodes={})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    for seed_text, selection in samples.items():
        seed = int(seed_text)
        ctx = _ctx_from_run(str((args.runs/f'seed{seed}').resolve()))
        result['seeds'][seed_text] = dict(config_sha256=hashlib.sha256(
            (args.runs/f'seed{seed}'/'config.resolved.toml').read_bytes()).hexdigest())
        E.clear_cache()
        IE._PLANET_OBJ.clear()
        world = _core.planet_run(_core.make_config(E.planet_config(ctx.cfg)), seed, 4, 4, False)
        for i, obj in enumerate(world, 1):
            E.put_part(ctx, i, obj)
        for item in selection:
            node = item['node']
            records, base, rain_signature = {}, None, None
            for tau in [0.]+args.times:
                g = generate(ctx, node, sets=[f'island.water.core_exchange_days={tau}',
                    f'island.water.core_footprint_scale={args.footprint_scale if tau else 0}'],
                    write=False, log=lambda _: None, **({'res_m': args.res_m} if args.res_m else {}))
                doc = rivers_doc(g)
                doc['res_m'] = g['res_km']*1000
                metrics = daily_scales(doc)
                signature = hashlib.sha256(g['rain_mm'].tobytes()).hexdigest()
                if base is None:
                    base, rain_signature = metrics, signature
                else:
                    assert signature == rain_signature, 'Rain changed in supply-only candidate'
                record = audit(doc, g, dict(seed=seed, node=node, exchange_days=tau, weather_year=0))
                land = g['island_id'] >= 0
                volume = float(g['condense_mm'][land].sum(dtype=float)*g['res_km']**2*1000)
                main = g['island_id'] == 0
                record['main_supply'] = dict(
                    core_m3s=float(g['condense_mm'][main].sum(dtype=float)*g['res_km']**2*1000/g['year_s']),
                    rain_runoff_m3s=float((g['runoff_mm'][main].astype(float)-g['condense_mm'][main]).sum()*g['res_km']**2*1000/g['year_s']))
                record['paired_budget'] = dict(condensation_m3_year=volume, ocean_evaporation_m3_year=volume,
                    latent_heat_to_island_MW=volume*1000*2.45e6/g['year_s']/1e6,
                    note='Equal prescribed source/sink obligations, NOT simulated ocean transport or proof of local heat balance.')
                record['compared_to_same_world_legacy'] = compare(base, metrics)
                records[str(tau)] = record
                result['nodes'][f'{seed}/{node}'] = records
                args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
                print(seed, node, tau, 'cond mm', record['water_mm']['condense_mm'],
                      'perennial', record['compared_to_same_world_legacy']['all_perennial'], flush=True)
                del g, doc, metrics
                gc.collect()


if __name__ == '__main__':
    main()
