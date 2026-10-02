"""Audit exported v7 river geometry without generating or editing world data.

python tools/audit_river_geometry.py --root game/planet/seed42/groups \
    --before build/river-model-before --nodes 6329 6327 --out build/river-model-audit.json
Positive-day medians and all-year dry fractions are reported separately.
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np


def read_pack(path):
    data = path.read_bytes()
    magic, version, h, w, res, x0, z0 = struct.unpack_from('<4sIIIddd', data)
    if magic != b'ZZWG' or version != 7:
        raise ValueError(f'{path}: expected full v7 terrain pack')
    count = h*w
    def raster(offset, dtype):
        return np.frombuffer(data, dtype, count, 40+count*offset)
    grids = {k:raster(o,d) for k,o,d in [('shore',4,'<f4'),('island',8,'u1'),
             ('rain',42,'<f4'),('runoff',46,'<f4'),('condense',58,'<f4')]}
    at = 40+count*66
    n, = struct.unpack_from('<I',data,at); at += 4
    segments = []
    for _ in range(n):
        island,down,join,exit_,basin,size = struct.unpack_from('<iiiiiI',data,at); at += 24
        pts = np.frombuffer(data,'<f4',size*21,at).reshape(size,21).astype(float); at += size*21*4
        segments.append((island,down,join,exit_,basin,pts))
    n, = struct.unpack_from('<I',data,at); at += 4
    curves = []
    for _ in range(n):
        size, = struct.unpack_from('<I',data,at); at += 4
        curves.append(np.frombuffer(data,'<f4',size,at).astype(float)); at += size*4
    return dict(segments=segments,curves=curves,res=res,x0=x0,z0=z0,grids=grids)


def weighted_median(values, weights):
    valid = np.isfinite(values)&(weights>0)
    if not valid.any():
        return None
    order = np.argsort(values[valid]); v=values[valid][order]; w=weights[valid][order]
    return float(v[np.searchsorted(w.cumsum(),w.sum()/2)])


def audit(pack, require_closure=False):
    points=[]; weights=[]; widths=[]; depths=[]; dry=[]; outlets=[]
    curves=pack['curves']; offset=len(curves)//2
    for island,down,join,exit_,basin,p in pack['segments']:
        length=np.hypot(*np.diff(p[:,:2],axis=0).T)*pack['res']
        weight=np.zeros(len(p)); weight[:-1]+=length/2; weight[1:]+=length/2
        ix=np.array([curves[basin+(offset if r==0 else 0)] for r in p[:,20]])
        q=p[:,4,None]*ix
        flow=np.divide(q,p[:,5,None],out=np.zeros_like(q),where=p[:,5,None]>0)
        depth=p[:,7,None]*flow**0.4; half=p[:,6,None]/2
        extent=np.where(flow<=1,half*flow**0.26,half+(depth-p[:,7,None])/0.04)
        width=np.minimum(extent,half+p[:,17,None])+np.minimum(extent,half+p[:,18,None])
        positive=q>0
        # Zero-flow reaches retain NaN medians and a separate dry-day count.
        wm=np.array([np.median(v[m]) if m.any() else np.nan for v,m in zip(width,positive)])
        dm=np.array([np.median(v[m]) if m.any() else np.nan for v,m in zip(depth,positive)])
        points.append(p);weights.append(weight);widths.append(wm);depths.append(dm);dry.append((~positive).mean(axis=1))
        if down<0:
            outlets.append(dict(island=island,area_km2=p[-1,3],q_mean=p[-1,4],
                width=float(wm[-1]) if np.isfinite(wm[-1]) else None,
                depth=float(dm[-1]) if np.isfinite(dm[-1]) else None))
    p=np.concatenate(points); weight=np.concatenate(weights); wm=np.concatenate(widths); dm=np.concatenate(depths); dry=np.concatenate(dry)
    closure=np.abs(p[:,10]-p[:,11]-p[:,7])
    if require_closure:
        assert closure.max()<0.003, ('bed/depth/surface',closure.max())
        # Export rounds hydraulic quantities to 1e-6 before float32 storage.
        assert np.allclose(p[:,5],5*p[:,4],atol=3.1e-6,rtol=1e-6), 'reference discharge'
        assert np.allclose(p[:,6]/5**0.26,p[:,8],atol=1e-6,rtol=1e-6), 'reference/mean width'
        assert np.allclose(p[:,7]/5**0.4,p[:,9],atol=1e-6,rtol=1e-6), 'reference/mean depth'
        assert all(np.all(x>=0) and np.isfinite(x).all() and (not x.any() or abs(x.mean()-1)<1e-6) for x in curves)
    bins=[]
    for lo,hi in [(0.3,1),(1,10),(10,50),(50,float('inf'))]:
        mask=(p[:,3]>=lo)&(p[:,3]<hi)
        bins.append(dict(area_km2=f'{lo:g}–{hi:g}',points=int(mask.sum()),length_km=float(weight[mask].sum()/1000),
            wet_day_median_width_m=weighted_median(wm[mask],weight[mask]),
            wet_day_median_center_depth_m=weighted_median(dm[mask],weight[mask]),
            length_weighted_dry_fraction=float(np.average(dry[mask],weights=weight[mask])) if weight[mask].sum() else None))
    grids=pack['grids'];land=grids['shore']>=0;main=land&(grids['island']==0)
    nearest=int(np.argmin((pack['x0']+p[:,1]*pack['res']+1120)**2+(pack['z0']+p[:,0]*pack['res']+920)**2))
    q=p[nearest]
    return dict(points=len(p),segments=len(pack['segments']),land_km2=float(land.sum()*pack['res']**2/1e6),
        main_km2=float(main.sum()*pack['res']**2/1e6),
        rain_mm=float(grids['rain'][land].mean()),runoff_mm=float(grids['runoff'][land].mean()),
        condense_mm=float(grids['condense'][land].mean()),
        bank_closure_max_m=float(closure.max()),bank_closure_median_m=float(np.median(closure)),
        perennial_length_km=float(weight[p[:,20]==1].sum()/1000),
        bins=bins,largest_main_outlets=sorted([o for o in outlets if o['island']==0],key=lambda x:-x['area_km2'])[:6],
        near_original=dict(x=pack['x0']+q[1]*pack['res'],z=pack['z0']+q[0]*pack['res'],area_km2=q[3],
            q_mean=q[4],width_at_mean_flow=q[8],center_depth_at_mean_flow=q[9],slope=q[12]))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--before',type=Path)
    parser.add_argument('--nodes',type=int,nargs='+',required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args(); report={}
    for node in args.nodes:
        current=read_pack(args.root/str(node)/'terrain.bin')
        record={'after':audit(current,True)}
        if args.before:
            previous=read_pack(args.before/str(node)/'terrain.bin')
            record['before']=audit(previous)
            record['unchanged_water_inputs']={k:bool(np.array_equal(previous['grids'][k],current['grids'][k],equal_nan=True)) for k in ['rain','runoff','condense']}
            a=np.concatenate([s[-1] for s in previous['segments']]);b=np.concatenate([s[-1] for s in current['segments']])
            record['unchanged_network_and_supply']=bool(a.shape==b.shape and np.array_equal(a[:,[0,1,2,3,4,20]],b[:,[0,1,2,3,4,20]]))
            assert all(record['unchanged_water_inputs'].values())
            assert record['unchanged_network_and_supply']
        report[str(node)]=record
        print(node,json.dumps(record['after']['bins'],ensure_ascii=False),flush=True)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
