package bvmap

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/emdzej/openbv/server/internal/proto"
)

// Shipped maps the original refuses too (IsMapValid): CTF-Slayer has no spawns at all.
var knownInvalid = map[string]bool{"CTF-Slayer": true}

// The game's data is not in git: ref/ (vanilla, fetched by ./play) and game/ (Prozac) are optional.
func loadAll(t *testing.T, dir string) int {
	lib := Library{Dir: dir}
	names := lib.List()
	if len(names) == 0 {
		return 0
	}
	for _, n := range names {
		m, err := lib.Load(n)
		if err != nil {
			t.Errorf("%s/%s: %v", dir, n, err)
			continue
		}
		if m.Width < 3 || m.Height < 3 || len(m.Cells) != m.Width*m.Height {
			t.Errorf("%s: size %dx%d", n, m.Width, m.Height)
		}
		if knownInvalid[n] {
			if Valid(m, proto.GameTypeDM) {
				t.Errorf("%s: expected to be invalid", n)
			}
			continue
		}
		if !Valid(m, proto.GameTypeDM) {
			t.Errorf("%s: no DM spawn", n)
		}
		if strings.HasPrefix(strings.ToUpper(n), "CTF") && !Valid(m, proto.GameTypeCTF) {
			t.Errorf("%s: CTF map without flag pods", n)
		}
		for _, s := range m.DMSpawns {
			if s[0] < 0 || s[1] < 0 || s[0] >= float32(m.Width) || s[1] >= float32(m.Height) {
				t.Errorf("%s: spawn %v outside the map", n, s)
			}
		}
		if m.Area() <= 0 {
			t.Errorf("%s: no area", n)
		}
	}
	return len(names)
}

func TestVanillaMaps(t *testing.T) {
	root := os.Getenv("OPENBV_TEST_CONTENT") // CI: tools/fetch-content.sh's .deps/content
	if root == "" {
		root = "../../../ref/BaboViolent2/BaboViolent2/Content"
	}
	dir := root + "/main/maps"
	if _, err := os.Stat(dir); err != nil {
		t.Skip("no vanilla data in ref/ (./play fetches it)")
	}
	n := loadAll(t, dir)
	t.Logf("%d vanilla maps", n)
	if n < 10 {
		t.Fatalf("only %d maps", n)
	}
	m, err := Library{Dir: dir}.Load("CTF-Daivuk")
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("CTF-Daivuk: v%d %dx%d, %d dm spawns, pods %v %v, area %d", m.Version, m.Width, m.Height, len(m.DMSpawns), m.FlagPodPos[0], m.FlagPodPos[1], m.Area())
}

func TestProzacMaps(t *testing.T) {
	dir := "../../../game/main/maps"
	if _, err := os.Stat(dir); err != nil {
		t.Skip("no Prozac data in game/")
	}
	t.Logf("%d Prozac maps", loadAll(t, dir))
}

func TestPathRejectsTraversal(t *testing.T) {
	lib := Library{Dir: t.TempDir()}
	os.WriteFile(filepath.Join(lib.Dir, "a.bvm"), nil, 0o644)
	for _, bad := range []string{"../a", "x/../a", "..\\a", "c:a", ""} {
		if _, ok := lib.Path(bad); ok {
			t.Errorf("accepted %q", bad)
		}
	}
	if _, ok := lib.Path("A"); !ok {
		t.Error("case-insensitive lookup failed")
	}
}

func TestMapName(t *testing.T) {
	if MapName("abcdefghijklmnopq.bvm") != "abcdefghijklmno" {
		t.Fatal(MapName("abcdefghijklmnopq.bvm"))
	}
}
