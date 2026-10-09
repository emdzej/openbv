// Package cvar is the game's console variables (src/engine/dk/CSystemVariable.h, dksvar.cpp) as far
// as a server uses them: the sv_* variables registered in GameVar.cpp (from :336), their text formats
// (bool true/false, int %i, float %f, string "value") and the engine's parsing on `set`.
package cvar

import (
	"fmt"
	"strconv"
	"strings"
)

// Kind is a variable's type.
type Kind int

const (
	Bool Kind = iota
	Int
	Float
	String
)

// Limits (CSystemVariable.h LIMIT_MIN, LIMIT_MAX).
const (
	LimitMin = 1
	LimitMax = 2
)

// Var is one registered variable.
type Var struct {
	Name     string // sv_port
	Help     string // the rest of the registered name: "[int : valid port (default 3333)]"
	Kind     Kind
	B        bool
	I        int32
	F        float32
	S        string
	Min, Max float32
	Limits   int
	def      string
}

// Registered is the name the engine registered: the name and its help text.
func (v *Var) Registered() string {
	if v.Help == "" {
		return v.Name
	}
	return v.Name + " " + v.Help
}

// Value is CSVType::getValue.
func (v *Var) Value() string {
	switch v.Kind {
	case Bool:
		if v.B {
			return "true"
		}
		return "false"
	case Int:
		return strconv.FormatInt(int64(v.I), 10)
	case Float:
		return fmt.Sprintf("%f", v.F)
	default:
		return `"` + v.S + `"`
	}
}

// Set is CSVType::setValue: the first token (quotes trimmed) for numbers and bools, the whole text
// for strings; false if it can't be parsed or is out of range (the value then stays).
func (v *Var) Set(params string) bool {
	if v.Kind == String {
		v.S = strings.Trim(strings.Trim(params, " "), `"`)
		return true
	}
	tok := strings.Trim(firstToken(params), `"`)
	switch v.Kind {
	case Bool:
		switch {
		case strings.EqualFold(tok, "false"):
			v.B = false
		case strings.EqualFold(tok, "true"):
			v.B = true
		default:
			return false
		}
	case Int:
		n := scanInt(tok)
		if (v.Limits&LimitMin != 0 && float32(n) < v.Min) || (v.Limits&LimitMax != 0 && float32(n) > v.Max) {
			return false
		}
		v.I = n
	case Float:
		f := scanFloat(tok)
		if (v.Limits&LimitMin != 0 && f < v.Min) || (v.Limits&LimitMax != 0 && f > v.Max) {
			return false
		}
		v.F = f
	}
	return true
}

// Reset puts the default back.
func (v *Var) Reset() { v.Set(v.def) }

func firstToken(s string) string {
	s = strings.TrimLeft(s, " ")
	if i := strings.IndexByte(s, ' '); i >= 0 {
		return s[:i]
	}
	return s
}

// scanInt is the engine CString::toInt: sscanf("%i"), which takes a leading decimal, 0x hex or
// 0 octal number and stops at the first character that doesn't fit. Nothing parsed: 0 (the original
// would leave its variable uninitialised).
func scanInt(s string) int32 {
	s = strings.TrimLeft(s, " \t\n")
	neg := false
	if s != "" && (s[0] == '-' || s[0] == '+') {
		neg = s[0] == '-'
		s = s[1:]
	}
	base := 10
	switch {
	case len(s) > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'):
		base, s = 16, s[2:]
	case len(s) > 0 && s[0] == '0':
		base = 8
	}
	end := 0
	for end < len(s) && digit(s[end], base) {
		end++
	}
	n, _ := strconv.ParseInt(s[:end], base, 64)
	if neg {
		n = -n
	}
	return int32(n)
}

func digit(c byte, base int) bool {
	switch base {
	case 8:
		return c >= '0' && c <= '7'
	case 16:
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')
	}
	return c >= '0' && c <= '9'
}

// scanFloat is sscanf("%f"): the longest leading float.
func scanFloat(s string) float32 {
	s = strings.TrimLeft(s, " \t\n")
	for end := len(s); end > 0; end-- {
		if f, err := strconv.ParseFloat(s[:end], 32); err == nil {
			return float32(f)
		}
	}
	return 0
}

// Registry is the variables in registration order: lookups by prefix take the first match, as
// dksvarGetFormatedVar does.
type Registry struct {
	vars []*Var
}

// Lookup finds the first variable whose registered name starts with name, case-insensitively.
func (r *Registry) Lookup(name string) *Var {
	for _, v := range r.vars {
		reg := v.Registered()
		if len(reg) >= len(name) && strings.EqualFold(reg[:len(name)], name) {
			return v
		}
	}
	return nil
}

// Formatted is dksvarGetFormatedVar: "<name as asked> <value>", or "" if nothing matches.
func (r *Registry) Formatted(name string) string {
	v := r.Lookup(name)
	if v == nil {
		return ""
	}
	return name + " " + v.Value()
}

// All returns the variables in registration order.
func (r *Registry) All() []*Var { return r.vars }

func (r *Registry) add(v *Var) *Var {
	v.def = v.Value()
	if v.Kind == String {
		v.def = v.S
	}
	r.vars = append(r.vars, v)
	return v
}

func (r *Registry) boolVar(name, help string, def bool) *Var {
	return r.add(&Var{Name: name, Help: help, Kind: Bool, B: def})
}

func (r *Registry) intVar(name, help string, def int32, min, max float32, limits int) *Var {
	return r.add(&Var{Name: name, Help: help, Kind: Int, I: def, Min: min, Max: max, Limits: limits})
}

func (r *Registry) floatVar(name, help string, def, min, max float32, limits int) *Var {
	return r.add(&Var{Name: name, Help: help, Kind: Float, F: def, Min: min, Max: max, Limits: limits})
}

func (r *Registry) stringVar(name, help, def string) *Var {
	return r.add(&Var{Name: name, Help: help, Kind: String, S: def})
}
