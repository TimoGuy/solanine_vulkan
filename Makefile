OUT_PATH         = x64/Debug/solanine_vulkan.exe
OUT_PATH_RELEASE = x64/Release/solanine_vulkan.exe

.PHONY: all
all:
	@make build
	@make run

.PHONY: build
build:
	@msbuild.exe -m -noLogo -p:Configuration=Debug

.PHONY: run
run:
	@(cd solanine_vulkan && ../$(OUT_PATH))

.PHONY: release
release:
	@make build_release
	@make run_release

.PHONY: build_release
build_release:
	@msbuild.exe -m -noLogo -p:Configuration=Release

.PHONY: run_release
run_release:
	@(cd solanine_vulkan && ../$(OUT_PATH_RELEASE))

.PHONY: clean
clean:
	@msbuild.exe -m -noLogo -t:Clean

