FROM python:3.10-slim-bookworm@sha256:5be5aaecb962a41567bc8041f1e9e3432cb085b037fe057f9a562630b2e4b370 AS runtime

ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 LANG=C.UTF-8
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends ffmpeg libopus0 \
    && rm -rf /var/lib/apt/lists/*
COPY deploy/voice/requirements.txt /tmp/requirements.txt
RUN --mount=type=cache,target=/root/.cache/pip \
    (pip install --timeout 120 --retries 3 --index-url https://mirrors.aliyun.com/pypi/simple/ -r /tmp/requirements.txt \
     || pip install --timeout 120 --retries 3 -r /tmp/requirements.txt) \
    && pip check \
    && pip freeze > /usr/local/share/xiaozhi-requirements.txt
COPY main/xiaozhi-server/ /app/
COPY scripts/check-voice.py scripts/check-voice-startup.py scripts/voice_api.py /checks/
ARG SOURCE_REVISION=working-tree
LABEL org.opencontainers.image.revision=$SOURCE_REVISION \
      org.opencontainers.image.source="https://github.com/yanghan24/xiaozhi" \
      io.xiaozhi.upstream.revision="c8e60d1325650c8a31d00e36d8528f8bb74aeb11"
CMD ["python", "app.py"]

FROM runtime AS tests
RUN pip install --no-cache-dir --timeout 120 --retries 3 \
        --index-url https://mirrors.aliyun.com/pypi/simple/ \
        pytest==8.4.2 pytest-asyncio==0.26.0 freezegun==1.5.5 \
    && pip check \
    && pip freeze > /usr/local/share/xiaozhi-test-requirements.txt
COPY deploy/voice/pytest-config.yaml /app/data/.config.yaml
ENV RUN_LIVE_API_TESTS=0
CMD ["python", "-m", "pytest", "tests/", "-q"]
