# CompactPhone — top-level developer convenience commands.
#
# Headless dev loop (everything in Docker):
#   make up        # start dev container + Asterisk fixture
#   make build     # build inside the dev container
#   make test      # run unit + integration tests
#   make shell     # interactive shell inside the dev container
#   make down      # stop everything
#
# macOS host build (for real audio acceptance):
#   make pjsip-macos       # one-time: build PJSIP natively to ~/pjproject
#   make build-macos       # configure + build the .app bundle
#   make run-macos         # launch the .app
#   make acceptance-macos  # up asterisk, run the app, print test plan
#
# Helpers:
#   make logs              # tail Asterisk logs
#   make asterisk-cli      # interactive Asterisk CLI
#   make clean             # remove build dirs
#   make vcpkg-status      # show what vcpkg has installed

SHELL := /bin/bash

COMPOSE := docker compose -f tools/dev/docker-compose.yml
DEV     := $(COMPOSE) exec -T dev

# ----- dev container (Linux, headless) ---------------------------------------

.PHONY: up
up: ## start dev container + Asterisk
	$(COMPOSE) up -d

.PHONY: down
down: ## stop dev container + Asterisk
	$(COMPOSE) down

.PHONY: shell
shell: up ## interactive shell inside the dev container
	$(COMPOSE) exec dev bash

.PHONY: build
build: up ## configure + build inside dev container
	$(DEV) bash -c "cd /workspace && cmake --preset linux && cmake --build --preset linux"

.PHONY: test
test: build ## run unit + integration tests
	$(DEV) bash -c "cd /workspace/build/linux && ctest --output-on-failure"

.PHONY: test-unit
test-unit: build ## run unit tests only
	$(DEV) bash -c "cd /workspace/build/linux && ctest --output-on-failure -L unit"

.PHONY: test-integration
test-integration: build ## run integration tests only (against Asterisk)
	$(DEV) bash -c "cd /workspace/build/linux && ctest --output-on-failure -L integration"

.PHONY: clean
clean: ## remove the build/ tree
	rm -rf build

.PHONY: rebuild
rebuild: clean build ## clean + build

# ----- Asterisk fixture helpers ----------------------------------------------

.PHONY: logs
logs: ## tail Asterisk logs
	$(COMPOSE) logs -f asterisk

.PHONY: asterisk-cli
asterisk-cli: up ## interactive Asterisk CLI
	$(COMPOSE) exec asterisk asterisk -rvvv

.PHONY: asterisk-endpoints
asterisk-endpoints: up ## list registered SIP endpoints
	$(COMPOSE) exec asterisk asterisk -rx "pjsip show endpoints"

# ----- macOS host build (for real audio acceptance) --------------------------

PJSIP_VERSION := 2.17
PJSIP_PREFIX  := $(HOME)/pjproject
PJSIP_TARBALL := /tmp/pjproject-$(PJSIP_VERSION).tar.gz
VCPKG_ROOT    ?= $(HOME)/vcpkg

.PHONY: prereqs-macos
prereqs-macos: ## install brew packages needed for native macOS build
	brew install cmake ninja pkg-config autoconf autoconf-archive automake libtool

.PHONY: vcpkg-macos
vcpkg-macos: ## clone + bootstrap vcpkg under $$VCPKG_ROOT (default ~/vcpkg)
	@if [ ! -d "$(VCPKG_ROOT)" ]; then \
	    git clone https://github.com/microsoft/vcpkg.git "$(VCPKG_ROOT)"; \
	    "$(VCPKG_ROOT)/bootstrap-vcpkg.sh" -disableMetrics; \
	else \
	    echo "vcpkg already at $(VCPKG_ROOT)"; \
	fi

.PHONY: pjsip-macos
pjsip-macos: ## one-time: build PJSIP $(PJSIP_VERSION) natively to $(PJSIP_PREFIX)
	@if [ -f "$(PJSIP_PREFIX)/lib/pkgconfig/libpjproject.pc" ]; then \
	    echo "PJSIP already installed at $(PJSIP_PREFIX). Remove it to rebuild."; \
	else \
	    set -e; \
	    cd /tmp && rm -rf pjproject-$(PJSIP_VERSION); \
	    curl -sL https://github.com/pjsip/pjproject/archive/refs/tags/$(PJSIP_VERSION).tar.gz | tar -xz; \
	    cd pjproject-$(PJSIP_VERSION) && \
	    ./aconfigure --prefix=$(PJSIP_PREFIX) \
	        --disable-shared --enable-static --with-ssl=yes \
	        --disable-video --disable-libwebrtc --disable-libyuv \
	        --disable-opencore-amr --disable-bcg729 --disable-sdl \
	        --disable-ffmpeg --disable-v4l2 --disable-openh264 && \
	    make dep && \
	    make -j$$(sysctl -n hw.ncpu) && \
	    make install; \
	    echo "PJSIP installed to $(PJSIP_PREFIX)"; \
	fi

