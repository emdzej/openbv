package cvar

// SV is a server's sv_* variables, registered in GameVar.cpp's order (from :336) with the Pro build's
// defaults and limits. One per session.
type SV struct {
	Registry

	TimeToSpawn, FriendlyFire, ReflectedDamage, TopView, MinSendInterval, ForceRespawn, BaboStats *Var
	RoundTimeLimit, GameTimeLimit, ScoreLimit, WinLimit, ServerType, SpawnType, GameType          *Var
	SubGameType, BombTime, GameName, Port, MaxPlayer, MaxPlayerInGame, Password                   *Var
	EnableSMG, EnableShotgun, EnableSniper, EnableDualMachineGun, EnableChainGun, EnableBazooka   *Var
	EnablePhotonRifle, EnableFlameThrower, EnableShotgunReload, SlideOnIce, ShowEnemyTag          *Var
	AdminUser, AdminPass                                                                          *Var
	EnableSecondary, EnableKnives, EnableNuclear, EnableShield, EnableMinibot                     *Var
	AutoBalance, AutoBalanceTime, EnableVote, GamePublic, MaxUploadRate, ShowKills                *Var
	Matchcode, Matchmode, Report, MaxPing, AutoSpectateWhenIdle, AutoSpectateIdleMaxTime          *Var
	BeGoodServer, ValidateWeapons, MinTilesPerBabo, MaxTilesPerBabo, ZookaRemoteDet, ExplodingFT  *Var
	SendJoinMessage, EnableMolotov, JoinMessage, ShottyDropRadius, ShottyRange                    *Var
	FtMaxRange, FtMinRange, FtExpirationTimer                                                     *Var
	PhotonDamageCoefficient, PhotonVerticalShift, PhotonDistMult, PhotonHorizontalShift           *Var
	FtDamage, SmgDamage, DmgDamage, CgDamage, SniperDamage, ShottyDamage, ZookaDamage             *Var
	ZookaRadius, NukeRadius, NukeTimer, NukeReload, PhotonType, SpawnImmunityTime                 *Var
	CubicMotion                                                                                   *Var
}

// SentToClients is GameVar::sendSVVar's list, in order (GameVar.cpp:813).
var SentToClients = []string{
	"sv_friendlyFire", "sv_reflectedDamage", "sv_timeToSpawn", "sv_topView", "sv_minSendInterval",
	"sv_forceRespawn", "sv_baboStats", "sv_roundTimeLimit", "sv_gameTimeLimit", "sv_scoreLimit",
	"sv_winLimit", "sv_gameType", "sv_serverType", "sv_spawnType", "sv_subGameType", "sv_bombTime",
	"sv_gameName", "sv_port", "sv_maxPlayer", "sv_maxPlayerInGame", "sv_password", "sv_enableSMG",
	"sv_enableShotgun", "sv_enableSniper", "sv_enableDualMachineGun", "sv_enableChainGun",
	"sv_enableBazooka", "sv_enablePhotonRifle", "sv_enableFlameThrower", "sv_enableShotgunReload",
	"sv_slideOnIce", "sv_showEnemyTag", "sv_enableSecondary", "sv_enableKnives", "sv_enableNuclear",
	"sv_enableShield", "sv_enableMinibot", "sv_autoBalance", "sv_autoBalanceTime", "sv_gamePublic",
	"sv_matchcode", "sv_matchmode", "sv_maxPing", "sv_shottyDropRadius", "sv_shottyRange",
	"sv_enableMolotov", "sv_ftMaxRange", "sv_ftMinRange", "sv_photonDamageCoefficient",
	"sv_zookaRemoteDet", "sv_smgDamage", "sv_ftDamage", "sv_dmgDamage", "sv_cgDamage", "sv_shottyDamage",
	"sv_sniperDamage", "sv_zookaDamage", "sv_photonType", "sv_zookaRadius", "sv_nukeRadius",
	"sv_nukeTimer", "sv_nukeReload", "sv_minTilesPerBabo", "sv_maxTilesPerBabo", "sv_photonDistMult",
	"sv_photonVerticalShift", "sv_photonHorizontalShift", "sv_joinMessage", "sv_sendJoinMessage",
	"sv_ftExpirationTimer", "sv_explodingFT", "sv_enableVote",
}

