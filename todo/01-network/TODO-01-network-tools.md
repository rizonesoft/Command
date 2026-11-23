---
schema_version: 1
id: network-tools
domain: 01-network
status: active
title: "TODO-01 -- Network Tools"
---

# TODO-01 -- Network Tools

> **Goal:** The Windows network commands people type most, with the same switches and exit codes.

> **Current state:** Measured 2026-11-23. `thash`, `thex`, `tfind`, `ttree` and `tcmp` are built, with 130 tests on Windows after two reviews. No network tool exists. `docs/tools.md` sorts all Windows tools.

## Inputs

- `docs/tools.md`: which tools, and their order.
- Microsoft's documentation of each command: behaviour only, our own text.


## Outcome

- `ping`, `tracert`, `pathping`, `ipconfig`, `getmac`, `hostname`, `netstat`, `arp`, `route` and `nslookup` work on Windows without administrator rights.


**Adjacency:** all=not-applicable (commands keep no records, settings or reports)

**Adjacency rationale:** Each command prints and exits.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Network calls in the SDK list | -- | [ ] |
| 2 | §2 | ping | §1 | [ ] |
| 3 | §3 | tracert and pathping | §2 | [ ] |
| 4 | §4 | ipconfig, getmac and hostname | §1 | [ ] |
| 5 | §5 | netstat, arp and route | §1 | [ ] |
| 6 | §6 | nslookup | §1 | [ ] |

## 1. Network calls in the SDK list

`ping` and `tracert` need the ICMP calls and the adapter list. They are added to Impossible SDK's list with this section, as a deliberate change there.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the calls to `abi/tiers.json` in Impossible SDK and move the pin here. Done when: the import check accepts them at tier 4 only.
2. Write `common/net.c`. Done when: a name resolves and an address prints.

- [ ] List the needed `iphlpapi` and `ws2_32` calls in Impossible SDK's `abi/tiers.json` with the reason. Done when: a tier 0 program that uses one is refused. Cheaper substitute: an unchecked program.
- [ ] Resolve names and print IPv4 and IPv6 addresses in `common/net.c`. Done when: the RFC 5952 table passes. Cheaper substitute: IPv4 only.
- [ ] Report each Windows network error in one line. Done when: an unknown name, no network and a refused address have their own text and exit code 1. Cheaper substitute: the number alone.
- [ ] Commit: `command: network calls`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## 2. ping

Echo requests with the documented switches, statistics and Ctrl+C.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/ping.c`. Done when: a loopback run prints four replies and the statistics.
2. Add the switches. Done when: each is tested.

- [ ] Send and time echoes for IPv4 and IPv6 in `src/ping.c`. Done when: output, switches and exit codes equal Windows' own tool for the cases in `tests/test_ping.py` on loopback. Cheaper substitute: IPv4 only.
- [ ] Count sent, received, lost and round trips. Done when: a run to an address that does not answer reports 100 percent loss and exit 1. Cheaper substitute: no statistics.
- [ ] Refuse bad switches and values. Done when: each documented limit has a test. Cheaper substitute: clamping.
- [ ] Commit: `command: ping`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## 3. tracert and pathping

The route to a host by rising hop limit, with names, then loss per hop.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/tracert.c`. Done when: loopback finishes in one hop.
2. Write `src/pathping.c`. Done when: the per-hop table prints.

- [ ] Raise the hop limit and print three times per hop in `src/tracert.c`. Done when: output, switches and exit codes equal Windows' own tool for the cases in `tests/test_tracert.py`. Cheaper substitute: one probe.
- [ ] Resolve names without stalling the route. Done when: a name that never answers costs at most its timeout. Cheaper substitute: numbers only.
- [ ] Measure loss per hop in `src/pathping.c`. Done when: the statistics equal counts from the raw replies. Cheaper substitute: the route alone.
- [ ] Commit: `command: tracert and pathping`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## 4. ipconfig, getmac and hostname

Adapters, addresses, gateways, name servers and leases.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/ipconfig.c`. Done when: its adapters equal Windows' own list.
2. Write the two small ones. Done when: their tests pass.

- [ ] Print the short and the full form in `src/ipconfig.c`. Done when: output, switches and exit codes equal Windows' own tool for the cases in `tests/test_ipconfig.py`, compared field by field with `ipconfig /all`. Cheaper substitute: addresses only.
- [ ] Refuse what needs administrator rights with a clear line. Done when: `/release` without rights exits 1. Cheaper substitute: pretending.
- [ ] Print addresses in `src/getmac.c` and the name in `src/hostname.c`. Done when: both equal Windows' tools. Cheaper substitute: the first adapter.
- [ ] Commit: `command: ipconfig, getmac and hostname`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## 5. netstat, arp and route

Connections, listening ports, the neighbour table and the route table, read only.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/netstat.c`. Done when: a port opened by the test is listed.
2. Write the other two. Done when: they match Windows' tables.

- [ ] List TCP and UDP with owners in `src/netstat.c`. Done when: output, switches and exit codes equal Windows' own tool for the cases in `tests/test_netstat.py` for a socket the test opens. Cheaper substitute: TCP only.
- [ ] Print neighbours in `src/arp.c` and routes in `src/route.c`. Done when: both equal Windows' tables as sets. Cheaper substitute: IPv4 only.
- [ ] Refuse changes. Done when: `route add` exits 1 and says it is not made. Cheaper substitute: ignoring it.
- [ ] Commit: `command: netstat, arp and route`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## 6. nslookup

Questions to a named server over UDP and TCP, with the common record types.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `common/dns.c`. Done when: recorded answers parse.
2. Write `src/nslookup.c`. Done when: a local test server's answers print.

- [ ] Build and parse DNS messages with bounds checks in `common/dns.c`. Done when: a 120 second native fuzz run finds nothing. Cheaper substitute: trusting the server.
- [ ] Ask with retry over TCP when cut short. Done when: output, switches and exit codes equal Windows' own tool for the cases in `tests/test_nslookup.py` against a test server. Cheaper substitute: UDP only.
- [ ] Print A, AAAA, MX, NS, TXT, CNAME, PTR and SOA. Done when: each has a recorded answer test. Cheaper substitute: A only.
- [ ] Commit: `command: nslookup`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## Verification

- Every tool's cases are compared with Windows' own tool on this machine.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
