# FastSMRW push relay

Bridges Mastodon **Web Push** to the phone vendors' push services — Apple
**APNs** for iOS and **Firebase Cloud Messaging** for Android — so the apps can
receive notifications while they aren't running. It is a blind forwarder:
Mastodon posts an *encrypted* Web Push body to a per-device endpoint here, and
the relay forwards that still-encrypted blob on (to APNs as a `mutable-content`
alert, to FCM as a data-only message). The app decrypts it on-device — a
Notification Service Extension on iOS, `FastSmMessagingService` on Android.
**The relay never holds the decryption keys and never sees notification
contents.**

Either service may be left unconfigured; the relay starts as long as one of them
works, and refuses `/register` for the other.

Standard-library Go, no dependencies.

## Build

```sh
cd relay
GOOS=linux GOARCH=amd64 CGO_ENABLED=0 go build -trimpath -o dist/fastsm-push-relay .
```

Produces a static linux/amd64 binary (the brynify VM is Debian 13 x86_64).

## HTTP API

- `POST /register` — body
  `{"device_token","platform?","environment?","endpoint_id?"}`; returns
  `{"endpoint":"<public_base>/push/<id>"}`. `platform` is `apns` (the default,
  since the shipped iOS app predates the field) or `fcm`. `environment` applies
  to APNs only: `sandbox` (dev-signed builds) or `production` (TestFlight/App
  Store). Passing back a previously issued `endpoint_id` re-points that endpoint
  at a new device token, which is how the Android app handles Firebase rotating
  its token without having to re-subscribe with Mastodon. Optionally protected
  by a Bearer `RELAY_REGISTER_TOKEN`.
- `POST /push/<id>` — Mastodon posts the encrypted Web Push body here.
- `GET /healthz` — liveness.

## Install on the VM (mew@brynify.me)

The binary + unit + env template are staged in `~/fastsm-push-relay-staging/`.
These steps need sudo.

```sh
sudo install -m 0755 ~/fastsm-push-relay-staging/fastsm-push-relay /usr/local/bin/
sudo install -m 0644 ~/fastsm-push-relay-staging/fastsm-push-relay.service /etc/systemd/system/
sudo mkdir -p /etc/fastsm-push-relay
sudo install -m 0640 ~/fastsm-push-relay-staging/relay.env.example /etc/fastsm-push-relay/relay.env
# Put the APNs key here (mode 600) and edit relay.env to match:
sudo install -m 0600 /path/to/AuthKey_DA36MM9RZR.p8 /etc/fastsm-push-relay/
sudo $EDITOR /etc/fastsm-push-relay/relay.env      # set RELAY_PUBLIC_BASE etc.
sudo systemctl daemon-reload
sudo systemctl enable --now fastsm-push-relay
curl -s http://127.0.0.1:8787/healthz              # -> {"status":"ok"}
```

The unit pulls both keys in with `LoadCredential`, and systemd refuses to start
a service whose credential file is missing — so delete the `LoadCredential` /
`Environment` pair for whichever service you are not running (see the Android
section below for the Firebase one).

## Android setup (Firebase Cloud Messaging)

FCM needs a Firebase project. It is free, and produces two files: one that goes
into the app, one that goes onto this VM.

1. Create a project at <https://console.firebase.google.com>.

2. **Add an Android app** to it with package name `me.masonasons.fastsmrw` (the
   release id), and a **second** one for `me.masonasons.fastsmrw.debug` (the
   debug build type appends that suffix). Both are required: the plugin fails
   any variant whose application id has no client in the file, so with only the
   release app registered `assembleDebug` stops with "No matching client found
   for package name".

3. Download the generated **`google-services.json`** and put it at
   `android/app/google-services.json`. It is gitignored; for CI, add it as the
   repo secret `FASTSMRW_GOOGLE_SERVICES_B64`:

   ```sh
   base64 -w0 android/app/google-services.json
   ```

   A build without this file still succeeds — the app just reports push as
   unavailable in Settings.

4. **Project settings -> Service accounts -> Generate new private key** gives a
   JSON key the relay signs with. Install it on the VM alongside the APNs key:

   ```sh
   sudo install -m 0600 /path/to/key.json /etc/fastsm-push-relay/fcm-service-account.json
   sudo systemctl restart fastsm-push-relay
   ```

   The systemd unit passes it in via `LoadCredential`, so `FCM_SERVICE_ACCOUNT`
   is set for you; leave `FCM_PROJECT_ID` blank unless it differs from the
   `project_id` in the key file.

5. Make sure **Firebase Cloud Messaging API (V1)** is enabled for the project in
   the Google Cloud console (adding an Android app normally enables it).

The service-account key is a secret: mode 600, never in git (see `.gitignore`).

## Expose it over HTTPS (fits the existing SNI router)

The front host's `:443` is a two-tier SNI stream router
(`/etc/nginx/streams-router.conf`) that adds PROXY protocol. To add
`push.brynify.me` terminating on this host:

1. **DNS** (you): `push.brynify.me  A  38.143.59.76`.

2. **TLS cert** for `push.brynify.me` (DNS-01 is simplest given :443/:80 are
   already routed):
   ```sh
   sudo certbot certonly --preferred-challenges dns -d push.brynify.me
   ```

3. **Tier-1 map** — add one line to the `$tier1` map in
   `/etc/nginx/streams-router.conf` (leave the existing entries untouched):
   ```nginx
   push.brynify.me  127.0.0.1:8500;
   ```

4. **TLS-terminating vhost** at `127.0.0.1:8500` — new file
   `/etc/nginx/sites-available/push.conf`, symlinked into `sites-enabled/`:
   ```nginx
   server {
       # tier-1 forwards raw TLS here WITH the PROXY header.
       listen 127.0.0.1:8500 ssl proxy_protocol;
       set_real_ip_from 127.0.0.1;
       real_ip_header   proxy_protocol;
       server_name push.brynify.me;

       ssl_certificate     /etc/letsencrypt/live/push.brynify.me/fullchain.pem;
       ssl_certificate_key /etc/letsencrypt/live/push.brynify.me/privkey.pem;

       location / {
           proxy_pass http://127.0.0.1:8787;
           proxy_set_header Host              $host;
           proxy_set_header X-Forwarded-For   $proxy_protocol_addr;
           proxy_set_header X-Forwarded-Proto https;
       }
   }
   ```

5. `sudo nginx -t && sudo systemctl reload nginx`

Then `RELAY_PUBLIC_BASE=https://push.brynify.me` and endpoints become
`https://push.brynify.me/push/<id>`.

Verify end to end:
```sh
curl -s https://push.brynify.me/healthz         # -> {"status":"ok"}
```

## Security notes

- The `.p8` and the Firebase service-account JSON are secrets: mode 600, only in
  `/etc/fastsm-push-relay/`, never in git (see `.gitignore`).
- Endpoint ids are 192-bit random and unguessable (the Web Push secrecy model).
- Set `RELAY_REGISTER_TOKEN` to a long random string if you want to gate
  `/register`; the app's `push_subscribe` sends the same value.
- Never place the user's account email in a subscription or relay request.