// NewSV registers the variables with their defaults.
func NewSV() *SV {
	s := &SV{}
	r := &s.Registry
	mm := LimitMin | LimitMax
	s.TimeToSpawn = r.floatVar("sv_timeToSpawn", "[float : (default 5)]", 5, 0, 60, mm)
	s.FriendlyFire = r.boolVar("sv_friendlyFire", "[bool : (default false)]", false)
	s.ReflectedDamage = r.boolVar("sv_reflectedDamage", "[bool : (default false)]", false)
	s.TopView = r.boolVar("sv_topView", "[bool : (default true)]", true)
	s.MinSendInterval = r.intVar("sv_minSendInterval", "[int : (default 2)]", 2, 0, 5, mm)
	s.ForceRespawn = r.boolVar("sv_forceRespawn", "[bool : (default false)]", false)
	s.BaboStats = r.boolVar("sv_baboStats", "[bool : (default false)]", false)
	s.RoundTimeLimit = r.floatVar("sv_roundTimeLimit", "[float : 0 = unlimited (default 180)]", 180, 0, 0, LimitMin)
	s.GameTimeLimit = r.floatVar("sv_gameTimeLimit", "[float : 0 = unlimited (default 1800)]", 1800, 0, 0, LimitMin)
	s.ScoreLimit = r.intVar("sv_scoreLimit", "[int : 0 = unlimited (default 50)]", 50, 0, 0, LimitMin)
	s.WinLimit = r.intVar("sv_winLimit", "[int : 0 = unlimited (default 7)]", 7, 0, 0, LimitMin)
	s.ServerType = r.intVar("sv_serverType", "[int : 0=Normal, 1=Pro]", 0, 0, 1, mm)
	s.SpawnType = r.intVar("sv_spawnType", "[int : 0=Normal, 1=Ladder]", 0, 0, 1, mm)
	s.GameType = r.intVar("sv_gameType", "[int : 0=Deathmatch, 1=Team Deathmatch, 2=Capture The Flag, 3=Champion]", 1, 0, 3, mm)
	s.SubGameType = r.intVar("sv_subGameType", "[int : 0=Normal, 1=Instagib, 2=RandomWeapon]", 0, 0, 2, mm)
	s.BombTime = r.floatVar("sv_bombTime", "[int : (default 60)]", 60, 10, 0, LimitMin) // a float (GameVar.h:103), whatever the help says
	s.GameName = r.stringVar("sv_gameName", `[string : ""]`, "Babo Violent 2 - Server")
	s.Port = r.intVar("sv_port", "[int : valid port (default 3333)]", 3333, 1024, 65536, mm)
	s.MaxPlayer = r.intVar("sv_maxPlayer", "[int : 1 to 32 (default 16)]", 16, 1, 32, mm)
	s.MaxPlayerInGame = r.intVar("sv_maxPlayerInGame", "[int : 0 to 32 (default 0, no limit)]", 0, 0, 32, mm)
	s.Password = r.stringVar("sv_password", `[string : ""`, "")
	b := func(name string, def bool) *Var {
		d := "false"
		if def {
			d = "true"
		}
		return r.boolVar(name, "[bool : true | false (default "+d+")]", def)
	}
	s.EnableSMG = b("sv_enableSMG", true)
	s.EnableShotgun = b("sv_enableShotgun", true)
	s.EnableSniper = b("sv_enableSniper", true)
	s.EnableDualMachineGun = b("sv_enableDualMachineGun", true)
	s.EnableChainGun = b("sv_enableChainGun", true)
	s.EnableBazooka = b("sv_enableBazooka", true)
	s.EnablePhotonRifle = b("sv_enablePhotonRifle", true)
	s.EnableFlameThrower = b("sv_enableFlameThrower", true)
	s.EnableShotgunReload = b("sv_enableShotgunReload", true)
	s.SlideOnIce = b("sv_slideOnIce", false)
	s.ShowEnemyTag = b("sv_showEnemyTag", false)
	s.AdminUser = r.stringVar("zsv_adminUser", `[string : ""`, "")
	s.AdminPass = r.stringVar("zsv_adminPass", `[string : ""`, "")
	s.EnableSecondary = b("sv_enableSecondary", true)
	s.EnableKnives = b("sv_enableKnives", true)
	s.EnableNuclear = b("sv_enableNuclear", true)
	s.EnableShield = b("sv_enableShield", true)
	s.EnableMinibot = b("sv_enableMinibot", true)
	s.AutoBalance = b("sv_autoBalance", true)
	s.AutoBalanceTime = r.intVar("sv_autoBalanceTime", "[int : 1 to 15 (default 4)]", 4, 1, 15, mm)
	s.EnableVote = b("sv_enableVote", true)
	s.GamePublic = b("sv_gamePublic", true)
	s.MaxUploadRate = r.floatVar("sv_maxUploadRate", "[float : 0 = unlimited (default 8.0)]", 8, 0, 0, LimitMin)
	s.ShowKills = b("sv_showKills", false)
	s.Matchcode = r.stringVar("sv_matchcode", `[string : "" (default "")]`, "")
	s.Matchmode = r.intVar("sv_matchmode", "[int : 0 = unlimited (default 0)]", 0, 0, 0, LimitMin)
	s.Report = b("sv_report", false)
	s.MaxPing = r.intVar("sv_maxPing", "[int : 0 to 1000 (default 1000)]", 1000, 0, 1000, mm)
	s.AutoSpectateWhenIdle = b("sv_autoSpectateWhenIdle", true)
	s.AutoSpectateIdleMaxTime = r.intVar("sv_autoSpectateIdleMaxTime", "[int : 60 to unlimited (default 180)]", 180, 60, 0, LimitMin)
	s.BeGoodServer = b("sv_beGoodServer", true)
	s.ValidateWeapons = b("sv_validateWeapons", true)
	s.MinTilesPerBabo = r.floatVar("sv_minTilesPerBabo", "[float : 0.0 to 250.0 (default 55.0)]", 55, 0, 250, mm)
	s.MaxTilesPerBabo = r.floatVar("sv_maxTilesPerBabo", "[float : 0.0 to 500.0 (default 80.0)]", 80, 0, 500, mm)
	s.ZookaRemoteDet = b("sv_zookaRemoteDet", true)
	s.ExplodingFT = b("sv_explodingFT", false)
	s.SendJoinMessage = b("sv_sendJoinMessage", true)
	s.EnableMolotov = b("sv_enableMolotov", true)
	s.JoinMessage = r.stringVar("sv_joinMessage", `[string : "" ]`, "Welcome to the server!")
	f := func(name, help string, def, min, max float32) *Var { return r.floatVar(name, help, def, min, max, mm) }
	s.ShottyDropRadius = f("sv_shottyDropRadius", "[float : 0 to 2 (default 0.40)]", 0.40, 0, 2)
	s.ShottyRange = f("sv_shottyRange", "[float : 1 to 24 (default 6.75)]", 6.75, 1, 24)
	s.FtMaxRange = f("sv_ftMaxRange", "[float : 1 to 24  (default 8.0)]", 8, 1, 24)
	s.FtMinRange = f("sv_ftMinRange", "[float : 0 to 24  (default 1.0)]", 1, 0, 24)
	s.FtExpirationTimer = f("sv_ftExpirationTimer", "[float : 0 to 30  (default 1.5)]", 1.5, 0, 30)
	s.PhotonDamageCoefficient = f("sv_photonDamageCoefficient", "[float : -100 to 100 (default 0.5)]", 0.5, -100, 100)
	s.PhotonVerticalShift = f("sv_photonVerticalShift", "[float : -100 to 100 (default 0.325)]", 0.325, -100, 100)
	s.PhotonDistMult = f("sv_photonDistMult", "[float : -100 to 100 (default 0.25)]", 0.25, -100, 100)
	s.PhotonHorizontalShift = f("sv_photonHorizontalShift", "[float : -100 to 100 (default 5.00)]", 5, -100, 100)
	dmg := func(name string, def float32, d string) *Var {
		return f(name, "[float : -100 to 100 (actual damage 100 times this, default "+d+")]", def, -100, 100)
	}
	s.FtDamage = dmg("sv_ftDamage", 0.12, "0.12")
	s.SmgDamage = dmg("sv_smgDamage", 0.1, "0.10")
	s.DmgDamage = dmg("sv_dmgDamage", 0.14, "0.14")
	s.CgDamage = dmg("sv_cgDamage", 0.16, "0.16")
	s.SniperDamage = dmg("sv_sniperDamage", 0.2, "0.20")
	s.ShottyDamage = dmg("sv_shottyDamage", 0.21, "0.21")
	s.ZookaDamage = dmg("sv_zookaDamage", 0.85, "0.85")
	s.ZookaRadius = f("sv_zookaRadius", "[float : 1 to 8 (default 2.0)]", 2, 1, 8)
	s.NukeRadius = f("sv_nukeRadius", "[float : 4 to 16 (default 6.0)]", 6, 4, 16)
	s.NukeTimer = f("sv_nukeTimer", "[float : 0 to 12 (default 3.0)]", 3, 0, 12)
	s.NukeReload = f("sv_nukeReload", "[float : 0 to 48 (default 12.0)]", 12, 0, 48)
	s.PhotonType = r.intVar("sv_photonType", "[int : 0 to 3 (default 1)", 1, 0, 3, mm)
	s.SpawnImmunityTime = f("sv_spawnImmunityTime", "[float : 0 to 3 (default 2.0)]", 2, 0, 3)
	s.CubicMotion = b("cl_cubicMotion", true)
	return s
}

// ChangeText is the NET_SVCL_SV_CHANGE text GameVar::sendOne makes (GameVar.cpp:988): "set " and the
// formatted variable, cut to 79 characters. The password is never sent (design/server.md §2.6,
// a deliberate deviation: the original sent it in clear to every client).
func (s *SV) ChangeText(name string) string {
	text := "set " + s.Formatted(name)
	if name == "sv_password" {
		text = `set sv_password ""`
	}
	if len(text) > 79 {
		text = text[:79]
	}
	return text
}
