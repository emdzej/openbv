package admin

import (
	"context"
	"crypto/subtle"
	"errors"
	"slices"
	"strings"
	"sync"

	"github.com/coreos/go-oidc/v3/oidc"
)

// Auth checks the admin page's bearer token and says who it is.
type Auth interface {
	Verify(ctx context.Context, bearer string) (who string, err error)
}

var ErrForbidden = errors.New("forbidden")

// TokenAuth accepts one fixed token (OPENBV_ADMIN_TOKEN).
type TokenAuth struct{ Token string }

func (t TokenAuth) Verify(_ context.Context, bearer string) (string, error) {
	if t.Token == "" || subtle.ConstantTimeCompare([]byte(bearer), []byte(t.Token)) != 1 {
		return "", ErrForbidden
	}
	return "token", nil
}

// OIDCAuth verifies access tokens from an OpenID Connect provider (OIDC_ISSUER): issued to ClientID
// (the admin page's public client) and carrying Role, as a flat "roles" claim, a realm role or a role
// of the client (Keycloak's layouts; as ../nowhereinparticular's support desk). The provider's keys
// are fetched on first use, so the server starts even if the issuer is briefly away.
type OIDCAuth struct {
	Issuer, ClientID, Role string

	mu       sync.Mutex
	verifier *oidc.IDTokenVerifier
}

func (a *OIDCAuth) get() (*oidc.IDTokenVerifier, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.verifier != nil {
		return a.verifier, nil
	}
	p, err := oidc.NewProvider(context.Background(), a.Issuer)
	if err != nil {
		return nil, err
	}
	// Keycloak's access tokens have aud "account" (or none): the client is checked as azp below
	a.verifier = p.Verifier(&oidc.Config{SkipClientIDCheck: true})
	return a.verifier, nil
}

func (a *OIDCAuth) Verify(ctx context.Context, bearer string) (string, error) {
	v, err := a.get()
	if err != nil {
		return "", err
	}
	tok, err := v.Verify(ctx, bearer)
	if err != nil {
		return "", ErrForbidden
	}
	var c struct {
		Azp            string                   `json:"azp"`
		Name           string                   `json:"name"`
		PreferredName  string                   `json:"preferred_username"`
		Email          string                   `json:"email"`
		Roles          []string                 `json:"roles"`
		RealmAccess    struct{ Roles []string } `json:"realm_access"`
		ResourceAccess map[string]struct {
			Roles []string `json:"roles"`
		} `json:"resource_access"`
	}
	if err := tok.Claims(&c); err != nil {
		return "", ErrForbidden
	}
	if c.Azp != a.ClientID && !slices.Contains(tok.Audience, a.ClientID) {
		return "", ErrForbidden
	}
	if a.Role != "" && !slices.Contains(c.Roles, a.Role) && !slices.Contains(c.RealmAccess.Roles, a.Role) &&
		!slices.Contains(c.ResourceAccess[a.ClientID].Roles, a.Role) {
		return "", ErrForbidden
	}
	for _, who := range []string{c.PreferredName, c.Email, c.Name, tok.Subject} {
		if who = strings.TrimPrefix(who, "service-account-"); who != "" {
			return who, nil
		}
	}
	return tok.Subject, nil
}

// AnyAuth accepts what any of its checks accepts (a token for scripts next to OIDC for people).
type AnyAuth []Auth

func (as AnyAuth) Verify(ctx context.Context, bearer string) (string, error) {
	err := ErrForbidden
	for _, a := range as {
		who, e := a.Verify(ctx, bearer)
		if e == nil {
			return who, nil
		}
		if !errors.Is(e, ErrForbidden) {
			err = e
		}
	}
	return "", err
}
