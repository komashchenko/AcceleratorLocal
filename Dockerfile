FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest-container-runtime-depot

ARG LLVM_VERSION=21

RUN wget -qO /usr/share/keyrings/llvm-snapshot.asc https://apt.llvm.org/llvm-snapshot.gpg.key \
	&& echo "deb [signed-by=/usr/share/keyrings/llvm-snapshot.asc] https://apt.llvm.org/bullseye/ llvm-toolchain-bullseye-${LLVM_VERSION} main" \
		> /etc/apt/sources.list.d/llvm.list \
	&& apt-get update \
	&& apt-get install -y --no-install-recommends clang-${LLVM_VERSION} ninja-build \
	&& rm -rf /var/lib/apt/lists/*

ENV CC=clang-${LLVM_VERSION} \
	CXX=clang++-${LLVM_VERSION}

WORKDIR /workspace
