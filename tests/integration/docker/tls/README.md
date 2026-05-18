# Test-only TLS cert

Self-signed cert used by the Asterisk integration fixture. Subject is
`CN=asterisk` with SANs for `asterisk`, `localhost`, and `127.0.0.1`,
valid for 10 years from generation.

**Never use these in production** — both the cert and private key are
public on this repo.

To regenerate:

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout server.key -out server.crt -days 3650 \
    -subj "/CN=asterisk" \
    -addext "subjectAltName=DNS:asterisk,DNS:localhost,IP:127.0.0.1"
```
