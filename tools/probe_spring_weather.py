"""Replay actual generated weather through snow, groundwater and surface flow.

Explicit homogeneous K/Sy or a recorded layered case are material scenarios,
not adopted lithologies.
Start at a specified recharge equilibrium, optionally spin up a repeated
weather year, carry storage between years, and report initialization drift. No discharge renormalization,
river classification update, channel regeneration or game export is performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

import numpy as np

from audit_river_scale import quantiles
from probe_layered_aquifer import equilibrate


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run', type=Path, default=Path('build/river-preview/seed42'))
    ap.add_argument('--node', type=int, required=True)
    materials = ap.add_mutually_exclusive_group(required=True)
    materials.add_argument('--k-m-day', type=float)
    materials.add_argument('--layered-study', type=Path,
        help='Reuse an explicit material case from probe_layered_aquifer output')
    ap.add_argument('--layered-case', type=int, default=0)
    ap.add_argument('--sy', type=float)
    ap.add_argument('--res-m', type=float, default=300.)
    ap.add_argument('--exchange-days', type=float, default=0.)
    ap.add_argument('--footprint-scale', type=float, default=0.)
    ap.add_argument('--first-year', type=int, default=-1)
    ap.add_argument('--years', type=int, default=2)
    ap.add_argument('--initial-recharge', choices=['core', 'total'], default='total')
    ap.add_argument('--spinup-cycles', type=int, default=0)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if args.years < 1:
        ap.error('At least one year is required')
    if args.k_m_day is not None and args.sy is None:
        ap.error('The homogeneous material requires --sy')
    if args.layered_study and args.sy is not None:
        ap.error('Layered storage comes from the recorded material case, not --sy')
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
    sets = [f'island.water.core_exchange_days={args.exchange_days}',
            f'island.water.core_footprint_scale={args.footprint_scale}']
    g = generate(ctx, args.node, res_m=args.res_m, steps=4, write=False, log=lambda _: None, sets=sets)
    inp = IE.inputs(ctx, args.node, g['inp'])
    rain_cfg = ctx.cfg['island']['rivers']
    main_land = g['island_id'] == 0
    rows, cols = np.where(main_land)
    r0, c0, r1, c1 = rows.min(), cols.min(), rows.max()+1, cols.max()+1
    sl = np.s_[r0:r1, c0:c1]
    mask = np.ascontiguousarray(main_land[sl])
    top = np.ascontiguousarray(g['height'][sl], dtype=float)
    base = np.ascontiguousarray(g['skel_top'][sl], dtype=float)
    shape = mask.shape
    cond = np.ascontiguousarray(g['condense_mm'][sl]/(1000*g['year_s']), dtype=float)
    bfi = np.ascontiguousarray(g['bfi'][sl], dtype=float)
    p_mean = inp['precip_mm_ref']*np.clip(inp['precip'], 0, 1)
    pscale = np.ascontiguousarray(g['rain_mm'][sl]/p_mean, dtype=float) if p_mean > 0 else np.zeros(shape)
    annual_rain_yield = np.maximum(0., g['runoff_mm'][sl].astype(float)-g['condense_mm'][sl])
    fraction = np.divide(annual_rain_yield, g['rain_mm'][sl], out=np.zeros(shape), where=g['rain_mm'][sl] > 0)
    fraction = np.ascontiguousarray(np.clip(fraction, 0, 1))  # float32 export roundoff
    offset = np.ascontiguousarray(inp['lapse_c_per_km']*(inp['height_m']-top)/1000.)
    ri, rj = g['recv_i'][sl]-r0, g['recv_j'][sl]-c0
    valid = mask & (ri >= 0) & (ri < shape[0]) & (rj >= 0) & (rj < shape[1])
    receiver = np.full(shape, -1, dtype=np.int64)
    receiver[valid] = ri[valid]*shape[1]+rj[valid]
    area = g['flowacc_km2'][sl]
    channel = mask & ((g['river'][sl] > 0) | (g['stream'][sl] > 0)) & ~g['lake'][sl].astype(bool)
    rr, cc = np.indices(shape)
    length = np.where(valid & (ri != rr) & (rj != cc), np.sqrt(2), 1)*args.res_m
    layered, material_record = None, None
    if args.layered_study:
        study = json.loads(args.layered_study.read_text(encoding='utf-8'))
        if study['seed'] != ctx.seed or study['res_m'] != args.res_m:
            raise ValueError('Layered study seed/resolution mismatch')
        material_record = study['nodes'][str(args.node)][args.layered_case]
        if not material_record['converged']:
            raise ValueError('Unconverged material case cannot initialize a replay')
        layered = _core.make_layered_aquifer(top,
            np.ascontiguousarray(g['strat_top'][sl], dtype=float), base, mask, g['strat_raw'][0],
            material_record['material_horizontal_ms'], material_record['material_vertical_ms'],
            material_record['specific_yield'], material_record['specific_storage_per_m'], args.res_m,
            **material_record['shallow_profile'])
        head_pick = np.ones(len(layered.node_cells), dtype=bool)
    else:
        km, sy = np.full(shape, args.k_m_day/86400.), np.full(shape, args.sy)
        head_pick = mask
    initial_recharge = cond.copy()
    initial_snow = np.zeros(shape)
    if args.initial_recharge == 'total':
        if args.spinup_cycles:
            # Equilibrate to the actual repeated year's recharge, not the
            # climatological annual rain (which can differ from this year).
            IE.weather_year_cpp(ctx, args.node, ctx.cfg['island'], g, year=args.first_year-1, log=lambda _: None)
            warm_weather = g['weather']['arrays']
            warm_dt = g['year_s']/len(warm_weather['precip_mm'])
            for cycle in range(2):
                mean_rain = np.zeros(shape)
                for p, t in zip(warm_weather['precip_mm'], warm_weather['temp_c']):
                    water = _core.spring_rain_step(pscale, fraction, offset, initial_snow, mask,
                        float(p), float(t), args.res_m, warm_dt,
                        rain_cfg['snow_t_c'], rain_cfg['melt_t_c'], rain_cfg['ddf_mm'])
                    initial_snow = water['snowpack_mm']
                    mean_rain += water['liquid_runoff_ms']/len(warm_weather['precip_mm'])
            initial_recharge += mean_rain*bfi
        else:
            initial_recharge += annual_rain_yield*bfi/(1000*g['year_s'])
    initial = equilibrate(layered, initial_recharge, label=f'{args.node} initial') if layered else \
        _core.solve_aquifer_balance(top, base, km, initial_recharge, mask, args.res_m)
    if not initial['converged']:
        raise RuntimeError('Initial recharge equilibrium did not converge')
    head, snow = initial['head_m'], initial_snow
    report = dict(status='actual weather replay with explicit material scenario; no adopted river geometry',
        args={k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
        config_sha256=hashlib.sha256((args.run/'config.resolved.toml').read_bytes()).hexdigest(),
        native_sha256=hashlib.sha256(Path(_core.__file__).read_bytes()).hexdigest(),
        material_case=material_record, spinup=[], spinup_converged=None, years=[])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    runs = [(args.first_year-1, True)]*args.spinup_cycles + [(y, False) for y in range(args.first_year, args.first_year+args.years)]
    previous_cycle_q = None
    for year, is_spinup in runs:
        if is_spinup and report['spinup_converged']:
            continue
        IE.weather_year_cpp(ctx, args.node, ctx.cfg['island'], g, year=year, log=lambda _: None)
        weather = g['weather']['arrays']
        nd = len(weather['precip_mm'])
        dt = g['year_s']/nd
        day_q = np.zeros((nd, np.count_nonzero(channel)))
        numerical_floor = np.zeros(nd)
        total = dict(precipitation_m3=0., core_m3=0., nonrunoff_m3=0., snow_change_m3=0.,
                     groundwater_change_m3=0., surface_outlet_m3=0., coast_m3=0., absolute_cell_error_m3=0.)
        start_head, start_snow, started = head.copy(), snow.copy(), time.perf_counter()
        for day in range(nd):
            water = _core.spring_rain_step(pscale, fraction, offset, snow, mask,
                float(weather['precip_mm'][day]), float(weather['temp_c'][day]), args.res_m, dt,
                rain_cfg['snow_t_c'], rain_cfg['melt_t_c'], rain_cfg['ddf_mm'])
            if layered:
                rain = water['liquid_runoff_ms']
                aq = layered.step(np.ascontiguousarray(cond+rain*bfi), head, dt)
                if not aq['converged']:
                    raise RuntimeError(f'Layered daily state did not converge: year {year}, day {day}')
                routed = _core.route_surface_water(np.ascontiguousarray(aq['surface_m3s']+
                    rain*(1-bfi)*args.res_m**2), mask, receiver.ravel())
                routed['core_input_m3s'] = float(cond[mask].sum()*args.res_m**2)
                routed['surface_outlet_m3s'] = routed['outlet_m3s']
            else:
                routed = _core.step_spring_routing(top, base, km, water['liquid_runoff_ms'], cond,
                    bfi, mask, receiver.ravel(), head, sy, args.res_m, dt)
                aq = routed['aquifer']
            numerical_floor[day] = max(1e-12, float(np.abs(aq['residual_m3s']).sum()))
            day_q[day] = routed['river_m3s'][channel]
            head, snow = aq['head_m'], water['snowpack_mm']
            total['precipitation_m3'] += water['precipitation_m3']
            total['nonrunoff_m3'] += water['nonrunoff_m3']
            total['snow_change_m3'] += water['snow_storage_change_m3']
            total['groundwater_change_m3'] += aq['total_storage_change_m3']
            total['core_m3'] += routed['core_input_m3s']*dt
            total['surface_outlet_m3'] += routed['surface_outlet_m3s']*dt
            total['coast_m3'] += float(aq['coast_m3s'].sum())*dt
            total['absolute_cell_error_m3'] += float(np.abs(aq['residual_m3s']).sum())*dt
            if (day+1) % 84 == 0:
                print(args.node, 'spinup' if is_spinup else 'year',
                      len(report['spinup'])+1 if is_spinup else year, 'day', day+1,
                      'seconds', round(time.perf_counter()-started, 1), flush=True)
        supply = total['precipitation_m3']+total['core_m3']
        accounted = sum(total[k] for k in ['nonrunoff_m3', 'snow_change_m3', 'groundwater_change_m3', 'surface_outlet_m3', 'coast_m3'])
        error = supply-accounted
        if abs(error) > total['absolute_cell_error_m3']+max(1e-5, supply*1e-10):
            raise RuntimeError('Combined weather/groundwater/river ledger did not close')
        groups = {}
        qmed, qmean = np.median(day_q, axis=0), day_q.mean(axis=0)
        # A positive discharge below the whole-solve absolute residual is not
        # resolved evidence of perennial flow. This floor is numerical only.
        resolved_continuous = np.all(day_q > numerical_floor[:, None], axis=0)
        weights, areas = length[channel], area[channel]
        for lo, hi in [(.3, 1), (1, 10), (10, 50), (50, np.inf)]:
            sel = (areas >= lo) & (areas < hi)
            den = weights[sel].sum()
            groups[f'{lo:g}-{hi:g}'] = dict(length_km=float(den/1000),
                continuously_supplied_share=float(weights[sel & resolved_continuous].sum()/den) if den else None,
                annual_median_flow_m3s=quantiles(qmed[sel], weights[sel]) if den else None,
                annual_mean_flow_m3s=quantiles(qmean[sel], weights[sel]) if den else None)
        record = dict(year=year, weather_precip_mm=float(np.sum(weather['precip_mm'])),
            seconds=time.perf_counter()-started, budget=total, relative_balance_error=error/max(1., supply),
            numerical_flow_floor_max_m3s=float(numerical_floor.max()),
            head_change_p10_p50_p90_m=np.quantile((head-start_head)[head_pick], [.1, .5, .9]).tolist(),
            stored_snow_end_m3=float(snow[mask].sum()*args.res_m**2/1000), per_area=groups)
        if is_spinup:
            head_error = float(np.max(np.abs((head-start_head)[head_pick])))
            snow_error = float(np.max(np.abs((snow-start_snow)[mask])))
            q_error = float(np.max(np.abs(day_q-previous_cycle_q))/max(1e-12, day_q.max())) if previous_cycle_q is not None else None
            # Numerical initialization criteria, unrelated to any target width.
            converged = head_error < .01 and snow_error < .01 and q_error is not None and q_error < 1e-4
            record.update(max_cycle_head_change_m=head_error, max_cycle_snow_change_mm=snow_error,
                          relative_cycle_q_change=q_error, converged=converged)
            report['spinup'].append(record)
            report['spinup_converged'] = converged
            previous_cycle_q = day_q
            args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
            print(args.node, 'spinup', len(report['spinup']), 'head', head_error, 'snow', snow_error, 'q', q_error, flush=True)
            continue
        report['years'].append(record)
        args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
        np.savez_compressed(args.out.with_name(args.out.stem+f'-y{year}.npz'), q_m3s=day_q,
            cell=np.flatnonzero(channel), shape=shape, crop_origin_rc=[r0, c0], head=head, snow_mm=snow,
            numerical_flow_floor_m3s=numerical_floor)
        print('completed', args.node, year, 'relative error', record['relative_balance_error'], groups, flush=True)


if __name__ == '__main__':
    main()