.PHONY: build-macos
build-macos: prereqs-macos vcpkg-macos pjsip-macos ## configure + build the macOS .app
	VCPKG_ROOT=$(VCPKG_ROOT) \
	PKG_CONFIG_PATH=$(PJSIP_PREFIX)/lib/pkgconfig \
	    cmake --preset macos
	VCPKG_ROOT=$(VCPKG_ROOT) \
	PKG_CONFIG_PATH=$(PJSIP_PREFIX)/lib/pkgconfig \
	    cmake --build --preset macos

.PHONY: run-macos
run-macos: ## launch the macOS .app (must be built first)
	@if [ ! -d "build/macos/src/Compact Phone.app" ]; then \
	    echo "Not built yet — run 'make build-macos' first."; exit 1; \
	fi
	open "build/macos/src/Compact Phone.app"

.PHONY: acceptance-macos
acceptance-macos: up build-macos ## full acceptance: Asterisk up, app launched, prints test plan
	@echo
	@echo "===================================================================="
	@echo "Asterisk fixture is up (UDP 5060, TLS 5061 on host)."
	@echo "Launching CompactPhone..."
	@echo
	@echo "Test plan:"
	@echo "  1. Click 'Allow' on the macOS microphone-permission prompt."
	@echo "  2. Accounts tab > Add:"
	@echo "       Username: 1001"
	@echo "       Password: compactphone1001"
	@echo "       Server:   127.0.0.1:5060"
	@echo "     Click Connect. Status should reach 'registered' within 5s."
	@echo "  3. Dialer tab > leave the default sip:600@127.0.0.1:5060, click Call."
	@echo "  4. You should hear the Asterisk echo-test prompt, then your own"
	@echo "     voice echoed back within ~1 second."
	@echo "  5. Click Hang up. Quit with Cmd-Q."
	@echo "===================================================================="
	@echo
	$(MAKE) run-macos

# ----- diagnostics -----------------------------------------------------------

.PHONY: vcpkg-status
vcpkg-status: up ## show vcpkg installed packages in the dev container
	$(DEV) bash -c "ls /opt/vcpkg/installed/arm64-linux/share 2>/dev/null | head -30"

# ----- remote build (offload to a VPS) ---------------------------------------
# Heavy compile workloads can run on a powerful Linux VPS instead of the
# laptop. The flow: rsync the workspace to the VPS, then docker compose
# up + build there. Source of truth is your laptop; the VPS keeps an
# incrementally-updated mirror at $(REMOTE_BUILD_PATH).
#
# Configuration is read from .env.local (gitignored) — see
# .env.local.example. Required: REMOTE_BUILD_HOST. Optional:
# REMOTE_BUILD_PATH (default /srv/compactphone-build).
#
# Public-repo discipline: never commit the actual hostname. The Makefile
# refuses to run remote-* targets unless REMOTE_BUILD_HOST is set, so a
# clean clone can't accidentally hit anyone else's VPS.

# Sourced silently — file is optional. If it doesn't exist, env vars from
# the user's shell still win.
-include .env.local

REMOTE_BUILD_HOST ?=
REMOTE_BUILD_PATH ?= /srv/compactphone-build

# Files we do NOT want to ship to the VPS: build artifacts (the VPS has
# its own), vcpkg's local install tree (rebuilt remotely), local-only
# secrets, IDE state, and the .git history (the VPS doesn't need to push
# anywhere — it just builds whatever source we send).
_RSYNC_EXCLUDES := \
	--exclude=build/ \
	--exclude=build-*/ \
	--exclude=vcpkg_installed/ \
	--exclude=.git/ \
	--exclude=.env.local \
	--exclude=.vscode/ \
	--exclude=.idea/ \
	--exclude=.DS_Store

