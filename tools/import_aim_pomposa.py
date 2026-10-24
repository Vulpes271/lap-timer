"""Extract factual Pomposa long-track timing coordinates from AiM's public export."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
import io

URL = "https://aim-sportline.com/aim-software-betas/Software/Applications/GPSManager/release/export_rs3_tracks_Europe.zwd"
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="Use an already downloaded .zwd")
    args = parser.parse_args()
    if args.archive:
        data = args.archive.read_bytes()
    else:
        request = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        root = ET.fromstring(archive.read("gps_track_conf.mpl"))
        entries = [e for e in root.iter("mpk_info") if e.findtext("SZ_MPK_Name") == "PomposaMotoL"]
        if len(entries) != 1:
            raise ValueError("Expected exactly one AiM PomposaMotoL record")
        entry = entries[0]
        marker = entry.find("line_info")
        lat = float(marker.findtext("L_POS_X_LLH"))
        lon = float(marker.findtext("L_POS_Y_LLH"))
        shape_name = entry.findtext("SZ_TRACK_FILE_KML")
        shape = archive.read(shape_name)
        # Legacy KMB: 256-byte header, then signed little-endian lat/lon * 1e7.
        if (len(shape) - 256) % 8:
            raise ValueError("Unexpected KMB coordinate layout")
        points = [(a / 1e7, b / 1e7) for a, b in struct.iter_unpack("<ii", shape[256:])]
        coslat = math.cos(math.radians(lat))
        def local(p):
            return ((p[1] - lon) * 111320 * coslat, (p[0] - lat) * 111320)
        if not all(44.70 < a < 44.72 and 12.20 < b < 12.23 for a, b in points):
            raise ValueError("KMB is not the expected Pomposa location")
        index = min(range(1, len(points) - 1), key=lambda i: math.hypot(*local(points[i])))
        before, after = local(points[index - 1]), local(points[index + 1])
        east, north = after[0] - before[0], after[1] - before[1]
        length = math.hypot(east, north)
        if length < 5 or math.hypot(*local(points[index])) > 10:
            raise ValueError("Cannot derive a reliable local tangent at the lap marker")
        east, north = east / length, north / length
        # Project sectors: thirds of the downloaded driven line, not AiM splits.
        # Project the lap marker onto its nearest segment, then walk forward once.
        xy = [local(p) for p in points]
        candidates = []
        for i in range(len(xy)):
            a, b = xy[i], xy[(i + 1) % len(xy)]
            dx, dy = b[0] - a[0], b[1] - a[1]
            norm = dx * dx + dy * dy
            if norm == 0:
                continue
            t = max(0, min(1, -(a[0] * dx + a[1] * dy) / norm))
            p = (a[0] + t * dx, a[1] + t * dy)
            candidates.append((math.hypot(*p), i, p))
        _, start, origin = min(candidates)
        ordered = [origin] + [xy[(start + j) % len(xy)] for j in range(1, len(xy) + 1)] + [origin]
        lengths = [math.dist(a, b) for a, b in zip(ordered, ordered[1:])]
        total = sum(lengths)
        splits = []
        for fraction in (1 / 3, 2 / 3):
            target, distance = total * fraction, 0
            for i, segment_length in enumerate(lengths):
                if segment_length and distance + segment_length >= target:
                    t = (target - distance) / segment_length
                    a, b = ordered[i], ordered[i + 1]
                    x, y = a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])
                    splits.append({"latitude": lat + y / 111320,
                                   "longitude": lon + x / (111320 * coslat),
                                   "forward_east": (b[0] - a[0]) / segment_length,
                                   "forward_north": (b[1] - a[1]) / segment_length,
                                   "distance_from_start_m": target})
                    break
                distance += segment_length
        record = {
            "name": "Pomposa Long", "aim_export_name": "PomposaMotoL",
            "source_url": URL, "archive_sha256": hashlib.sha256(data).hexdigest(),
            "archive_entry_date": list(archive.getinfo("gps_track_conf.mpl").date_time),
            "aim_week_number": int(entry.findtext("US_GPS_Week_Nr")),
            "start_finish": {"latitude": lat, "longitude": lon},
            "forward_unit": {"east": east, "north": north},
            "direction_method": "Tangent from ordered legacy KMB points next to the AiM lap marker",
            "tangent_points": points[index - 1:index + 2],
            "gate_half_width_m": 20, "gate_width_source": "Project policy; not supplied by AiM",
            "sector_method": "Project-defined thirds of ordered AiM driven line; not official AiM split points",
            "driven_line_length_m": total,
            "splits": splits,
            "current_solo2_name_match_verified": False,
            "note": "Historical public AiM long motorcycle profile. Verify against the current Solo 2 Pomposa Long export before comparing lap times.",
        }
    (ROOT / "tracks").mkdir(exist_ok=True)
    (ROOT / "tracks/pomposa_long.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    split_rows = ",\n".join(
        f"    {{{s['latitude']:.9f}, {s['longitude']:.9f}, {s['forward_east']:.12f}, {s['forward_north']:.12f}, 20.0, 15.0}}"
        for s in splits)
    header = f'''#pragma once
// Generated by tools/import_aim_pomposa.py. See tracks/pomposa_long.json for provenance.
// Historical AiM PomposaMotoL; current Solo 2 name equivalence is not verified.
#include "track_config.h"
namespace Tracks {{
// Custom sector boundaries at one third and two thirds of the legacy driven line.
constexpr Timing::Gate PomposaSplits[] = {{
{split_rows}
}};
constexpr Timing::TrackConfig PomposaLong = {{
    "Pomposa Long", "PomposaMotoL", {lat:.9f}, {lon:.9f},
    {east:.12f}, {north:.12f}, 20.0, 15.0, 10.0, 30000, 600000,
    PomposaSplits, 2
}};
}}
'''
    (ROOT / "include/pomposa_track.h").write_text(header, encoding="ascii")
    print(f"Imported {record['aim_export_name']}: {lat:.6f}, {lon:.6f}, heading {math.degrees(math.atan2(east,north)) % 360:.2f} deg")


if __name__ == "__main__":
    main()
