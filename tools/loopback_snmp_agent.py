"""Minimal SNMPv1/v2c responder bound to 127.0.0.1 ONLY, for automated tests.

Answers GET and GETNEXT for a few system objects when the community matches, and SET (write
community "smokerw", in memory only) for sysName.0. It never
listens on any other address, so tests using it cannot reach a real device.
Usage: loopback_snmp_agent.py <port> <community> <log-file> [max-answered-requests] [timeout-seconds]
Exits after answering max-answered-requests (default: never) or after timeout-seconds."""
import socket, sys

port, community, logfile = int(sys.argv[1]), sys.argv[2].encode(), sys.argv[3]
max_answers = int(sys.argv[4]) if len(sys.argv) > 4 else 0
timeout = float(sys.argv[5]) if len(sys.argv) > 5 else 120.0
answered = 0

def tlv(tag, body): 
    n = len(body)
    if n < 128: return bytes([tag, n]) + body
    nb = n.to_bytes((n.bit_length() + 7) // 8, 'big')
    return bytes([tag, 0x80 | len(nb)]) + nb + body
def enc_int(v):
    n = max(1, (v.bit_length() + 8) // 8)
    return tlv(2, v.to_bytes(n, 'big', signed=True))
def enc_oid(o):
    p = [int(x) for x in o.split('.')]
    b = bytes([40 * p[0] + p[1]])
    for x in p[2:]:
        s = [x & 0x7f]; x >>= 7
        while x: s.append(0x80 | (x & 0x7f)); x >>= 7
        b += bytes(reversed(s))
    return tlv(6, b)
def read(b, i):
    tag = b[i]; n = b[i+1]; i += 2
    if n & 0x80:
        k = n & 0x7f; n = int.from_bytes(b[i:i+k], 'big'); i += k
    return tag, b[i:i+n], i + n
def dec_oid(b):
    parts = [b[0] // 40, b[0] % 40]; v = 0
    for x in b[1:]:
        v = (v << 7) | (x & 0x7f)
        if not x & 0x80: parts.append(v); v = 0
    return '.'.join(map(str, parts))

import time
START = time.time()
counter_value = [1000]

def uptime_ticks():
    return tlv(0x43, ((int((time.time() - START) * 100) + 100000) & 0xFFFFFFFF).to_bytes(4, 'big'))

def moving_counter():
    # Counter32 that grows by 1000 every time it is read, so a watch sees a steady rate.
    counter_value[0] = (counter_value[0] + 1000) & 0xFFFFFFFF
    return tlv(0x41, counter_value[0].to_bytes(4, 'big'))

WRITE_COMMUNITY = b'smokerw'
WRITABLE = '1.3.6.1.2.1.1.5.0'   # sysName.0, the one object a SET can change
QUIT_OID = '1.3.6.1.4.1.99999.0'   # asking for this makes the agent answer once more and exit

MIB = [  # sorted; a value is either encoded bytes or a function returning them
    ('1.3.6.1.2.1.1.1.0', tlv(4, b'Loopback test agent (not a real device)')),
    # A vendor registration under the enterprise number the test MIB (GOOD-MIB) uses, so that
    # "Detect profile" has something to find.
    ('1.3.6.1.2.1.1.2.0', enc_oid('1.3.6.1.4.1.90001.7')),
    ('1.3.6.1.2.1.1.3.0', uptime_ticks),
    ('1.3.6.1.2.1.1.5.0', tlv(4, b'loopback-agent')),
    ('1.3.6.1.2.1.2.2.1.10.1', moving_counter),
    # Deliberately the wrong type: the test MIB (GOOD-MIB goodValue) defines this as Integer32.
    ('1.3.6.1.4.1.90001.1.0', tlv(0x42, (42).to_bytes(1, 'big'))),
]
# A small sparse table (GOOD-MIB goodTable): rows are indexed by a name (length-prefixed string),
# column 1 is the index itself (not-accessible, so never returned), column 2 a count that every
# row has, column 3 a note that only "eth1" has.
def row_index(name):
    return '.'.join([str(len(name))] + [str(ord(c)) for c in name])
for _name, _count in (('eth0', 10), ('eth1', 20), ('lo', 30)):
    MIB.append(('1.3.6.1.4.1.90001.2.1.2.' + row_index(_name), tlv(2, _count.to_bytes(1, 'big'))))
MIB.append(('1.3.6.1.4.1.90001.2.1.3.' + row_index('eth1'), tlv(4, b'uplink')))
# goodExtTable AUGMENTS goodTable: one more column per goodTable row.
for _name, _level in (('eth0', 1), ('eth1', 2), ('lo', 3)):
    MIB.append(('1.3.6.1.4.1.90001.3.1.1.' + row_index(_name), tlv(2, _level.to_bytes(1, 'big'))))
# goodImpTable: the index is an IMPLIED string, so the instance is just the characters.
for _name, _value in (('ab', 5), ('xyz', 6)):
    MIB.append(('1.3.6.1.4.1.90001.4.1.2.' + '.'.join(str(ord(c)) for c in _name), tlv(2, _value.to_bytes(1, 'big'))))
MIB.sort(key=lambda m: [int(x) for x in m[0].split('.')])
BIG = '1.3.6.1.4.1.90001.10'
BIG_COUNT = 3000
LOOP = '1.3.6.1.4.1.90001.9'
LOOP_NEXT = {LOOP: LOOP + '.1', LOOP + '.1': LOOP + '.2', LOOP + '.2': LOOP + '.1'}
def value_of(entry):
    return entry[1]() if callable(entry[1]) else entry[1]
key = lambda o: [int(x) for x in o.split('.')]
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('127.0.0.1', port))
sock.settimeout(timeout)
log = open(logfile, 'a', buffering=1)
log.write(f'listening 127.0.0.1:{port}\n')
quit_after_answer = False
while True:
    try:
        data, addr = sock.recvfrom(4096)
    except socket.timeout:
        log.write('timeout\n'); sys.exit(0)
    try:
        _, msg, _ = read(data, 0)
        _, ver, i = read(msg, 0)
        _, com, i = read(msg, i)
        ptag, pdu, _ = read(msg, i)
        _, rid, j = read(pdu, 0); _, nonrep, j = read(pdu, j); _, maxrep, j = read(pdu, j)
        _, vbl, _ = read(pdu, j)
        oids = []; k = 0
        while k < len(vbl):
            _, vb, k = read(vbl, k)
            _, o, _ = read(vb, 0)
            oids.append(dec_oid(o))
        log.write(f'request type=0x{ptag:02x} version={ver[0]} community={com.decode()} oid={oids[0]} count={len(oids)} from={addr}\n')
        # SET needs the write community; everything else the read community.
        if com != (WRITE_COMMUNITY if ptag == 0xA3 else community):
            log.write('  wrong community, ignored\n'); continue
        if ptag == 0xA3:
            # SET, kept in memory only. sysName.0 is writable (a string); the rest answers with a
            # fixed error naming the varbind, using SNMPv1 error-status values over v1.
            v2 = ver[0] != 0
            items = []; k = 0
            while k < len(vbl):
                _, vb, k = read(vbl, k)
                _, o, nxt = read(vb, 0)
                vt, vv, _ = read(vb, nxt)
                items.append((dec_oid(o), vt, vv))
            status = index = 0
            for n, (oid, vt, vv) in enumerate(items):
                if oid == WRITABLE:
                    if vt != 4: status = 7 if v2 else 3          # wrongType / badValue
                elif oid == '1.3.6.1.4.1.90001.1.0': status = 10 if v2 else 3   # wrongValue / badValue
                elif any(m[0] == oid for m in MIB): status = 17 if v2 else 4    # notWritable / readOnly
                else: status = 6 if v2 else 2                    # noAccess / noSuchName
                if status: index = n + 1; break
            if not status:
                for oid, vt, vv in items:
                    for i, m in enumerate(MIB):
                        if m[0] == oid: MIB[i] = (oid, tlv(4, vv))
            echoed = b''.join(tlv(0x30, enc_oid(o) + tlv(vt, vv)) for o, vt, vv in items)
            resp = tlv(0xA2, tlv(2, rid) + enc_int(status) + enc_int(index) + tlv(0x30, echoed))
            sock.sendto(tlv(0x30, tlv(2, ver) + tlv(4, com) + resp), addr)
            log.write(f'  set status={status} index={index}\n')
            answered += 1
            if max_answers and answered >= max_answers:
                log.write('answered %d request(s), exiting\n' % answered); sys.exit(0)
            continue
        if any(key(o)[:len(key(BIG))] == key(BIG) for o in oids):
            time.sleep(0.005)   # the large subtree answers like a device that is not instant
        def successor(oid):
            nxt = successor_real(oid)
            # A large subtree made up on demand (BIG.1 ... BIG.<BIG_COUNT>, integers), only reached
            # by walking from BIG itself or inside it; it is not part of any other walk.
            k = key(oid); b = key(BIG)
            if k[:len(b)] == b:
                n = k[len(b)] if len(k) > len(b) else 0
                if n < BIG_COUNT:
                    big = (BIG + '.' + str(n + 1), enc_int(n + 1))
                    if nxt is None or key(big[0]) < key(nxt[0]): return big
            return nxt
        def successor_real(oid):
            # A deliberately broken subtree: it answers 9.1, 9.2, then 9.1 again (an OID that is
            # lower than the one just given), so a walk that trusts the agent never ends.
            if oid in LOOP_NEXT: return (LOOP_NEXT[oid], tlv(2, b'\x07'))
            return next((m for m in MIB if key(m[0]) > key(oid)), None)
        def varbind(hit, oid):
            if hit: return tlv(0x30, enc_oid(hit[0]) + value_of(hit))
            return tlv(0x30, enc_oid(oid) + (b'\x82\x00' if ver[0] else b'\x05\x00'))
        if QUIT_OID in oids: quit_after_answer = True
        bodies = b''; error_index = 0
        if ptag == 0xA0:
            for n, oid in enumerate(oids):
                hit = next((m for m in MIB if m[0] == oid), None)
                if not hit and ver[0] == 0 and not error_index: error_index = n + 1
                bodies += varbind(hit, oid)
        elif ptag == 0xA1:
            for n, oid in enumerate(oids):
                hit = successor(oid)
                if not hit and ver[0] == 0 and not error_index: error_index = n + 1
                bodies += varbind(hit, oid)
        elif ptag == 0xA5:
            fixed = int.from_bytes(nonrep, 'big'); reps = int.from_bytes(maxrep, 'big')
            for oid in oids[:fixed]:
                bodies += varbind(successor(oid), oid)
            cursors = list(oids[fixed:])
            for _ in range(reps):
                progressed = False
                for n, oid in enumerate(cursors):
                    hit = successor(oid)
                    bodies += varbind(hit, oid)
                    if hit: cursors[n] = hit[0]; progressed = True
                if not progressed: break
        else: continue
        if not bodies: continue
        # SNMPv1 reports a missing object as noSuchName(2) naming the varbind; v2c uses exceptions.
        if error_index:
            bodies = b''.join(tlv(0x30, enc_oid(o) + b'\x05\x00') for o in oids)
        resp = tlv(0xA2, tlv(2, rid) + enc_int(2 if error_index else 0) + enc_int(error_index) + tlv(0x30, bodies))
        sock.sendto(tlv(0x30, tlv(2, ver) + tlv(4, com) + resp), addr)
        answered += 1
        if quit_after_answer or (max_answers and answered >= max_answers):
            log.write('answered %d request(s), exiting\n' % answered); sys.exit(0)
    except Exception as e:
        log.write(f'error {e!r}\n')
