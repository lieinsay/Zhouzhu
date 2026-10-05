"""Isolated atmospheric supply audit, NOT a new condensation/river model.

The full transported model column crossing a main island's projected bounding
width is a generous availability envelope. It is NOT near-surface humidity,
cloud liquid water or collectable core yield. Required fractions below must not
be turned into capture efficiencies or selected to fit desired river widths.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import sys

import numpy as np


def signatures(data):
    result = {}
    for key, value in data.items():
        if isinstance(value, dict):
            result[key] = signatures(value)
        elif isinstance(value, np.ndarray):
            result[key] = dict(shape=list(value.shape), dtype=str(value.dtype),
                               sha256=hashlib.sha256(value.tobytes()).hexdigest())
        else:
            result[key] = value
    return result


def probe_node(cc, budget, g, lat, lon):
    def sample(key):
        return np.array([cc.grid_interp(np.ascontiguousarray(a),
                         float(budget['lats'][0]), float(np.diff(budget['lats'])[0]),
                         float(budget['lons'][0]), float(np.diff(budget['lons'])[0]), lat, lon)
                         for a in budget[key]])
    column, u, v = [sample(k) for k in ['water_column_mm', 'wind_u_ms', 'wind_v_ms']]
    speed = np.hypot(u, v)
    main = g['island_id'] == 0
    row, col = np.where(main)
    cell_m = g['res_km'] * 1000
    x, y = col*cell_m, -row*cell_m
    widths = np.zeros(len(speed))
    for s in range(len(speed)):
        if speed[s] > 0:
            # Project square land cells normal to seasonal wind. This is a
            # bounding width, including gaps; not a sum over repeated cells.
            a, b = -v[s]/speed[s], u[s]/speed[s]
            projected = a*x+b*y
            widths[s] = np.ptp(projected)+cell_m*(abs(a)+abs(b))
    envelope_m3s = column * speed * widths / 1000   # mm = kg/m2, water rho=1000
    to_q = g['res_km']**2 * 1000 / g['year_s']
    cond_q = float(g['condense_mm'][main].sum(dtype=float)*to_q)
    rain_q = float(g['rain_mm'][main].sum(dtype=float)*to_q)
    annual_envelope = float(envelope_m3s.mean())
    return dict(lat=lat, lon=lon, main_km2=float(main.sum()*g['res_km']**2),
                current_condensation_m3s=cond_q, all_main_rain_cap_m3s=rain_q,
                model_column_mm_by_season=column.tolist(), wind_ms_by_season=speed.tolist(),
                projected_main_width_m_by_season=widths.tolist(),
                full_column_envelope_m3s_by_season=envelope_m3s.tolist(),
                required_fraction_current=cond_q/annual_envelope if annual_envelope else None,
                required_fraction_all_land_rain_cap=rain_q/annual_envelope if annual_envelope else None,
                note='Annual screening bound only. Full column and full bounding width assumed accessible. Rain cap over all land is an extreme setting bound, NOT predicted capture or a proposed scenario.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runs', type=Path, default=Path('build/river-preview'))
    ap.add_argument('--samples', type=Path, default=Path('build/river-scale-study.json'))
    ap.add_argument('--baseline', type=Path, default=Path('build/river-moisture-before.json'))
    ap.add_argument('--out', type=Path, default=Path('build/river-moisture-study'))
    args = ap.parse_args()
    sys.path.insert(0, str(Path('extern/skyisle-gen').resolve()))
    from skyisle_gen.cli import _ctx_from_run
    from skyisle_gen.engine import core, planet_config
    from skyisle_gen.island import generate
    from skyisle_gen import _core

    samples = json.loads(args.samples.read_text(encoding='utf-8'))['samples']
    baseline = json.loads(args.baseline.read_text(encoding='utf-8')) if args.baseline.exists() else None
    args.out.mkdir(parents=True, exist_ok=True)
    cc = core()
    result = dict(native_sha256=hashlib.sha256(Path(_core.__file__).read_bytes()).hexdigest(),
                  baseline_checked=baseline is not None, seeds={}, nodes={})
    for seed_text, selection in samples.items():
        seed = int(seed_text)
        run = args.runs/f'seed{seed}'
        ctx = _ctx_from_run(str(run.resolve()))
        world = cc.planet_run(cc.make_config(planet_config(ctx.cfg)), seed, 4, 4, False)
        arrays = cc.climate_arrays(world[3])
        if baseline is not None:
            assert signatures(arrays) == baseline[seed_text], f'Existing climate output changed: {seed}'
        budget = cc.climate_moisture_arrays(world[3])
        np.savez_compressed(args.out/f'seed{seed}-moisture.npz', **budget)
        q, p, e = [budget[k] for k in ['water_column_mm', 'rain_flux_mm_day', 'source_flux_mm_day']]
        np.testing.assert_allclose(p, q*budget['rain_rate_per_day'], rtol=1e-13)
        weights = np.cos(np.radians(budget['lats']))[None, :, None]
        # At steady state, a closed globe has equal integrated E and P.
        # Report, do not silently normalize any numerical residual away.
        residual = ((e-p)*weights).sum(axis=(1, 2))/(e*weights).sum(axis=(1, 2))
        result['seeds'][seed_text] = dict(
            config_sha256=hashlib.sha256((run/'config.resolved.toml').read_bytes()).hexdigest(),
            global_source_minus_rain_fraction_by_season=residual.tolist(),
            column_mm_p10_p50_p90=np.quantile(q, [.1, .5, .9]).tolist(),
            year_days=budget['year_days_86400s'], original_climate_equal=True if baseline else None)
        islands = cc.islands_arrays(world[2])
        for item in selection:
            node = item['node']
            g = generate(ctx, node, write=False, log=lambda _: None)
            record = probe_node(cc, budget, g, float(islands['lat'][node]), float(islands['lon'][node]))
            result['nodes'][f'{seed}/{node}'] = record
            print(seed, node, 'required fraction: current / setting ceiling',
                  record['required_fraction_current'], record['required_fraction_all_land_rain_cap'], flush=True)
            del g
            gc.collect()
        (args.out/'report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
