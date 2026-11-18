FROM eclipse-temurin:21.0.6_7-jre-jammy@sha256:903dcea12637919198041da42cd270d549656c06bfda9126be36f182bb0de72d
ARG SOURCE_REVISION
LABEL org.opencontainers.image.revision=$SOURCE_REVISION
WORKDIR /app
COPY app.jar /app/app.jar
ENTRYPOINT ["java", "-jar", "/app/app.jar"]
