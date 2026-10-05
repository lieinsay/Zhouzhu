"""Read-only river-scale baseline; never changes generator inputs or world packs.

python -B tools/audit_river_scale.py --run build/river-preview/seed42 \
    --nodes 6329 6327 1165 6582 --out build/river-scale-baseline.json

Junction deficits are unexplained storage/loss requirements, NOT proof that water
is destroyed: the current export has no local storage, exchange or travel-time
state with which to close the daily balance. Implied Manning n is a diagnostic
under uniform-flow assumptions, NOT a fitted roughness or a validity threshold.
"""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path

import numpy as np


def quantiles(values, weights):
    valid = np.isfinite(values) & (weights > 0)
    if not valid.any():
        return None
    ix = np.argsort(values[valid])
    v, w = values[valid][ix], weights[valid][ix]
    return [float(v[min(len(v)-1, np.searchsorted(w.cumsum(), q*w.sum()))])
            for q in (0.1, 0.5, 0.9)]


def topology(doc):
    segs = doc['segments']
    starts = np.cumsum([0] + [len(s['pts']) for s in segs])
    by_id = {s['id']: i for i, s in enumerate(segs)}
    p = np.concatenate([np.asarray(s['pts'], dtype=float) for s in segs])
    n = len(p)
    down = np.full(n, -1, dtype=int)
    basin = np.empty(n, dtype=int)
    island = np.empty(n, dtype=int)
    for i, s in enumerate(segs):
        a, b = starts[i:i+2]
        down[a:b-1] = np.arange(a+1, b)
        if s['down'] >= 0:
            target = by_id[s['down']]
            if not 0 <= s['join'] < starts[target+1]-starts[target]:
                raise ValueError('Invalid junction index')
            down[b-1] = starts[target]+s['join']
        basin[a:b] = s['basin']
        island[a:b] = s['island']
    src = np.flatnonzero(down >= 0)
    dst = down[src]
    if np.any(basin[src] != basin[dst]) or np.any(island[src] != island[dst]):
        raise ValueError('Connected points disagree on basin or island')
    length = np.linalg.norm(p[src, :2]-p[dst, :2], axis=1)*doc['res_m']
    weights = np.zeros(n)
    np.add.at(weights, src, length/2)
    np.add.at(weights, dst, length/2)
    indegree = np.bincount(dst, minlength=n)
    left = indegree.copy()
    order = np.ones(n, dtype=int)
    upstream_max = np.zeros(n, dtype=int)
    maximum_count = np.zeros(n, dtype=int)
    queue = deque(np.flatnonzero(left == 0).tolist())
    seen = 0
    while queue:
        k = queue.popleft()
        seen += 1
        r = down[k]
        if r < 0:
            continue
        if order[k] > upstream_max[r]:
            upstream_max[r], maximum_count[r] = order[k], 1
        elif order[k] == upstream_max[r]:
            maximum_count[r] += 1
        left[r] -= 1
        if left[r] == 0:
            order[r] = upstream_max[r]+int(maximum_count[r] >= 2)
            queue.append(int(r))
    if seen != n:
        raise ValueError('River network contains a cycle')
    return p, down, basin, island, weights, order, indegree


def wetted_width(p, ix, b, f):
    ratio = np.divide(p[:, 4]*ix, p[:, 5], out=np.zeros(len(p)), where=p[:, 5] > 0)
    half = p[:, 6]/2
    depth = p[:, 7]*ratio**f
    extent = np.where(ratio <= 1, half*ratio**b, half+(depth-p[:, 7])/0.04)
    return np.minimum(extent, half+p[:, 17])+np.minimum(extent, half+p[:, 18]), depth


