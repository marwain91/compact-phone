# PBX / SBC interop matrix

The Asterisk fixture in `tools/dev/` covers the happy path. Before a v1.0
public release, every scenario below should be exercised against each
target system on real network paths.

Last updated: <!-- update when running -->

## Test plan (per target)

For each PBX/SBC, exercise:

1. **Registration** — UDP, TCP, TLS over the WAN (i.e. through the host
   NAT). Verify the `Contact:` header uses the public address discovered
   via STUN or `publicAddress` override.
2. **Outbound call** — dial an echo extension, talk for 30 s, hang up.
   Verify SDP codec negotiation lands on Opus, then PCMA, then PCMU
   depending on the per-account codec order.
3. **Inbound call** — external client → our app. Answer, talk, hang up.
   Confirm DTMF (RFC 2833) digits make it through.
4. **Hold** — exercise both sides. Verify SDP-level a=sendonly /
   a=recvonly are honored and music-on-hold plays.
5. **Blind + attended transfer** — confirm REFER and Replaces semantics.
6. **SIP MESSAGE / IM** — bi-directional plaintext.
7. **BLF / presence** — watch a remote extension, dial in/out from it,
   verify our app updates state.
8. **MWI** — leave a voicemail, verify NOTIFY arrives and the indicator
   lights up.
9. **DND** — incoming should get 486 Busy Here without ringing.
10. **Call forwarding** — always / on-busy / no-answer all redirect
    correctly.

## Targets

| PBX / SBC | Version | Transport | Tested by | Date | Result |
|---|---|---|---|---|---|
| Asterisk (chan_pjsip) | 20 LTS | UDP/TCP/TLS+SRTP | dev fixture | continuous | ✅ all green |
| Asterisk (chan_pjsip) | 21 | UDP/TCP/TLS+SRTP | — | — | not run |
| FreeSWITCH | 1.10 | UDP/TCP/TLS+SRTP | — | — | not run |
| Kamailio + RTPengine | 5.7 | UDP/TLS+SRTP | — | — | not run |
| 3CX | v20 | UDP/TLS+SRTP | — | — | not run |
| FreePBX | 16/17 | UDP/TLS | — | — | not run |
| SIP.US | hosted | UDP+TLS | — | — | not run |
| Twilio Programmable Voice | hosted | TLS+SRTP | — | — | not run |
| Vonage Business | hosted | UDP/TLS | — | — | not run |

## NAT scenarios

For each target, also verify under:

- **Open** — public IP, no NAT.
- **Full-cone NAT** — most home routers. STUN should suffice.
- **Symmetric NAT** — carrier-grade or some hotel networks. Verify the app
  detects this (PJSIP NAT detect) and either upgrades to TLS+TURN or
  surfaces a clear "no media" error.
- **TLS-only egress** — corporate firewall that blocks UDP 5060 entirely;
  rely on TLS over 443/5061.
- **TCP keepalive over flaky LTE** — long-lived registration where the
  carrier silently drops idle TCP sockets.

## Reporting

File a row per failure with:
- Target + version
- Transport + NAT shape
- Captured SIP trace (PJSIP `log_level=5` for the run)
- pcap of the failing exchange
- Expected vs. actual

Open issues at https://github.com/marwain91/compact-phone/issues with
the `interop` label.
