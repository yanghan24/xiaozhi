FROM node:20.19.0-bookworm-slim@sha256:5cfa999422613d3b34f766cbb814d964cbfcb76aaf3607e805da21cccb352bac AS node
FROM maven:3.9.9-eclipse-temurin-21@sha256:3a4ab3276a087bf276f79cae96b1af04f53731bec53fb2e651aca79e4b10211e AS java
FROM python:3.10-slim-bookworm@sha256:5be5aaecb962a41567bc8041f1e9e3432cb085b037fe057f9a562630b2e4b370

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl ffmpeg git gosu libfreetype6 libopus0 openssh-client \
    procps supervisor tini unzip && rm -rf /var/lib/apt/lists/*
COPY --from=node /usr/local/ /usr/local/
COPY --from=java /opt/java/openjdk/ /opt/java/openjdk/
COPY --from=java /usr/share/maven/ /usr/share/maven/
RUN python -m pip install --no-cache-dir pexpect==4.9.0 \
    && useradd --uid 1000 --create-home --shell /bin/bash dev
ENV JAVA_HOME=/opt/java/openjdk MAVEN_HOME=/usr/share/maven \
    PATH=/home/dev/.venv/bin:/opt/java/openjdk/bin:/usr/share/maven/bin:$PATH \
    DEV_CONTAINER=true PYTHONPYCACHEPREFIX=/home/dev/.cache/python \
    PIP_CACHE_DIR=/home/dev/.cache/pip npm_config_cache=/home/dev/.npm
COPY .devcontainer/entrypoint.sh /usr/local/bin/xiaozhi-dev-entrypoint
RUN chmod 755 /usr/local/bin/xiaozhi-dev-entrypoint
WORKDIR /workspace
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/xiaozhi-dev-entrypoint"]
CMD ["sleep", "infinity"]
