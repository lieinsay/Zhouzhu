"""Reproducible, isolated scale study; native generation uses write=False.

No generator settings, preview products or game packs are modified. Selection
uses climate/size BEFORE rivers are generated, never favorable output widths.
Run with the existing skyisle virtual environment and Python -B.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

from audit_river_scale import audit, topology, quantiles


def select_nodes(run):
    with np.load(run/'s03_islands/islands.npz') as z, np.load(run/'s04_climate/climate_islands.npz') as c:
        chosen = []
        for name, lo, hi in [('small', 100, 500), ('large', 1000, 3000)]:
            ids = np.flatnonzero((z['main_area_km2'] >= lo) & (z['main_area_km2'] < hi)
                                 & (np.abs(z['lat']) >= 15) & (np.abs(z['lat']) <= 50))
            if len(ids) < 4:
                raise ValueError(f'Insufficient candidates for {name}')
            ids = ids[np.argsort(c['precip_mm'][ids], kind='stable')]
            for wetness, fraction in [('dry', .2), ('wet', .8)]:
                node = int(ids[round(fraction*(len(ids)-1))])
                chosen.append(dict(node=node, size=name, wetness=wetness, candidates=len(ids),
                                   selection_rain_mm=float(c['precip_mm'][node]),
                                   selection_main_km2=float(z['main_area_km2'][node])))
        return chosen


def groundwater_audit(g):
    land = g['island_id'] >= 0
    out = g['wt_outlet'] & land
    recharge = g['recharge_acc']
    total = float(np.sum(g['recharge_mm'][land], dtype=float)*g['res_km']**2)
    terminal = float(np.sum(recharge[out], dtype=float))
    masks = dict(perennial=g['river'] > 0,
                 temporary=(g['stream'] > 0) & (g['river'] == 0),
                 lake=g['lake'] & (g['river'] == 0) & (g['stream'] == 0),
                 other=(g['river'] == 0) & (g['stream'] == 0) & ~g['lake'])
    return dict(recharge_mean_m3s=total*1000/g['year_s'],
                terminal_relative_residual=(terminal-total)/total if total else 0,
                terminal_shares={k: float(recharge[out & mask].sum()/terminal) if terminal else 0 for k, mask in masks.items()},
                note='Existing groundwater routing endpoints, NOT new river supply or validated spring discharges. Surface river Q already includes the annual recharge component.')


def headwater_shape_probe(doc):
    """Literature sensitivity, not proposed production calibration.

Allen et al. 2018 DOI 10.1038/s41467-018-02991-w, equations 2-6,8.
Author code verifies A in m2 and ratio Wbf/Hbf, correcting the inverted label
in the published text. Only <=4.49 km2 (largest surveyed catchment) is probed.
Uniform-flow resistance may fail at shallow submergence/steps. No D84 data
exist independently of this generator's depth, so suitability is unresolved.
"""
    p, _, basin, _, weights, _, _ = topology(doc)
    bs = {v['id']: v for v in doc['basins']}
    ix = np.empty(len(p))
    for bid in np.unique(basin):
        for regime, key in [(0, 'event_index'), (1, 'index')]:
            vals = np.array(bs[int(bid)][key]); vals = vals[vals > 0]
            ix[(basin == bid) & (p[:, 20] == regime)] = np.median(vals) if len(vals) else 0
    # Same positive-day median DISCHARGE for every geometry in this experiment.
    pick = (p[:, 3] >= .3) & (p[:, 3] <= 4.49) & (p[:, 12] > 0) & (ix*p[:, 4] > 0)
    p, weights, ix = p[pick], weights[pick], ix[pick]
    q = p[:, 4]*ix
    within_current = q <= p[:, 5]
    p, weights, q = p[within_current], weights[within_current], q[within_current]
    if not len(p):
        return dict(points=0)
    old_w = p[:, 6]*(q/p[:, 5])**doc['width_exponent']
    old_d = p[:, 7]*(q/p[:, 5])**doc['depth_exponent']
    wbf = .008*(p[:, 3]*1e6)**.42
    hbf = wbf/14
    rows = []
    for r in [1, 2, 5, 10]:
        # Solve Q=(r/(r+1))*Wbf*(h/Hbf)^(1/r)*h^(5/3)*sqrt(S)/n.
        factor = (r/(r+1))*wbf*hbf**(-1/r)*np.sqrt(p[:, 12])/.04
        h = (q/factor)**(1/(5/3+1/r))
        w = wbf*(h/hbf)**(1/r)
        valid = h <= hbf
        reconstructed = w*h*r/(r+1)*h**(2/3)*np.sqrt(p[:, 12])/.04
        np.testing.assert_allclose(reconstructed, q, rtol=1e-11)
        rows.append(dict(shape_r=r, valid_length_share=float(weights[valid].sum()/weights.sum()),
                         width_ratio_p10_p50_p90=quantiles(w[valid]/old_w[valid], weights[valid]),
                         depth_ratio_p10_p50_p90=quantiles(h[valid]/old_d[valid], weights[valid]),
                         wider_length_share=float(weights[valid & (w > old_w)].sum()/weights[valid].sum()) if weights[valid].sum() else None))
    return dict(points=len(p), flow='same positive-day median discharge; below both reference banks',
                note='Literature sensitivity only. All r cases disclosed; n=0.04, W/H=14 and 0.008 A_m2^0.42 from paper, not fitted to desired widths. Not validated for these islands.',
                scenarios=rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runs', type=Path, default=Path('build/river-preview'))
    ap.add_argument('--generator', type=Path, default=Path('extern/skyisle-gen'))
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    sys.path.insert(0, str(args.generator.resolve()))
    from skyisle_gen.cli import _ctx_from_run
    from skyisle_gen.island import generate
    from skyisle_gen.island.rivernet import rivers_doc
    from skyisle_gen import _core
    samples = {42: [dict(node=n, selection='existing dry/wet references') for n in [6329, 6327, 1165, 6582]]}
    for seed in [7, 2026]:
        samples[seed] = select_nodes(args.runs/f'seed{seed}')
    results = dict(selection='For additional seeds: abs latitude 15-50; main area 100-500 or 1000-3000 km2; rain rank 20th/80th percentile in each size class. No selection on river output.',
                   native_extension_sha256=hashlib.sha256(Path(_core.__file__).read_bytes()).hexdigest(),
                   samples=samples, results={})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    for seed, rows in samples.items():
        run = args.runs/f'seed{seed}'
        ctx = _ctx_from_run(str(run.resolve()))
        config_hash = hashlib.sha256((run/'config.resolved.toml').read_bytes()).hexdigest()
        for selection in rows:
            node = selection['node']
            print('audit', seed, node, flush=True)
            g = generate(ctx, node, write=False, log=lambda _: None)
            doc = rivers_doc(g)
            doc['res_m'] = g['res_km']*1000
            meta = dict(g['json']['meta'], config_sha256=config_hash, weather_year=0)
            record = audit(doc, g, meta)
            record['groundwater'] = groundwater_audit(g)
            record['headwater_shape_probe'] = headwater_shape_probe(doc)
            results['results'][f'{seed}/{node}'] = record
            args.out.write_text(json.dumps(results, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
            print('done', seed, node, 'points', record['points'], flush=True)
            del g, doc
            gc.collect()


if __name__ == '__main__':
    main()
