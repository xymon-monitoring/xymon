#!/usr/bin/env python3
"""Record the real conversation a shipped protocols.cfg entry has with a server.

Speaks exactly what the entry sends, logs every byte in both directions, and
saves a transcript usable as a test fixture.
"""
import socket, ssl, sys, time, datetime, pathlib

def rec(name, host, ip, port, steps, mode="plain"):
    out = [f"# {name}  {host} ({ip}) port {port}  mode={mode}",
           f"# recorded {datetime.datetime.utcnow().isoformat()}Z"]
    cert = None
    try:
        raw = socket.create_connection((ip, port), timeout=20)
        raw.settimeout(20)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
        s = raw
        if mode == "implicit":
            s = ctx.wrap_socket(raw, server_hostname=host)
            out.append("# [TLS handshake at connect]")
            cert = s.getpeercert(binary_form=False) or s.getpeercert(True)
        buf = b""
        def readsome(limit=8192):
            try:
                d = s.recv(limit)
            except Exception as e:
                out.append(f"# read error: {e}"); return b""
            for line in d.split(b"\r\n"):
                if line: out.append("S: " + line.decode("utf-8", "replace"))
            return d
        for st in steps:
            kind, val = st
            if kind == "read":
                readsome()
            elif kind == "send":
                out.append("C: " + val.rstrip("\r\n"))
                s.sendall(val.encode())
            elif kind == "starttls":
                out.append("# [STARTTLS upgrade]")
                s = ctx.wrap_socket(s, server_hostname=host)
                cert = s.getpeercert(binary_form=False) or True
            elif kind == "sleep":
                time.sleep(val)
        try: s.close()
        except Exception: pass
    except Exception as e:
        out.append(f"# CONNECT/PROTOCOL ERROR: {e}")
    if cert:
        out.append(f"# peer certificate present: {str(cert)[:200]}")
    p = pathlib.Path(f"{name}.txt"); p.write_text("\n".join(out) + "\n")
    print(f"{name:<22} {len(out):>3} lines -> {p}")

E = "\r\n"
rec("smtp-mx",     "gmail-smtp-in.l.google.com", "142.251.127.26", 25,
    [("read",None),("send","ehlo xymonnet"+E),("read",None),("send","quit"+E),("read",None)])
rec("submissiontls-587", "smtp.gmail.com", "142.251.127.108", 587,
    [("read",None),("send","ehlo xymonnet"+E),("read",None),("send","starttls"+E),("read",None),
     ("starttls",None),("send","ehlo xymonnet"+E),("read",None),("send","quit"+E),("read",None)])
rec("smtps-465",   "smtp.gmail.com", "142.251.127.108", 465,
    [("read",None),("send","ehlo xymonnet"+E),("read",None),("send","quit"+E),("read",None)], mode="implicit")
rec("imaps-993",   "imap.gmail.com", "108.177.96.109", 993,
    [("read",None),("send","ABC123 LOGOUT"+E),("read",None)], mode="implicit")
rec("pop3s-995",   "pop.gmail.com", "142.251.127.108", 995,
    [("read",None),("send","quit"+E),("read",None)], mode="implicit")
rec("ftp-21",      "ftp.gnu.org", "209.51.188.20", 21,
    [("read",None),("send","quit"+E),("read",None)])
rec("ssh-22",      "github.com", "140.82.121.3", 22, [("read",None)])
rec("telnet-23",   "telehack.com", "64.13.139.230", 23, [("read",None),("sleep",1),("read",None)])
