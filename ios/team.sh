#!/bin/sh
# The signing team for ios/build_lp.sh and ios/deploy_lp.sh: LP_IOS_TEAM, else the first team of the account signed in to
# Xcode (Settings > Accounts), else the team of the first Apple Development certificate in the keychain.
if [ -n "${LP_IOS_TEAM:-}" ]; then echo "$LP_IOS_TEAM"; exit 0; fi
t=$(defaults export com.apple.dt.Xcode - 2>/dev/null | python3 -I -c 'import plistlib,sys
try:
    d = plistlib.loads(sys.stdin.buffer.read()).get("IDEProvisioningTeamByIdentifier", {})
    print(next((t["teamID"] for ts in d.values() for t in ts if t.get("teamID")), ""))
except Exception:
    print("")')
[ -n "$t" ] || t=$(security find-identity -v -p codesigning | sed -n 's/.*"Apple Development: .*(\([A-Z0-9]*\))".*/\1/p' | head -1)
echo "$t"
