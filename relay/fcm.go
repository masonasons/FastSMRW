// Firebase Cloud Messaging sender — Android's counterpart to the APNs path.
//
// FCM only accepts a Google OAuth2 access token, which we mint ourselves from
// the Firebase project's service-account key (an RS256 JWT exchanged at the
// token endpoint) so the relay keeps its standard-library-only rule.
//
// Pushes go out as DATA-ONLY messages. FCM must not compose a notification of
// its own here: the payload is an encrypted Web Push blob that only the device
// can read, so the app has to decrypt it before it knows what to say. Data-only
// plus priority HIGH is what wakes the app's messaging service while the app
// itself is closed.
package main

import (
	"bytes"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"errors"
	"io"
	"net/http"
	"net/url"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

// The fields we need out of a Firebase service-account JSON key.
type serviceAccount struct {
	ProjectID   string `json:"project_id"`
	ClientEmail string `json:"client_email"`
	PrivateKey  string `json:"private_key"`
	TokenURI    string `json:"token_uri"`
}

type fcm struct {
	projectID string
	email     string
	tokenURI  string
	key       *rsa.PrivateKey
	client    *http.Client

	mu      sync.Mutex
	token   string
	expires time.Time
}

func newFCM(cfg config) (*fcm, error) {
	raw, err := os.ReadFile(cfg.fcmKeyPath)
	if err != nil {
		return nil, err
	}
	var sa serviceAccount
	if err := json.Unmarshal(raw, &sa); err != nil {
		return nil, err
	}
	if sa.ClientEmail == "" || sa.PrivateKey == "" {
		return nil, errors.New("fcm key: missing client_email or private_key")
	}
	block, _ := pem.Decode([]byte(sa.PrivateKey))
	if block == nil {
		return nil, errors.New("fcm key: private_key is not PEM")
	}
	parsed, err := x509.ParsePKCS8PrivateKey(block.Bytes)
	if err != nil {
		return nil, err
	}
	key, ok := parsed.(*rsa.PrivateKey)
	if !ok {
		return nil, errors.New("fcm key: not an RSA private key")
	}
	project := cfg.fcmProjectID
	if project == "" {
		project = sa.ProjectID
	}
	if project == "" {
		return nil, errors.New("fcm: no project id (set FCM_PROJECT_ID)")
	}
	tokenURI := sa.TokenURI
	if tokenURI == "" {
		tokenURI = "https://oauth2.googleapis.com/token"
	}
	return &fcm{
		projectID: project,
		email:     sa.ClientEmail,
		tokenURI:  tokenURI,
		key:       key,
		client:    &http.Client{Timeout: 15 * time.Second},
	}, nil
}

// accessToken returns a cached OAuth2 bearer token for the FCM send scope,
// refreshing it a minute before it actually expires.
func (f *fcm) accessToken() (string, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.token != "" && time.Now().Before(f.expires) {
		return f.token, nil
	}
	now := time.Now()
	header := b64url([]byte(`{"alg":"RS256","typ":"JWT"}`))
	claims := b64url([]byte(`{"iss":"` + f.email +
		`","scope":"https://www.googleapis.com/auth/firebase.messaging","aud":"` + f.tokenURI +
		`","iat":` + strconv.FormatInt(now.Unix(), 10) +
		`,"exp":` + strconv.FormatInt(now.Add(time.Hour).Unix(), 10) + `}`))
	signingInput := header + "." + claims
	digest := sha256.Sum256([]byte(signingInput))
	sig, err := rsa.SignPKCS1v15(rand.Reader, f.key, crypto.SHA256, digest[:])
	if err != nil {
		return "", err
	}
	assertion := signingInput + "." + b64url(sig)

	form := url.Values{
		"grant_type": {"urn:ietf:params:oauth:grant-type:jwt-bearer"},
		"assertion":  {assertion},
	}
	resp, err := f.client.PostForm(f.tokenURI, form)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(resp.Body, 1<<16))
	if resp.StatusCode >= 300 {
		return "", errors.New("fcm token: " + resp.Status + " " + string(body))
	}
	var tok struct {
		AccessToken string `json:"access_token"`
		ExpiresIn   int    `json:"expires_in"`
	}
	if err := json.Unmarshal(body, &tok); err != nil || tok.AccessToken == "" {
		return "", errors.New("fcm token: unusable response")
	}
	ttl := tok.ExpiresIn
	if ttl <= 0 {
		ttl = 3600
	}
	f.token = tok.AccessToken
	f.expires = now.Add(time.Duration(ttl-60) * time.Second)
	return f.token, nil
}

// send delivers one data-only FCM message. Returns the HTTP status and, on
// failure, the error code FCM gives ("UNREGISTERED", "INVALID_ARGUMENT", ...)
// so the caller can drop dead registration tokens.
func (f *fcm) send(d device, data map[string]string) (int, string, error) {
	token, err := f.accessToken()
	if err != nil {
		return 0, "", err
	}
	msg := map[string]any{
		"message": map[string]any{
			"token": d.Token,
			// Data-only: no "notification" key, so the app's service always runs
			// and can show the decrypted text. HIGH priority delivers in Doze.
			"data":    data,
			"android": map[string]any{"priority": "HIGH"},
		},
	}
	payload, err := json.Marshal(msg)
	if err != nil {
		return 0, "", err
	}
	req, err := http.NewRequest(http.MethodPost,
		"https://fcm.googleapis.com/v1/projects/"+f.projectID+"/messages:send",
		bytes.NewReader(payload))
	if err != nil {
		return 0, "", err
	}
	req.Header.Set("authorization", "Bearer "+token)
	req.Header.Set("content-type", "application/json")
	resp, err := f.client.Do(req)
	if err != nil {
		return 0, "", err
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(resp.Body, 1<<16))
	if resp.StatusCode < 300 {
		return resp.StatusCode, "", nil
	}
	// {"error":{"status":"NOT_FOUND","details":[{"errorCode":"UNREGISTERED"}]}}
	var e struct {
		Error struct {
			Status  string `json:"status"`
			Message string `json:"message"`
			Details []struct {
				ErrorCode string `json:"errorCode"`
			} `json:"details"`
		} `json:"error"`
	}
	_ = json.Unmarshal(body, &e)
	reason := e.Error.Status
	for _, det := range e.Error.Details {
		if det.ErrorCode != "" {
			reason = det.ErrorCode
		}
	}
	if reason == "" {
		reason = strings.TrimSpace(string(body))
	}
	return resp.StatusCode, reason, nil
}