# Guard target — every remote-* target depends on this so we fail fast
# with a useful message instead of running ssh with an empty host.
.PHONY: _remote-guard
_remote-guard:
	@if [ -z "$(REMOTE_BUILD_HOST)" ]; then \
	    echo "REMOTE_BUILD_HOST is unset. Copy .env.local.example to"; \
	    echo ".env.local and fill in your VPS host (or export the var"; \
	    echo "in your shell). See tools/dev/remote-builds.md."; \
	    exit 1; \
	fi

.PHONY: remote-sync
remote-sync: _remote-guard ## rsync workspace to $(REMOTE_BUILD_HOST):$(REMOTE_BUILD_PATH)
	@echo ">> syncing workspace to $(REMOTE_BUILD_HOST):$(REMOTE_BUILD_PATH)"
	@ssh $(REMOTE_BUILD_HOST) "mkdir -p $(REMOTE_BUILD_PATH)"
	rsync -az --delete $(_RSYNC_EXCLUDES) ./ $(REMOTE_BUILD_HOST):$(REMOTE_BUILD_PATH)/

# The VPS needs only docker + docker compose; we invoke compose directly
# over SSH rather than depending on `make` being installed there.
_REMOTE_COMPOSE := docker compose -f tools/dev/docker-compose.yml
_REMOTE_DEV     := $(_REMOTE_COMPOSE) exec -T dev

.PHONY: remote-up
remote-up: remote-sync ## start dev container + Asterisk on the VPS
	ssh $(REMOTE_BUILD_HOST) "cd $(REMOTE_BUILD_PATH) && $(_REMOTE_COMPOSE) up -d"

.PHONY: remote-rebuild-image
remote-rebuild-image: remote-sync ## rebuild the dev container image on the VPS (after Dockerfile change)
	ssh $(REMOTE_BUILD_HOST) "cd $(REMOTE_BUILD_PATH) && $(_REMOTE_COMPOSE) build dev && $(_REMOTE_COMPOSE) up -d --force-recreate dev"

.PHONY: remote-build
remote-build: remote-up ## build inside the dev container on the VPS
	ssh $(REMOTE_BUILD_HOST) "cd $(REMOTE_BUILD_PATH) && $(_REMOTE_DEV) bash -c 'cd /workspace && cmake --preset linux && cmake --build --preset linux'"

.PHONY: remote-test
remote-test: remote-build ## run unit + integration tests on the VPS
	ssh $(REMOTE_BUILD_HOST) "cd $(REMOTE_BUILD_PATH) && $(_REMOTE_DEV) bash -c 'cd /workspace/build/linux && ctest --output-on-failure'"

.PHONY: remote-shell
remote-shell: remote-up ## interactive shell inside the dev container on the VPS
	ssh -t $(REMOTE_BUILD_HOST) "cd $(REMOTE_BUILD_PATH) && $(_REMOTE_COMPOSE) exec dev bash"

.PHONY: remote-down
remote-down: _remote-guard ## stop dev container + Asterisk on the VPS
	ssh $(REMOTE_BUILD_HOST) "cd $(REMOTE_BUILD_PATH) && $(_REMOTE_COMPOSE) down"

.PHONY: remote-status
remote-status: _remote-guard ## show docker + build status on the VPS
	@echo ">> $(REMOTE_BUILD_HOST):$(REMOTE_BUILD_PATH)"
	@ssh $(REMOTE_BUILD_HOST) "cd $(REMOTE_BUILD_PATH) 2>/dev/null && $(_REMOTE_COMPOSE) ps 2>/dev/null || echo '(workspace not synced yet)'"

# ----- documentation (web/docs/) --------------------------------------------
# The compactphone.app landing page lives in the MyWebs site repo. Only the
# Markdown-based docs under web/docs/ live here; MyWebs's own deploy
# pipeline pulls them into the public site.

.PHONY: docs-list
docs-list: ## list the docs source pages
	@ls -1 web/docs/pages/*.md 2>/dev/null || echo "(no docs yet)"

.PHONY: help
help: ## show this help
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage: make <target>\n\nTargets:\n"} \
	      /^[a-zA-Z_-]+:.*?##/ { printf "  \033[1m%-22s\033[0m %s\n", $$1, $$2 }' $(MAKEFILE_LIST)

.DEFAULT_GOAL := help
