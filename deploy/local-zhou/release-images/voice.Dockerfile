FROM python:3.10-slim-bookworm@sha256:5be5aaecb962a41567bc8041f1e9e3432cb085b037fe057f9a562630b2e4b370
ARG SOURCE_REVISION
LABEL org.opencontainers.image.revision=$SOURCE_REVISION
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 \
    LD_LIBRARY_PATH=/usr/local/lib/xiaozhi-media
WORKDIR /app
COPY media/bin/ /usr/local/bin/
COPY media/lib/ /usr/local/lib/xiaozhi-media/
RUN ffmpeg -version && ffprobe -version
RUN --mount=type=bind,source=wheels,target=/wheels \
    --mount=type=bind,source=requirements.txt,target=/tmp/requirements.txt \
    pip install --no-cache-dir --no-index --find-links=/wheels -r /tmp/requirements.txt \
    && pip check
COPY app/ /app/
RUN chmod -R a+rX /app
CMD ["python", "app.py"]
