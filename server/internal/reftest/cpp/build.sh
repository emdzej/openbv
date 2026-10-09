#!/usr/bin/env bash
# Build the reference driver from the original C++ and regenerate ../testdata/golden.json.gz.
#
# The driver is the original functions, extracted verbatim (extract.py) from src/game:
# rotateAboutAxis, reflect and the rand helpers (CVector.cpp, compiled as is), segmentToSphere
# (Helper.cpp), Map::rayTest (Map.cpp) and Map::rayTileTest (Map.h), and the damage part of
# Player::hitSV (Player.cpp, preprocessed for the dedicated Pro build with unifdef), inside
# stand-in types (driver_head.cpp). rand() is MSVC's; cosf and sinf are musl's (musl/), as in the wasm
# client. -ffp-contract=off keeps every float operation
# rounded on its own, as the Windows build and the wasm client compute them.
#
# Needs clang++ (or g++), python3 and unifdef. The JSON is committed; CI only reads it.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
src="$root/src/game"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

unifdef -DCONSOLE -D_PRO_ -UWIN32 -U_DX_ "$src/Player.cpp" > "$out/Player.cpp" || true

{
	cat "$here/driver_head.cpp"
	python3 -I "$here/extract.py" "$src/Helper.cpp" "bool segmentToSphere("
	python3 -I "$here/extract.py" "$src/Map.cpp" "bool Map::rayTest("
	echo "bool Map::rayTileTest(int x, int y, CVector3f & p1, CVector3f & p2, CVector3f & normal)"
	echo "{"
	python3 -I "$here/extract.py" "$src/Map.h" "inline bool rayTileTest(" --body
	echo "}"
	# hitSV up to its alive check: the damage computation
	echo "float Player::damagePrelude(Weapon * fromWeapon, Player * from, float damage)"
	echo "{"
	python3 -I - "$out/Player.cpp" <<'EOF'
import sys
s = open(sys.argv[1], encoding='utf-8', errors='replace').read()
a = s.index('void Player::hitSV(')
a = s.index('float cdamage = damage;', a)
b = s.index('if (status == PLAYER_STATUS_ALIVE)', a)
print("// --- from game/Player.cpp: Player::hitSV, the damage")
print(s[a:b])
EOF
	echo "	return cdamage;"
	echo "}"
	cat "$here/driver_main.cpp"
} > "$out/driver.cpp"

CXX=${CXX:-clang++}
CC=${CC:-clang}
# trigonometry from musl (musl/, MIT), the wasm client's libc: CVector.cpp's cosf and sinf are its
for f in cosf sinf __cosdf __sindf __rem_pio2f; do
	"$CC" -O0 -ffp-contract=off -w -I"$here/musl" -c "$here/musl/$f.c" -o "$out/$f.o"
done
"$CXX" -std=c++11 -O0 -ffp-contract=off -w -I"$src" -include "$here/musl/libm.h" "$out/driver.cpp" "$src/CVector.cpp" \
	"$out"/cosf.o "$out"/sinf.o "$out"/__cosdf.o "$out"/__sindf.o "$out"/__rem_pio2f.o -o "$out/driver"
"$out/driver" "$out/golden.json"
gzip -9 -n -c "$out/golden.json" > "$here/../testdata/golden.json.gz"
echo "wrote $(cd "$here/../testdata" && pwd)/golden.json.gz ($(wc -c < "$here/../testdata/golden.json.gz") bytes)"
