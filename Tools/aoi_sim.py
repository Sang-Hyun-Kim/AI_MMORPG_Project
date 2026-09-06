"""AOI Uniform Grid — 동치성 및 개선폭 시뮬레이션 (2026-09-06 [AOI-1] 재현)
GameRoom.cpp 의 격자 로직을 그대로 옮겨 전체 순회와 대조한다."""
import math, random

CELL_SIZE, AOI_RADIUS = 1000.0, 2000.0
CELL_SPAN = math.ceil(AOI_RADIUS / CELL_SIZE)          # = 2

def sector_coord(v):  return math.floor(v / CELL_SIZE)
def make_key(sx, sy): return (sx << 32) | (sy & 0xFFFFFFFF)

def grid_query(px, py, players, sectors, stat):
    """GetAdjacentSectorPlayers 재현. 반환: id 집합"""
    out, r2 = set(), AOI_RADIUS * AOI_RADIUS
    cx, cy = sector_coord(px), sector_coord(py)
    for sx in range(cx - CELL_SPAN, cx + CELL_SPAN + 1):
        for sy in range(cy - CELL_SPAN, cy + CELL_SPAN + 1):
            stat['cells'] += 1
            bucket = sectors.get(make_key(sx, sy))
            if bucket is None: continue
            for oid in bucket:
                ox, oy = players[oid]
                dx, dy = ox - px, oy - py
                stat['grid'] += 1
                if dx*dx + dy*dy <= r2: out.add(oid)
    return out

def naive_query(px, py, players, stat):
    out, r2 = set(), AOI_RADIUS * AOI_RADIUS
    for oid, (ox, oy) in players.items():
        dx, dy = ox - px, oy - py
        stat['naive'] += 1
        if dx*dx + dy*dy <= r2: out.add(oid)
    return out

def run(n, world, queries=400, shape='square', seed=None):
    rnd = random.Random(seed)
    players = {}
    for i in range(n):
        if shape == 'square':
            players[i] = (rnd.uniform(-world, world), rnd.uniform(-world, world))
        else:                                            # disc
            a, r = rnd.uniform(0, 2*math.pi), world*math.sqrt(rnd.random())
            players[i] = (r*math.cos(a), r*math.sin(a))
    sectors = {}
    for oid, (x, y) in players.items():
        sectors.setdefault(make_key(sector_coord(x), sector_coord(y)), set()).add(oid)

    stat = {'grid': 0, 'naive': 0, 'cells': 0}
    mismatch = 0
    for _ in range(queries):
        px, py = players[rnd.randrange(n)]               # 이동한 본인 위치에서 질의
        if grid_query(px, py, players, sectors, stat) != naive_query(px, py, players, stat):
            mismatch += 1
    return stat['grid']/queries, stat['naive']/queries, mismatch

if __name__ == '__main__':
    CONFIGS = [(10,3000),(10,10000),(50,10000),(100,10000),(500,20000),(1000,30000)]
    PUBLISHED = [4.3, 1.2, 3.2, 6.2, 8.3, 7.5]
    for shape in ('square','disc'):
        print(f"\n=== 배치 = {shape} · 질의 400회 · 시드 20회 평균 ===")
        print(f"{'인원':>6}{'월드':>8}{'그리드':>9}{'전체순회':>10}{'감소':>9}{'불일치':>7}   발표값  차이")
        for (n,w),pub in zip(CONFIGS,PUBLISHED):
            gs=[];ns=[];mm=0
            for s in range(20):
                g,nv,m = run(n,w,seed=s,shape=shape); gs.append(g);ns.append(nv);mm+=m
            g=sum(gs)/len(gs); nv=sum(ns)/len(ns)
            print(f"{n:>6}{w:>8}{g:>9.2f}{nv:>10.1f}{nv/g:>8.2f}x{mm:>7}   {pub:>5}  {g-pub:+.2f}")
