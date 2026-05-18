# Asterisk fixture

Local Asterisk for integration tests. Provides two endpoints (`1001` / `compactphone1001`, `1002` / `compactphone1002`) and an echo extension (`600`).

The Asterisk container runs as a service in the dev docker-compose so the dev container can reach it at `asterisk:5060` over the shared `compactphone-net` bridge network. There is no separate `docker-compose` file here — the service is defined in `tools/dev/docker-compose.yml` and the config files in this directory are bind-mounted into it.

## Run

```bash
docker compose -f tools/dev/docker-compose.yml up -d asterisk
```

(Bringing up `dev` also starts `asterisk` because the test suite depends on it.)

## Verify

```bash
docker compose -f tools/dev/docker-compose.yml exec asterisk \
  asterisk -rx "pjsip show endpoints"
```

Both endpoints should appear, `Unavailable` until CompactPhone registers.

## From inside the dev container

The Asterisk service is reachable at host `asterisk` (DNS alias on the shared network):

```bash
docker compose -f tools/dev/docker-compose.yml exec dev \
  nc -uvz asterisk 5060
```
