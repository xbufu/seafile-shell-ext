import os, socket, struct, threading, sys, json
home = sys.argv[1]; state = json.load(open(home + '/state.json'))
p = home + '/.seadrive/seadrive_ext.sock'
srv = socket.socket(socket.AF_UNIX); srv.bind(p); srv.listen()
log = open(home + '/requests.log', 'a', buffering=1)
def h(c):
    while True:
        hdr = c.recv(4)
        if len(hdr) < 4: return
        n, = struct.unpack('=I', hdr); body = b''
        while len(body) < n: body += c.recv(n - len(body))
        name, path = body.decode().split('\t')
        log.write(f"{name}\t{path}\n")
        st = json.load(open(home + '/state.json'))
        r = {'is-file-in-repo': 'true', 'get-file-status': st['status'], 'is-file-cached': st['cached'],
             'show-accounts': json.dumps([{"name": "me@x", "signature": "sig1"}])}.get(name, '').encode()
        c.sendall(struct.pack('=I', len(r)) + r)
while True:
    c, _ = srv.accept(); threading.Thread(target=h, args=(c,), daemon=True).start()
