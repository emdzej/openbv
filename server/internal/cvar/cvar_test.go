package cvar

import "testing"

func TestFormats(t *testing.T) {
	s := NewSV()
	cases := map[string]string{
		"sv_friendlyFire":  "set sv_friendlyFire false",
		"sv_timeToSpawn":   "set sv_timeToSpawn 5.000000",
		"sv_maxPlayer":     "set sv_maxPlayer 16",
		"sv_gameName":      `set sv_gameName "Babo Violent 2 - Server"`,
		"sv_enableShotgun": "set sv_enableShotgun true",
		"sv_smgDamage":     "set sv_smgDamage 0.100000",
		"sv_shottyRange":   "set sv_shottyRange 6.750000",
		"sv_password":      `set sv_password ""`,
		"sv_joinMessage":   `set sv_joinMessage "Welcome to the server!"`,
	}
	for name, want := range cases {
		if got := s.ChangeText(name); got != want {
			t.Errorf("%s: %q, want %q", name, got, want)
		}
	}
	if len(SentToClients) != 72 { // GameVar::sendSVVar sends 72
		t.Errorf("%d variables sent", len(SentToClients))
	}
	for _, n := range SentToClients {
		if s.Lookup(n) == nil {
			t.Errorf("%s not registered", n)
		}
	}
}

// The first registered match wins: sv_enableShotgun before sv_enableShotgunReload, sv_maxPlayer
// before sv_maxPlayerInGame.
func TestPrefixLookup(t *testing.T) {
	s := NewSV()
	if s.Lookup("sv_enableShotgun") != s.EnableShotgun || s.Lookup("sv_maxPlayer") != s.MaxPlayer {
		t.Fatal("prefix order")
	}
	if s.Lookup("sv_enableShotgunR") != s.EnableShotgunReload {
		t.Fatal("longer prefix")
	}
	if s.Formatted("sv_nothing") != "" {
		t.Fatal("unknown variable")
	}
}

func TestSetParsing(t *testing.T) {
	s := NewSV()
	if !s.MaxPlayer.Set("010") || s.MaxPlayer.I != 8 { // sscanf %i: octal
		t.Fatalf("octal: %d", s.MaxPlayer.I)
	}
	if !s.MaxPlayer.Set("0x1f") || s.MaxPlayer.I != 31 {
		t.Fatalf("hex: %d", s.MaxPlayer.I)
	}
	if s.MaxPlayer.Set("33") || s.MaxPlayer.I != 31 {
		t.Fatal("out of range accepted")
	}
	if !s.FriendlyFire.Set("TRUE") || !s.FriendlyFire.B {
		t.Fatal("bool case")
	}
	if s.FriendlyFire.Set("yes") {
		t.Fatal("bad bool accepted")
	}
	if !s.TimeToSpawn.Set(`"2.5" ignored`) || s.TimeToSpawn.F != 2.5 {
		t.Fatal("float")
	}
	if !s.GameName.Set(` "My server" `) || s.GameName.S != "My server" {
		t.Fatalf("string %q", s.GameName.S)
	}
}