def audit(doc, grids, meta):
    if doc['cols'][3:10] != ['acc_km2', 'q_mean', 'q_bf', 'w_bf_m', 'd_bf_m', 'w_mean_m', 'd_mean_m']:
        raise ValueError('Unrecognized river columns')
    p, down, basin, island, weights, order, indegree = topology(doc)
    b, f = doc['width_exponent'], doc['depth_exponent']
    src = np.flatnonzero(down >= 0)
    dst = down[src]
    incoming = np.zeros((2, len(p)))
    for regime in (0, 1):
        pick = src[p[src, 20] == regime]
        np.add.at(incoming[regime], down[pick], p[pick, 4])
    basin_docs = {v['id']: v for v in doc['basins']}
    wet_width = np.full(len(p), np.nan)
    wet_depth = wet_width.copy()
    annual_width = wet_width.copy()
    dry = np.ones(len(p))
    deficit_days = np.zeros(len(p), dtype=int)
    deficit_peak = np.zeros(len(p))
    nd = 0
    for bid in np.unique(basin):
        bs = basin_docs[int(bid)]
        curves = np.asarray([bs['event_index'], bs['index']])
        nd = curves.shape[1]
        ids = np.flatnonzero(basin == bid)
        for regime in (0, 1):
            idx = ids[p[ids, 20] == regime]
            curve = curves[regime]
            for positive_only, result in ((True, wet_width), (False, annual_width)):
                vals = np.sort(curve[curve > 0] if positive_only else curve)
                if len(vals):
                    # Median of a nonlinear function, including an even day count.
                    lo, hi = vals[(len(vals)-1)//2], vals[len(vals)//2]
                    wl, dl = wetted_width(p[idx], lo, b, f)
                    wh, dh = wetted_width(p[idx], hi, b, f)
                    result[idx] = (wl+wh)/2
                    if positive_only:
                        wet_depth[idx] = (dl+dh)/2
            dry[idx] = np.where(p[idx, 4] > 0, np.mean(curve <= 0), 1.0)
            wet_width[idx[p[idx, 4] <= 0]] = np.nan
            wet_depth[idx[p[idx, 4] <= 0]] = np.nan
        for start in range(0, len(ids), 256):
            ix = ids[start:start+256]
            qin = incoming[0, ix, None]*curves[0]+incoming[1, ix, None]*curves[1]
            qout = p[ix, 4, None]*curves[p[ix, 20].astype(int)]
            deficit = qin-qout
            significant = deficit > np.maximum(2e-5, qin*0.01)
            deficit_days[ix] = significant.sum(axis=1)
            deficit_peak[ix] = np.maximum(0, np.where(significant, deficit, 0).max(axis=1))
    # Uniform-flow n required by the CURRENT width/depth/discharge/slope.
    # Exact power-section area; numerically integrated wetted perimeter.
    shape = f/b
    wm, dm = p[:, 8], p[:, 9]
    area = wm*dm*shape/(shape+1)
    velocity = np.divide(p[:, 4], area, out=np.full(len(p), np.nan), where=area > 0)
    u, qweights = np.polynomial.legendre.leggauss(32)
    u, qweights = (u+1)/2, qweights/2
    gradient = np.divide(2*dm*shape, wm, out=np.zeros(len(p)), where=wm > 0)
    perimeter = wm*(np.sqrt(1+gradient[:, None]**2*u[None, :]**(2*shape-2))@qweights)
    radius = np.divide(area, perimeter, out=np.zeros(len(p)), where=perimeter > 0)
    implied_n = np.divide(radius**(2/3)*np.sqrt(p[:, 12]), velocity,
                          out=np.full(len(p), np.nan), where=velocity > 0)
    def group(mask):
        w = weights[mask]
        return dict(points=int(mask.sum()), length_km=float(w.sum()/1000),
                    wet_day_width_p10_p50_p90_m=quantiles(wet_width[mask], w),
                    wet_day_center_depth_p10_p50_p90_m=quantiles(wet_depth[mask], w),
                    annual_median_width_p10_p50_p90_m=quantiles(annual_width[mask], w),
                    channel_width_p10_p50_p90_m=quantiles(p[mask, 6], w),
                    q_mean_p10_p50_p90_m3s=quantiles(p[mask, 4], w),
                    slope_p10_p50_p90=quantiles(p[mask, 12], w),
                    implied_uniform_flow_n_p10_p50_p90=quantiles(implied_n[mask], w),
                    dry_day_fraction=float(np.average(dry[mask], weights=w)) if w.sum() else None,
                    perennial_length_share=float(w[p[mask, 20] == 1].sum()/w.sum()) if w.sum() else None)
    land = grids['island_id'] >= 0
    main = grids['island_id'] == 0
    main_outlets = (down < 0) & (island == 0)
    cell_km2 = (doc['res_m']/1000)**2
    out_area = np.sort(p[main_outlets, 3])[::-1]
    main_area = float(main.sum()*cell_km2)
    transitions = np.zeros(len(p), dtype=bool)
    np.logical_or.at(transitions, dst, p[src, 20] != p[dst, 20])
    worst = np.argsort(deficit_peak)[-5:][::-1]
    return dict(meta=meta, points=len(p), segments=len(doc['segments']), days=nd,
                weighting='half of each source-point edge at each end, INCLUDING tributary connectors',
                main_km2=main_area, main_largest_basin_share=float(out_area[0]/main_area),
                main_top4_basin_share=float(out_area[:4].sum()/main_area),
                main_outlets_over_5km2=int(np.sum(out_area >= 5)),
                land_km2=float(land.sum()*cell_km2),
                water_mm={k: float(grids[k][land].mean()) for k in ['rain_mm', 'runoff_mm', 'condense_mm', 'recharge_mm']},
                main_perennial=group((island == 0) & (p[:, 20] == 1)),
                tributary_connectors=len(doc['segments'])-int(np.sum(down < 0)),
                slope_length_share={str(lo): float(weights[(p[:, 12] >= lo) & (p[:, 12] < hi)].sum()/weights.sum())
                                    for lo, hi in [(0, .015), (.015, .03), (.03, .1), (.1, np.inf)]},
                by_area={f'{lo:g}-{hi:g}': group((p[:, 3] >= lo) & (p[:, 3] < hi))
                         for lo, hi in [(.3, 1), (1, 10), (10, 50), (50, np.inf)]},
                by_strahler={str(k): group(order == k) for k in np.unique(order)},
                regime_transitions=int(transitions.sum()),
                transitions_with_unexplained_deficit=int(np.sum(transitions & (deficit_days > 0))),
                all_points_with_unexplained_deficit=int(np.sum(deficit_days > 0)),
                transition_deficit_days_p10_p50_p90=np.quantile(deficit_days[transitions], [.1, .5, .9]).tolist() if transitions.any() else [],
                daily_balance_note='Negative increments require unexported storage/loss/routing; not an annual missing-water estimate. Tolerance max(2e-5 m3/s, 1% inflow).',
                worst_daily_deficits=[dict(row=int(p[k, 0]), col=int(p[k, 1]), basin=int(basin[k]),
                    area_km2=float(p[k, 3]), peak_m3s=float(deficit_peak[k]), days=int(deficit_days[k])) for k in worst if deficit_peak[k] > 0])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run', type=Path, required=True)
    ap.add_argument('--nodes', type=int, nargs='+', required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    results = {}
    for node in args.nodes:
        root = args.run/'islands'/str(node)
        river_bytes = (root/'rivers.json').read_bytes()
        meta = json.loads((root/'island.json').read_text(encoding='utf-8'))['meta']
        meta['rivers_sha256'] = hashlib.sha256(river_bytes).hexdigest()
        with np.load(root/'terrain.npz') as grids:
            results[str(node)] = audit(json.loads(river_bytes), grids, meta)
        v = results[str(node)]
        print(node, 'transitions', v['regime_transitions'], 'unexplained', v['transitions_with_unexplained_deficit'],
              'widths', [q[1] if q else None for x in v['by_area'].values()
                         for q in [x['wet_day_width_p10_p50_p90_m']]], flush=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(results, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
