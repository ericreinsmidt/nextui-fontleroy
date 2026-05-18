# Fontleroy — Root Makefile

DOCKER_IMAGE = ghcr.io/loveretro/tg5040-toolchain
DOCKER_RUN   = docker run --rm -v $(PWD):/workspace $(DOCKER_IMAGE)

.PHONY: build package clean

build:
	@bash scripts/build_tg5040_docker.sh

package: build
	@bash scripts/package_pak.sh

clean:
	$(DOCKER_RUN) make -C /workspace/ports/tg5040 clean
	rm -rf dist/
