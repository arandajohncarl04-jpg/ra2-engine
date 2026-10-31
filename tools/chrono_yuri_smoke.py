#!/usr/bin/env python3
"""Run selected ACTUAL Yuri faction functions with synthetic rules, no RA2 assets.

Extracts exact C++ functions from the current source files, wraps them with
lightweight type/Rules stubs, compiles with g++, and runs assertions. This does
not run game scenes, graphics, MIX/CSF parsing, or a real Android device.
"""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def extract(path, name):
    src = (ROOT / path).read_text(encoding="utf-8")
    m = re.search(r"\binline[^\n;{]*?\b" + re.escape(name) + r"\s*\(", src)
    if not m:
        raise RuntimeError(f"{path}: no inline function {name}")
    opening = src.find("{", m.end())
    if opening < 0:
        raise RuntimeError(f"{name}: missing opening brace")
    depth = 0
    for i in range(opening, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[m.start():i+1]
    raise RuntimeError(f"{name}: missing closing brace")

gf_names = [
    "gf_tokens", "gf_default_country", "gf_owner_has", "gf_base_unit",
    "gf_lobby_countries", "gf_side_of_country", "gf_next_country",
    "gf_next_country_same_side",
]
ai_names = [
    "csv_tokens", "list_has", "planning_side", "house_country",
    "owned_by", "defense_count",
]

prelude = r"""
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ra2engine {
struct World {
    enum class Side { Allied, Soviet, Yuri, Neutral };
};
struct SkirmishSlot {
    World::Side side = World::Side::Allied;
    std::string country;
};
}
struct Rules {
    std::map<std::pair<std::string, std::string>, std::string> data;
    void set(const std::string& section, const std::string& key, const std::string& value) {
        data[{section,key}] = value;
    }
    std::string section_string(const std::string& section,
                               const std::string& key,
                               const std::string& fallback = "") const {
        const auto it = data.find({section,key});
        return it == data.end() ? fallback : it->second;
    }
    std::string general_string(const std::string& key,
                               const std::string& fallback = "") const {
        return section_string("General", key, fallback);
    }
};
"""

checks = r"""
int main() {
    using Side = ra2engine::World::Side;
    using Slot = ra2engine::SkirmishSlot;
    Rules r;
    r.set("Americans", "Side", "GDI");
    r.set("Russians", "Side", "Nod");
    r.set("YuriCountry", "Side", "ThirdSide");
    r.set("Sides", "GDI", "Americans,Alliance,French,Germans,British");
    r.set("Sides", "Nod", "Africans,Arabs,Confederation,Russians");
    r.set("Sides", "ThirdSide", "YuriCountry");
    r.set("General", "BaseUnit", "AMCV,SMCV,PCV");
    r.set("AMCV", "Owner", "Americans");
    r.set("SMCV", "Owner", "Russians");
    r.set("PCV", "Owner", "YuriCountry");
    r.set("YACNST", "Owner", "YuriCountry");
    r.set("YACNST", "AIBasePlanningSide", "2");
    r.set("GACNST", "Owner", "Americans");
    r.set("GACNST", "AIBasePlanningSide", "0");
    r.set("General", "YuriBaseDefenseCounts", "25,22,6");

    assert(gf_lobby_countries().size() == 10);
    assert(gf_lobby_countries()[9] == "YuriCountry");
    assert(gf_side_of_country(r, "YuriCountry") == Side::Yuri);
    assert(gf_side_of_country(r, "Americans") == Side::Allied);
    assert(gf_side_of_country(r, "Russians") == Side::Soviet);
    std::cout << "PASS 1: Yuri lobby option and side selection\n";

    Slot s{Side::Soviet, "Russians"};
    gf_next_country(r, s);
    assert(s.country == "YuriCountry" && s.side == Side::Yuri);
    gf_next_country_same_side(r, s);
    assert(s.country == "YuriCountry" && s.side == Side::Yuri);
    gf_next_country(r, s);
    assert(s.country == "Americans" && s.side == Side::Allied);
    std::cout << "PASS 2: Country cycling and Yuri side persistence\n";

    assert(gf_default_country(Side::Yuri) == "YuriCountry");
    assert(gf_base_unit(r, "YuriCountry") == "PCV");
    assert(gf_base_unit(r, "Russians") == "SMCV");
    std::cout << "PASS 3: Yuri receives PCV with synthetic BaseUnit/Owner rules\n";

    assert(aiside::planning_side(Side::Allied) == 0);
    assert(aiside::planning_side(Side::Soviet) == 1);
    assert(aiside::planning_side(Side::Yuri) == 2);
    assert(aiside::house_country(Side::Yuri, "", &r) == "YuriCountry");
    assert(aiside::house_country(Side::Yuri, "Americans", &r) == "YuriCountry");
    assert(aiside::owned_by(r, "YACNST", Side::Yuri, "YuriCountry"));
    assert(!aiside::owned_by(r, "YACNST", Side::Soviet, "Russians"));
    assert(!aiside::owned_by(r, "GACNST", Side::Yuri, "YuriCountry"));
    assert(aiside::defense_count(r, Side::Yuri, 0) == 25);
    std::cout << "PASS 4: Yuri AI-side, ownership and defense-count selection\n";
    std::cout << "RESULT: 4 asset-free C++ smoke scenarios PASSED\n";
}
"""

code = (prelude + "\n\n" +
        "\n\n".join(extract("engine/gf_start.inc", n) for n in gf_names) +
        "\n\nnamespace aiside {\n" +
        "\n\n".join(extract("engine/ai_side_catalog.inc", n) for n in ai_names) +
        "\n}\n" + checks)
with tempfile.TemporaryDirectory(prefix="chrono-yuri-") as d:
    cpp = Path(d) / "chrono_yuri_smoke.cpp"
    exe = Path(d) / "chrono_yuri_smoke"
    cpp.write_text(code, encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-O1",
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("LIMITATION: real MIX/rulesmd parsing, Yuri art, gameplay and AI battles untested.")
