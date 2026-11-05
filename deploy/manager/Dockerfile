ARG NODE_IMAGE=node:20.19.0-bookworm-slim@sha256:5cfa999422613d3b34f766cbb814d964cbfcb76aaf3607e805da21cccb352bac
ARG MAVEN_IMAGE=maven:3.9.9-eclipse-temurin-21@sha256:3a4ab3276a087bf276f79cae96b1af04f53731bec53fb2e651aca79e4b10211e
ARG JAVA_IMAGE=eclipse-temurin:21.0.6_7-jre-jammy@sha256:903dcea12637919198041da42cd270d549656c06bfda9126be36f182bb0de72d
ARG NGINX_IMAGE=nginx:1.27.4-alpine@sha256:4ff102c5d78d254a6f0da062b3cf39eaf07f01eec0927fd21e219d0af8bc0591

FROM ${NODE_IMAGE} AS web-builder
WORKDIR /build
ENV NODE_OPTIONS=--max-old-space-size=1536
COPY main/manager-web/package*.json ./
RUN npm ci --no-audit --no-fund
COPY main/manager-web/ ./
RUN npm run test:contract && npm run test:unit -- --pool=forks --poolOptions.forks.singleFork && npm run test:snapshot
RUN npm run build

FROM ${NGINX_IMAGE} AS web
ARG SOURCE_REVISION
LABEL org.opencontainers.image.revision=${SOURCE_REVISION}
LABEL org.opencontainers.image.source="https://github.com/yanghan24/xiaozhi"
LABEL io.xiaozhi.upstream.revision="c8e60d1325650c8a31d00e36d8528f8bb74aeb11"
COPY deploy/manager/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=web-builder /build/dist/ /usr/share/nginx/html/

FROM ${MAVEN_IMAGE} AS api-builder
WORKDIR /build
ENV MAVEN_OPTS="-Xmx768m -XX:ActiveProcessorCount=2"
COPY main/manager-api/pom.xml ./
COPY main/manager-api/src/ ./src/
COPY main/manager-api/scripts/ ./scripts/
RUN mvn -B -ntp package -DskipTests

FROM api-builder AS api-tests
# An upstream contract test reads the adjacent management page source.
COPY main/manager-web/ /manager-web/

FROM ${JAVA_IMAGE} AS api
ARG SOURCE_REVISION
LABEL org.opencontainers.image.revision=${SOURCE_REVISION}
LABEL org.opencontainers.image.source="https://github.com/yanghan24/xiaozhi"
LABEL io.xiaozhi.upstream.revision="c8e60d1325650c8a31d00e36d8528f8bb74aeb11"
WORKDIR /app
COPY --from=api-builder /build/target/xiaozhi-esp32-api.jar /app/app.jar
ENV JAVA_TOOL_OPTIONS="-Xms128m -Xmx512m -XX:ActiveProcessorCount=2 -Djava.awt.headless=true"
EXPOSE 8002
ENTRYPOINT ["java", "-jar", "/app/app.jar"]
