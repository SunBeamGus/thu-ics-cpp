"""按 OSM 建筑中心点生成 locations.csv（GCJ-02，高德/Apple 中国区坐标系）。

OSM 坐标是 WGS84，这里用公开的 GCJ-02 偏移算法转换；ODbL 数据，见 README 的说明。
只做「楼级」定位：三教/六教这类多段建筑取各段中心点的平均。
"""
import math
import sys

A = 6378245.0
EE = 0.00669342162296594323

# (alias, 显示名, WGS84 lat, WGS84 lon, 说明)
BUILDINGS = [
    ("一教", "第一教学楼", 40.0001929, 116.3179204, "osm 79701306"),
    ("二教", "第二教学楼", 40.0010341, 116.3177943, "osm 79701309"),
    ("三教", "第三教学楼", (40.0013537 + 40.0017944) / 2, (116.3226033 + 116.3224233) / 2, "osm 79701125 + 79701097 平均"),
    ("四教", "第四教学楼", 40.0012011, 116.3212394, "osm 79700261"),
    ("五教", "第五教学楼", 40.0012777, 116.3205392, "osm 79701105"),
    ("六教", "第六教学楼", (40.0015547 + 40.0017799 + 40.0015655) / 3, (116.3238181 + 116.3236678 + 116.3234439) / 3, "osm 79701106/83690464/79701107 平均"),
    ("主楼", "中央主楼", 40.0006835, 116.3263596, "osm 79699845"),
    ("东主楼", "东主楼", 40.0005548, 116.3275587, "osm 84653294"),
    ("西主楼", "西主楼", 40.0004705, 116.3251263, "osm 79699846"),
    ("清华学堂", "清华学堂", 40.0011707, 116.3194509, "osm 83805067"),
    ("新清华学堂", "新清华学堂", 40.0003410, 116.3233969, "osm 158112141"),
    ("技科楼", "技术科学楼", 39.9969536, 116.3275081, "osm 81151484"),
    ("技术科学楼", "技术科学楼", 39.9969536, 116.3275081, "osm 81151484"),
    ("蒙民伟楼", "蒙民伟楼", 40.0028980, 116.3152689, "osm 80341746 学生文化活动中心"),
    ("建筑馆", "建筑馆", 39.9982512, 116.3274319, "osm 80342017"),
    ("明理楼", "明理楼", 39.9968950, 116.3258270, "osm 80341983 法学院"),
    ("综体", "综合体育馆", 40.0030976, 116.3262759, "osm 83799756"),
    ("综合体育馆", "综合体育馆", 40.0030976, 116.3262759, "osm 83799756"),
    ("东操", "东大操场", 40.0044410, 116.3261261, "osm 32954674"),
    ("西操", "西大操场", 40.0037785, 116.3162203, "osm 32954848"),
    ("图书馆", "清华大学图书馆", 40.0036156, 116.3174897, "osm 20779764 逸夫馆同址"),
    ("逸夫馆", "逸夫馆", 40.0036156, 116.3174897, "osm 20779764"),
    ("法律图书馆", "法律图书馆", 39.9979739, 116.3227782, "osm 817157928"),
    ("人文社科图书馆", "人文社科图书馆（凯风楼）", 40.0029544, 116.3224832, "osm 226671429"),
]


def _transform_lat(x, y):
    ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * math.sqrt(abs(x))
    ret += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    ret += (20.0 * math.sin(y * math.pi) + 40.0 * math.sin(y / 3.0 * math.pi)) * 2.0 / 3.0
    ret += (160.0 * math.sin(y / 12.0 * math.pi) + 320 * math.sin(y * math.pi / 30.0)) * 2.0 / 3.0
    return ret


def _transform_lon(x, y):
    ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * math.sqrt(abs(x))
    ret += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    ret += (20.0 * math.sin(x * math.pi) + 40.0 * math.sin(x / 3.0 * math.pi)) * 2.0 / 3.0
    ret += (150.0 * math.sin(x / 12.0 * math.pi) + 300.0 * math.sin(x / 30.0 * math.pi)) * 2.0 / 3.0
    return ret


def wgs84_to_gcj02(lat, lon):
    dlat = _transform_lat(lon - 105.0, lat - 35.0)
    dlon = _transform_lon(lon - 105.0, lat - 35.0)
    radlat = lat / 180.0 * math.pi
    magic = math.sin(radlat)
    magic = 1 - EE * magic * magic
    sqrtmagic = math.sqrt(magic)
    dlat = (dlat * 180.0) / ((A * (1 - EE)) / (magic * sqrtmagic) * math.pi)
    dlon = (dlon * 180.0) / (A / sqrtmagic * math.cos(radlat) * math.pi)
    return lat + dlat, lon + dlon


def main():
    target = sys.argv[1]
    lines = [
        "# 地点映射表：alias,name,lat,lon",
        "#",
        "# 坐标来自 OpenStreetMap 的建筑轮廓中心点（ODbL，https://www.openstreetmap.org/copyright），",
        "# 已按标准算法转换为 GCJ-02，与高德 / Apple 地图（中国区）一致；精度只到「楼」。",
        "# Google 地图 / OSM 用 WGS84，若要在那边导航，约差几百米（可用它自己的搜索代替）。",
        "# 多段建筑取各段中心的平均值（三教、六教）。用的是 OSM 当时的数据，若某栋楼",
        "# 位置对不上，改这一行即可。",
        "#",
        "# 想加新地点：照抄一行，alias 用课表里的写法（程序会把 3教/第三教学楼 归一成 三教）。",
        "alias,name,lat,lon",
    ]
    for alias, name, lat, lon, note in BUILDINGS:
        glat, glon = wgs84_to_gcj02(lat, lon)
        lines.append("%s,%s,%.6f,%.6f" % (alias, name, glat, glon))
        print("%-10s %-20s %s -> %.6f,%.6f  (%s)" % (alias, name, "%.4f,%.4f" % (lat, lon), glat, glon, note))

    with open(target, "w", encoding="utf-8", newline="") as handle:
        handle.write("\r\n".join(lines) + "\r\n")
    print("\nwrote %d entries to %s" % (len(BUILDINGS), target))


if __name__ == "__main__":
    main()
