"""Minimal SNMPv1/v2c responder bound to 127.0.0.1 ONLY, for automated tests.

Answers GET and GETNEXT for a few system objects when the community matches. It never
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

QUIT_OID = '1.3.6.1.4.1.99999.0'   # asking for this makes the agent answer once more and exit

MIB = [  # sorted; a value is either encoded bytes or a function returning them
    ('1.3.6.1.2.1.1.1.0', tlv(4, b'Loopback test agent (not a real device)')),
    ('1.3.6.1.2.1.1.2.0', enc_oid('1.3.6.1.4.1.8072.3.2.10')),
    ('1.3.6.1.2.1.1.3.0', uptime_ticks),
    ('1.3.6.1.2.1.1.5.0', tlv(4, b'loopback-agent')),
    ('1.3.6.1.2.1.2.2.1.10.1', moving_counter),
    # Deliberately the wrong type: the test MIB (GOOD-MIB goodValue) defines this as Integer32.
    ('1.3.6.1.4.1.90001.1.0', tlv(0x42, (42).to_bytes(1, 'big'))),
]
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
        _, rid, j = read(pdu, 0); _, _, j = read(pdu, j); _, _, j = read(pdu, j)
        _, vbl, _ = read(pdu, j)
        oids = []; k = 0
        while k < len(vbl):
            _, vb, k = read(vbl, k)
            _, o, _ = read(vb, 0)
            oids.append(dec_oid(o))
        log.write(f'request type=0x{ptag:02x} version={ver[0]} community={com.decode()} oid={oids[0]} count={len(oids)} from={addr}\n')
        if com != community:
            log.write('  wrong community, ignored\n'); continue
        bodies = b''
        for oid in oids:
            if oid == QUIT_OID:
                quit_after_answer = True
            if ptag == 0xA0: hit = next((m for m in MIB if m[0] == oid), None)
            elif ptag in (0xA1, 0xA5): hit = next((m for m in MIB if key(m[0]) > key(oid)), None)
            else: continue
            if hit:
                bodies += tlv(0x30, enc_oid(hit[0]) + value_of(hit))
            else:
                bodies += tlv(0x30, enc_oid(oid) + (b'\x82\x00' if ver[0] else b'\x05\x00'))
        if not bodies: continue
        resp = tlv(0xA2, tlv(2, rid) + enc_int(0) + enc_int(0) + tlv(0x30, bodies))
        sock.sendto(tlv(0x30, tlv(2, ver) + tlv(4, com) + resp), addr)
        answered += 1
        if quit_after_answer or (max_answers and answered >= max_answers):
            log.write('answered %d request(s), exiting\n' % answered); sys.exit(0)
    except Exception as e:
        log.write(f'error {e!r}\n')
