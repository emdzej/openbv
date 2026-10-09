package store

import (
	"encoding/json"
	"testing"
)

func TestBansPersist(t *testing.T) {
	dir := t.TempDir()
	b, err := OpenBans(dir)
	if err != nil {
		t.Fatal(err)
	}
	b.Add(Ban{Name: "Bravo", IP: "10.0.0.2", MAC: "02-00-00-00-00-02"})
	b.Add(Ban{Name: "MANUAL-IP-BAN", IP: "192.168.1.1"})
	if _, ok := b.Banned("10.0.0.2", ""); !ok {
		t.Fatal("ip")
	}
	if _, ok := b.Banned("", "02-00-00-00-00-02"); !ok {
		t.Fatal("mac")
	}
	if _, ok := b.Banned("", ""); ok {
		t.Fatal("empty address banned")
	}
	b2, _ := OpenBans(dir)
	if got := b2.List(); len(got) != 2 || got[0].Name != "Bravo" || got[0].At.IsZero() {
		t.Fatalf("reloaded %+v", got)
	}
	if _, ok, _ := b2.Remove(0); !ok {
		t.Fatal("remove")
	}
	if _, ok, _ := b2.Remove(5); ok {
		t.Fatal("removed a missing entry")
	}
	b3, _ := OpenBans(dir)
	if got := b3.List(); len(got) != 1 || got[0].IP != "192.168.1.1" {
		t.Fatalf("after remove %+v", got)
	}
}

func TestSessionsAndAudit(t *testing.T) {
	dir := t.TempDir()
	s, _ := OpenSessions(dir)
	s.Put("a", map[string]any{"name": "one"})
	s.Put("b", map[string]any{"name": "two"})
	s.Put("a", map[string]any{"name": "uno"})
	s2, _ := OpenSessions(dir)
	var first map[string]string
	if l := s2.List(); len(l) != 2 || l[0].ID != "a" || json.Unmarshal(l[0].Settings, &first) != nil || first["name"] != "uno" {
		t.Fatalf("sessions %+v", l)
	}
	s2.Delete("a")
	if l := s2.List(); len(l) != 1 || l[0].ID != "b" {
		t.Fatalf("after delete %+v", l)
	}
	a, _ := OpenAudit(dir)
	a.Log(Entry{Who: "token", Action: "kick", Session: "b", Detail: "kickid 3"})
	a2, _ := OpenAudit(dir)
	if r := a2.Recent(); len(r) != 1 || r[0].Action != "kick" || r[0].At.IsZero() {
		t.Fatalf("audit %+v", r)
	}
}

func TestMemoryOnly(t *testing.T) {
	b, _ := OpenBans("")
	if err := b.Add(Ban{IP: "1.2.3.4"}); err != nil {
		t.Fatal(err)
	}
	if _, ok := b.Banned("1.2.3.4", ""); !ok {
		t.Fatal("memory ban")
	}
}
