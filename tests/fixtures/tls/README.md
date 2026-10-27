# Test certificates for the TLS tests

For tests only: these keys protect nothing, and anyone may read them. Every
certificate is EC P-256 and valid until 9966.

| file | what |
|---|---|
| `ca.pem` | the test CA; its key was not kept |
| `server.pem`, `server.key` | signed by the CA; names `localhost`, `127.0.0.1` and `::1` |
| `web01.pem`, `web01.key` | a client certificate signed by the CA; its only name is `web01.example.com` in the subjectAltName, the CN says something else so that a check of the CN shows |
| `rogue.pem`, `rogue.key` | self-signed, naming `web01.example.com`: a certificate the CA did not issue |
| `renewed.pem`, `renewed.key` | self-signed, naming what `server.pem` names: a server certificate a client can pin, and, trusting only it, tell apart from `server.pem` |

git keeps no permission bits but the executable one, so a checked-out key is
usually readable by everyone, and the TLS library refuses such a key. A test
copies the keys it uses and makes the copies mode 0600.

Made with OpenSSL 3, by this script, run in an empty directory (it makes all
of them at once, since the CA key is not kept):

```sh
D=2900000
ec() { openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:prime256v1 -out "$1"; }
ec ca.key
openssl req -x509 -new -key ca.key -days $D -subj "/CN=Xymon test CA" -out ca.pem \
	-addext basicConstraints=critical,CA:TRUE -addext keyUsage=critical,keyCertSign,cRLSign
sign() {  # NAME SUBJECT SAN EKU
	ec "$1.key"
	openssl req -new -key "$1.key" -subj "$2" -out "$1.csr"
	printf 'subjectAltName=%s\nextendedKeyUsage=%s\nbasicConstraints=CA:FALSE\n' "$3" "$4" > "$1.ext"
	openssl x509 -req -in "$1.csr" -CA ca.pem -CAkey ca.key -CAcreateserial -days $D -sha256 -extfile "$1.ext" -out "$1.pem"
	rm -f "$1.csr" "$1.ext"
}
sign server "/CN=localhost" "DNS:localhost,IP:127.0.0.1,IP:::1" serverAuth
sign web01  "/CN=not-the-name-checked" "DNS:web01.example.com" clientAuth
ec rogue.key
openssl req -x509 -new -key rogue.key -days $D -subj "/CN=web01.example.com" -out rogue.pem \
	-addext subjectAltName=DNS:web01.example.com -addext extendedKeyUsage=clientAuth
rm -f ca.key ca.srl
```

`renewed` was added later. Being self-signed, it needs no CA key:

```sh
ec renewed.key
openssl req -x509 -new -key renewed.key -days $D -subj "/CN=localhost" -out renewed.pem \
	-addext subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1 -addext extendedKeyUsage=serverAuth
```
