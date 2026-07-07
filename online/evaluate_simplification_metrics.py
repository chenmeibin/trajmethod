import json
import math
import sys


def lonlat_to_meters(lon, lat):
    semimajoraxis = 6378137.0
    east = lon * 0.017453292519943295
    north = lat * 0.017453292519943295
    t = math.sin(north)
    return semimajoraxis * east, 3189068.5 * math.log((1 + t) / (1 - t))


def read_traj(path):
    traj = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            parts = line.strip().split()
            if len(parts) < 3:
                continue
            try:
                lat = float(parts[0])
                lon = float(parts[1])
                ts = float(parts[2])
            except ValueError:
                continue
            traj.append((lat, lon, ts))
    return traj


def project_traj(traj):
    projected = []
    for lat, lon, ts in traj:
        x, y = lonlat_to_meters(lon, lat)
        projected.append((x, y, ts))
    return projected


def match_kept_indices(original, simplified, time_tol=1e-6, coord_tol=1e-5):
    kept = []
    start_idx = 0
    for slat, slon, sts in simplified:
        matched = False
        for idx in range(start_idx, len(original)):
            olat, olon, ots = original[idx]
            if abs(ots - sts) <= time_tol and abs(olat - slat) <= coord_tol and abs(olon - slon) <= coord_tol:
                kept.append(idx)
                start_idx = idx + 1
                matched = True
                break
        if not matched:
            raise ValueError(
                f"Failed to align simplified point ({slat}, {slon}, {sts}) to original trajectory from index {start_idx}."
            )
    return kept


def point_sed(ps, pm, pe):
    denominator = pe[2] - ps[2]
    time_ratio = 1.0 if abs(denominator) < 1e-12 else (pm[2] - ps[2]) / denominator
    syn_x = ps[0] + (pe[0] - ps[0]) * time_ratio
    syn_y = ps[1] + (pe[1] - ps[1]) * time_ratio
    dx = syn_x - pm[0]
    dy = syn_y - pm[1]
    return math.hypot(dx, dy)


def point_ped(ps, pm, pe):
    a = pe[1] - ps[1]
    b = ps[0] - pe[0]
    c = pe[0] * ps[1] - ps[0] * pe[1]
    denom = math.hypot(a, b)
    if denom < 1e-12:
        return 0.0
    return abs((a * pm[0] + b * pm[1] + c) / denom)


def compute_metrics(original, simplified):
    if len(original) < 2:
        return {
            "input_points": len(original),
            "kept_points": len(simplified),
            "matched_points": len(simplified),
            "max_sed_m": 0.0,
            "mean_sed_m": 0.0,
            "max_ped_m": 0.0,
            "mean_ped_m": 0.0,
        }

    kept_indices = match_kept_indices(original, simplified)
    original_proj = project_traj(original)

    sed_errors = [0.0] * len(original_proj)
    ped_errors = [0.0] * len(original_proj)

    for left, right in zip(kept_indices, kept_indices[1:]):
        ps = original_proj[left]
        pe = original_proj[right]
        for idx in range(left + 1, right):
            pm = original_proj[idx]
            sed_errors[idx] = point_sed(ps, pm, pe)
            ped_errors[idx] = point_ped(ps, pm, pe)

    return {
        "input_points": len(original),
        "kept_points": len(simplified),
        "matched_points": len(kept_indices),
        "max_sed_m": max(sed_errors) if sed_errors else 0.0,
        "mean_sed_m": sum(sed_errors) / len(sed_errors) if sed_errors else 0.0,
        "max_ped_m": max(ped_errors) if ped_errors else 0.0,
        "mean_ped_m": sum(ped_errors) / len(ped_errors) if ped_errors else 0.0,
    }


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: py -3 evaluate_simplification_metrics.py <original_simple_traj> <simplified_output>")

    original = read_traj(sys.argv[1])
    simplified = read_traj(sys.argv[2])
    if not original:
        raise SystemExit("Original trajectory is empty.")
    if not simplified:
        raise SystemExit("Simplified trajectory is empty.")

    metrics = compute_metrics(original, simplified)
    print(json.dumps(metrics, ensure_ascii=True))


if __name__ == "__main__":
    main()
