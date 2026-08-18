# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile (SMALL form) -- builds ON TOP of the org C/C++ base image
# (ghcr.io/savantenvs/base). Fossilize is a small, dependency-light C++14 library (rapidjson +
# vendored miniz + vendored Vulkan headers are all IN the working tree as submodules/vendored
# files -- no apt -dev packages, no vcpkg/conan/FetchContent needed).
#
# Build (from the repo root, on the `mayhem` branch):
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile \
#       --build-arg COMMIT=$(git rev-parse HEAD) \
#       [--build-arg SOURCE_DATE_EPOCH=$(date +%s)] \
#       -t mytag .

FROM ghcr.io/savantenvs/base:20260621

ARG COMMIT
ARG REPO_URL
# SOURCE_DATE_EPOCH stays an ARG (available to build.sh); an empty value breaks clang, so it is
# never baked as ENV -- build.sh unsets it when empty.
ARG SOURCE_DATE_EPOCH

# Sanitizer override: the base exports SANITIZER_FLAGS (ASan+UBSan, halting) as the default.
# Declared WITHOUT a default so an unset arg lets the base ENV flow through to build.sh, while an
# explicit --build-arg (including an EMPTY one, for a no-sanitizer build) overrides it.
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# Source enters from the build CONTEXT (the CI checkout): the FULL repo (incl. .git history and
# the rapidjson / cli/SPIRV-Headers / cli/volk / cli/dirent submodule content -- see
# mayhem/build.sh; only rapidjson is actually compiled against, the others just need to be present
# so `git submodule update --init --recursive` in CI leaves a consistent checkout) is copied to
# /mayhem; build.sh builds in place.
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN bash mayhem/build.sh

# Fossilize's own upstream test suite (5 of its `add_test()` binaries, built above with NORMAL
# flags) + 3 KAT probes -- a failing suite fails the image.
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
