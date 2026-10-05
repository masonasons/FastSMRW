#!/usr/bin/env python3
"""Distribute a freshly uploaded FastSMRW build to its TestFlight groups.

Run right after ios/testflight.sh uploads an .ipa. It:
  1. waits for the build to finish processing (processingState VALID),
  2. sets the "What to Test" notes from the top changelog section,
  3. adds the build to the internal "Personal Builds" group (installable at
     once, no review),
  4. adds it to the external "Public Beta" group and submits it for beta
     review.

Credentials come from the environment (the same App Store Connect API key the
upload used):
  ASC_KEY_ID        key id         (default: the project's key)
  ASC_ISSUER_ID     issuer id      (default: the project's issuer)
  ASC_KEY_P8        the .p8 text, OR
  ASC_KEY_PATH      path to the .p8 (default: ~/.appstoreconnect/private_keys/AuthKey_<id>.p8)

Group / app ids are not secret and default to the known FastSMRW values; override
with ASC_APP_ID, ASC_INTERNAL_GROUP_ID, ASC_EXTERNAL_GROUP_ID if they ever change.

Usage:  testflight_distribute.py <build-number>
A non-fatal problem on the external path (e.g. another build of the same version
still in review) is logged but does not fail the run; the internal group — the
one the maintainer installs from — is treated as required.
"""
import os
import sys
import time
import pathlib

import jwt
import requests

BASE = "https://api.appstoreconnect.apple.com/v1"

KEY_ID = os.environ.get("ASC_KEY_ID", "FB9N292RPN")
ISSUER = os.environ.get("ASC_ISSUER_ID", "02117eeb-7d87-4d4d-bf11-850f80204c4c")
APP_ID = os.environ.get("ASC_APP_ID", "6793329679")
INTERNAL_GROUP = os.environ.get("ASC_INTERNAL_GROUP_ID",
                                "eaf7dc93-60ee-4d90-9f98-c65bd86dc098")
EXTERNAL_GROUP = os.environ.get("ASC_EXTERNAL_GROUP_ID",
                                "cfa9e6db-e0c8-4cfb-9363-ca339b6992bd")


def private_key():
    if os.environ.get("ASC_KEY_P8"):
        return os.environ["ASC_KEY_P8"]
    path = os.environ.get("ASC_KEY_PATH",
                          str(pathlib.Path.home() / ".appstoreconnect"
                              / "private_keys" / f"AuthKey_{KEY_ID}.p8"))
    return pathlib.Path(path).read_text()


def token():
    now = int(time.time())
    return jwt.encode(
        {"iss": ISSUER, "iat": now, "exp": now + 1200,
         "aud": "appstoreconnect-v1"},
        private_key(), algorithm="ES256",
        headers={"kid": KEY_ID, "typ": "JWT"})


def req(method, path, **kw):
    url = path if path.startswith("http") else BASE + path
    return requests.request(method, url,
                            headers={"Authorization": f"Bearer {token()}",
                                     "Content-Type": "application/json"},
                            timeout=60, **kw)


def find_build(buildnum):
    # sort=-uploadedDate 400s with this key; fetch unsorted and match the number.
    r = req("GET", f"/apps/{APP_ID}/builds?limit=200")
    r.raise_for_status()
    for b in r.json()["data"]:
        if b["attributes"]["version"] == str(buildnum):
            return b
    return None


def wait_valid(buildnum, timeout=2400):
    """Wait until the build appears and reaches processingState VALID."""
    start = time.time()
    while time.time() - start < timeout:
        b = find_build(buildnum)
        if b:
            state = b["attributes"]["processingState"]
            print(f"build {buildnum}: {state}", flush=True)
            if state == "VALID":
                return b["id"]
            if state in ("INVALID", "FAILED"):
                sys.exit(f"build {buildnum} processing {state}")
        else:
            print(f"build {buildnum}: not visible yet", flush=True)
        time.sleep(30)
    sys.exit(f"timed out waiting for build {buildnum} to process")


