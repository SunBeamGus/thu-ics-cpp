"""列出 Overpass 结果里与教学楼/场馆相关的候选，写成 UTF-8 文本供人工核对。"""
import json
import sys

KEYWORDS = ["教学楼", "主楼", "学堂", "技科", "技术科学", "蒙民伟", "建筑馆", "明理",
            "体育馆", "操场", "图书馆", "逸夫", "三教", "六教", "一教", "二教", "四教", "五教"]


def main():
    source, target = sys.argv[1], sys.argv[2]
    with open(source, encoding="utf-8") as handle:
        data = json.load(handle)

    rows = []
    for element in data.get("elements", []):
        tags = element.get("tags") or {}
        name = (tags.get("name") or "").strip()
        if not name:
            continue
        if not any(keyword in name for keyword in KEYWORDS):
            continue
        center = element.get("center") or {}
        lat, lon = center.get("lat"), center.get("lon")
        if lat is None or lon is None:
            continue
        rows.append((name, float(lat), float(lon), element.get("id"),
                     tags.get("building", ""), tags.get("amenity", ""), tags.get("leisure", "")))

    rows.sort(key=lambda item: item[0])
    with open(target, "w", encoding="utf-8", newline="\n") as handle:
        for name, lat, lon, element_id, building, amenity, leisure in rows:
            handle.write("%s\t%.7f\t%.7f\tid=%s\tbuilding=%s/amenity=%s/leisure=%s\n" %
                         (name, lat, lon, element_id, building, amenity, leisure))
    print("wrote %d candidates to %s" % (len(rows), target))


if __name__ == "__main__":
    main()
