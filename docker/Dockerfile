# Base image with JDK (required for Gradle + Android builds)
FROM eclipse-temurin:17-jdk

# Gradle environment
ENV GRADLE_VERSION=8.6
ENV GRADLE_HOME=/home/dev/gradle
ENV PATH=$GRADLE_HOME/bin:$PATH

# Install required packages
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        unzip \
        python3 \
        python3-pip \
        git \
        adb \
        ca-certificates \
        curl \
        bash && \
    rm -rf /var/lib/apt/lists/*

# Add non-root user for ADB
RUN groupadd -r adbusers && useradd -rm -g adbusers dev
USER dev
WORKDIR /home/dev

# Install Gradle
RUN curl -fsSL https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip -o gradle.zip && \
    unzip gradle.zip -d /home/dev && \
    mv /home/dev/gradle-${GRADLE_VERSION} $GRADLE_HOME && \
    rm gradle.zip

# Clone or update repo at runtime
# This ensures container always pulls the latest version
RUN git clone --recursive https://github.com/BinaryInkTN/AromaUI.git  
WORKDIR /home/dev/AromaUI
RUN mkdir -p /home/dev/AromaUI/local 

RUN chmod +x tools/setup_android_sdk.sh 
RUN ./tools/setup_android_sdk.sh 
WORKDIR /home/dev/AromaUI/android_test
RUN python3 ../tools/cli/aroma.py build android



# Set as entrypoint
CMD [ "bash" ]