def wait_beta_ready(bid, timeout=1200):
    """Beta-group adds 404 until TestFlight-side beta processing registers;
    wait for buildBetaDetail to leave PROCESSING first."""
    start = time.time()
    while time.time() - start < timeout:
        r = req("GET", f"/builds/{bid}/buildBetaDetail")
        if r.ok:
            a = r.json()["data"]["attributes"]
            internal = a.get("internalBuildState")
            print(f"buildBetaDetail: internal={internal} "
                  f"external={a.get('externalBuildState')}", flush=True)
            if internal and internal != "PROCESSING":
                return
        time.sleep(20)
    print("warning: buildBetaDetail still processing; continuing anyway",
          flush=True)


def changelog_notes():
    """The top (current) changelog section, as TestFlight 'what to test' notes,
    dropping entries that are only about the other front ends."""
    path = pathlib.Path(__file__).resolve().parent.parent / "docs" / "changelog.txt"
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return None
    # Skip the file title, then the first "x.y.z" heading + its "----" underline.
    i = 0
    while i < len(lines) and not _is_heading(lines, i):
        i += 1
    i += 2  # past heading and underline
    entries = []
    while i < len(lines) and not _is_heading(lines, i):
        line = lines[i].strip()
        if line.startswith("- ") and not _other_platform_only(line):
            entries.append(line)
        i += 1
    return "\n".join(entries) if entries else None


def _is_heading(lines, i):
    return (i + 1 < len(lines) and lines[i].strip()
            and set(lines[i + 1].strip()) == {"-"})


def _other_platform_only(line):
    low = line.lower()
    mentions_iphone = "iphone" in low or "ios" in low
    other = any(p in low for p in ("android", "windows", "linux",
                                   "on the mac", "on mac"))
    return other and not mentions_iphone


def set_notes(bid, text):
    r = req("GET", f"/builds/{bid}/betaBuildLocalizations")
    r.raise_for_status()
    loc = next((x for x in r.json()["data"]
                if x["attributes"]["locale"] == "en-US"), None)
    if loc:
        r = req("PATCH", f"/betaBuildLocalizations/{loc['id']}",
                json={"data": {"type": "betaBuildLocalizations", "id": loc["id"],
                               "attributes": {"whatsNew": text}}})
    else:
        r = req("POST", "/betaBuildLocalizations",
                json={"data": {"type": "betaBuildLocalizations",
                               "attributes": {"locale": "en-US", "whatsNew": text},
                               "relationships": {"build": {"data": {
                                   "type": "builds", "id": bid}}}}})
    print(f"notes: {r.status_code}", flush=True)


def add_group(bid, gid):
    r = req("POST", f"/builds/{bid}/relationships/betaGroups",
            json={"data": [{"type": "betaGroups", "id": gid}]})
    print(f"add group {gid}: {r.status_code} {r.text[:200]}", flush=True)
    return r.ok


def submit_review(bid):
    r = req("POST", "/betaAppReviewSubmissions",
            json={"data": {"type": "betaAppReviewSubmissions",
                           "relationships": {"build": {"data": {
                               "type": "builds", "id": bid}}}}})
    if r.status_code in (200, 201):
        print("submitted for external beta review", flush=True)
    elif "ANOTHER_BUILD_IN_REVIEW" in r.text:
        print("warning: another build of this version is still in review; "
              "skipping external review submission", flush=True)
    else:
        print(f"warning: beta review submission {r.status_code}: {r.text[:300]}",
              flush=True)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: testflight_distribute.py <build-number>")
    buildnum = sys.argv[1]

    bid = wait_valid(buildnum)
    print(f"build {buildnum} is VALID (id {bid})", flush=True)

    notes = changelog_notes()
    if notes:
        set_notes(bid, notes)

    wait_beta_ready(bid)

    if not add_group(bid, INTERNAL_GROUP):
        sys.exit("failed to add build to the internal group")

    # External testers only get the build after it passes beta review; submit
    # first (it drives externalBuildState), then add it to the public group.
    submit_review(bid)
    add_group(bid, EXTERNAL_GROUP)

    print(f"build {buildnum} distributed to both TestFlight groups", flush=True)


if __name__ == "__main__":
    main()
