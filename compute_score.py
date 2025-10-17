import math
from collections import defaultdict

def read_input_file(path):
    """Lit un fichier input et renvoie la topologie et la liste des flux."""
    with open(path, "r") as f:
        lines = [l.strip() for l in f.readlines() if l.strip()]
    
    M, N, FN, T = map(int, lines[0].split())
    idx = 1
    uavs = {}
    for _ in range(M * N):
        x, y, B, phi = lines[idx].split()
        uavs[(int(x), int(y))] = {"B": float(B), "phi": int(phi)}
        idx += 1
    
    flows = {}
    for i in range(FN):
        f, x, y, t_start, s, m1, n1, m2, n2 = map(int, lines[idx].split())
        flows[f] = {
            "access": (x, y),
            "t_start": t_start,
            "size": s,
            "range": (m1, n1, m2, n2)
        }
        idx += 1
    
    return {"M": M, "N": N, "FN": FN, "T": T, "uavs": uavs, "flows": flows}


def read_output_file(path):
    """Lit un fichier output de ton algo génétique."""
    flows_out = {}
    with open(path, "r") as f:
        lines = [l.strip() for l in f.readlines() if l.strip()]
    
    idx = 0
    while idx < len(lines):
        f, p = map(int, lines[idx].split())
        idx += 1
        records = []
        for _ in range(p):
            t, x, y, z = lines[idx].split()
            records.append((int(t), int(x), int(y), float(z)))
            idx += 1
        flows_out[f] = records
    return flows_out


def manhattan(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def compute_flow_score(flow_info, records):
    """Calcule le score d'un flux selon les formules du PDF."""
    s = flow_info["size"]
    access = flow_info["access"]
    t_start = flow_info["t_start"]
    if not records or s <= 0:
        return 0.0

    # 1. Total U2G Traffic Score
    total_sent = sum(z for _, _, _, z in records)
    u2g_score = min(1.0, total_sent / s)

    # 2. Traffic Delay Score (pondère plus les transferts précoces)
    # Le délai est calculé par rapport au t_start du flux
    delay_sum = sum((z / s) * (10 / ((t - t_start) + 10)) for t, _, _, z in records)
    delay_score = min(1.0, delay_sum)

    # 3. Transmission Distance Score
    distance_score = 0.0
    for t, x, y, z in records:
        h = manhattan(access, (x, y))
        distance_score += (z / s) * (2 ** (-0.1 * h))
    distance_score = min(1.0, distance_score)

    # 4. Landing Point Score
    landing_points = {(x, y) for _, x, y, _ in records}
    k = max(1, len(landing_points))
    landing_score = 1.0 / k

    total = 100 * (0.4 * u2g_score + 0.2 * delay_score +
                   0.3 * distance_score + 0.1 * landing_score)
    return total


def compute_total_score(input_file, output_file):
    """Calcule le score global."""
    inp = read_input_file(input_file)
    out = read_output_file(output_file)
    total_weighted = 0.0
    total_size = sum(f["size"] for f in inp["flows"].values())
    details = {}

    for f_id, flow in inp["flows"].items():
        score = compute_flow_score(flow, out.get(f_id, []))
        weight = flow["size"] / total_size
        total_weighted += score * weight
        details[f_id] = score

    return total_weighted, details


if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3:
        print("Usage: python compute_score.py <input_file> <output_file>")
        exit(1)
    
    total, details = compute_total_score(sys.argv[1], sys.argv[2])
    print(f"\n=== SCORE GLOBAL : {total:.3f} ===")
    #for f, sc in sorted(details.items()):
    #     print(f"Flow {f:3d}: {sc:.3f}")
    print()
